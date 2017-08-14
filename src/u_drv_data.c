/* 
 * this file contains driver for Joker TV card
 * Supported standards:
 *
 * DVB-S/S2 – satellite, is found everywhere in the world
 * DVB-T/T2 – mostly Europe
 * DVB-C/C2 – cable, is found everywhere in the world
 * ISDB-T – Brazil, Latin America, Japan, etc
 * ATSC – USA, Canada, Mexico, South Korea, etc
 * DTMB – China, Cuba, Hong-Kong, Pakistan, etc
 *
 * (c) Abylay Ospan <aospan@jokersys.com>, 2017
 * LICENSE: GPLv2
 * https://tv.jokersys.com
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>
#include <libusb.h>
#include <pthread.h>
#include "joker_tv.h"
#include "joker_fpga.h"
#include "u_drv_data.h"
#include "joker_utils.h"

/* helper func 
 * get current time in usec */
uint64_t getus() {
	struct timeval tv;
	gettimeofday(&tv,NULL);
	return tv.tv_sec*(uint64_t)1000000+tv.tv_usec;
}

/* init pool */
int pool_init(struct big_pool_t * pool)
{
	if (!pool)
		return -EINVAL;

	pool->node_counter = 0;
	pool->tail_size = 0;
	pool->ts_list_size = 0;
	if (pool->ts_list_size_max <= 0)
		pool->ts_list_size_max = TS_LIST_SIZE_DEFAULT;

	INIT_LIST_HEAD(&pool->ts_list);
	INIT_LIST_HEAD(&pool->ts_list_all);
	INIT_LIST_HEAD(&pool->programs_list);

	pthread_mutex_init(&pool->mux_all, NULL);
	pthread_mutex_init(&pool->mux, NULL);
	pthread_cond_init(&pool->cond_all, NULL);
	pthread_cond_init(&pool->cond, NULL);

	memset(&pool->hooks, 0, sizeof(pool->hooks));
	memset(&pool->transfers, 0, sizeof(pool->transfers));

	pool->initialized = BIG_POOL_MAGIC;

	return 0;
}

/* thread for processing Transport Stream packets
 */
void* process_ts(void * data) {
	struct big_pool_t * pool = (struct big_pool_t *)data;
	struct ts_node * node = NULL;
	unsigned char * pkt = NULL;
	int pid = 0, i = 0;

	while(!pool->cancel) {
		// get node from the list with locking (safe)
		pthread_mutex_lock(&pool->mux);
		if(list_empty(&pool->ts_list))
			pthread_cond_wait(&pool->cond, &pool->mux);
		if(!list_empty(&pool->ts_list)) {
			node = list_first_entry(&pool->ts_list, struct ts_node, list);
			list_del(&node->list);
		} else {
			node = NULL;
		}
		pthread_mutex_unlock(&pool->mux);

		if (!node)
			continue;

		// process hooks
		for (i = 0; i < node->size; i += TS_SIZE) {
			pkt = node->data + i;
			pid = (pkt[1]&0xf) << 8 | pkt[2];

			if(pool->hooks[pid]) {
				jdebug("calling hook pid=0x%x pool=%p pkt=%p\n", pid, pool, pkt);
				pool->hooks[pid](pool, pkt);
			}
		}

		// save node to list 
		pthread_mutex_lock(&pool->mux_all);
		list_add_tail(&node->list, &pool->ts_list_all);
		pool->ts_list_size += node->size;

		// keep list in desired memory limit (remove old nodes)
		jdebug("TS:all: ts_list_size=%d\n", pool->ts_list_size);
		while (pool->ts_list_size > pool->ts_list_size_max && !list_empty(&pool->ts_list_all)) {
			node = list_first_entry(&pool->ts_list_all, struct ts_node, list);
			pool->ts_list_size -= node->size;
			drop_ts_data(node);
			jdebug("TS:all: drop node %p. ts_list_size=%d\n", node, pool->ts_list_size);
		}

		jdebug("TS:all: node %p inserted. ts_list_size=%d\n", node, pool->ts_list_size);
		pthread_mutex_unlock(&pool->mux_all);
		pthread_cond_signal(&pool->cond_all); // wakeup read threads
	}
}

/* thread for 'kicking' libusb polling 
 * TODO: not only default libusb context polling
 * */
void* process_usb(void * data) {
	int completed = 0;
	struct big_pool_t * pool = (struct big_pool_t *)data;

#ifdef __linux__ 
	/* set FIFO schedule priority
	 * for faster USB ISOC transfer processing */
	struct sched_param p;
	p.sched_priority = sched_get_priority_max(SCHED_FIFO);
	sched_setscheduler(0, SCHED_FIFO, &p);
#endif

	while(!pool->cancel) {
		struct timeval tv = {
			.tv_sec = 0,
			.tv_usec = 1000
		};
		libusb_handle_events_timeout_completed(NULL, &tv, &completed);
	}
}

/* callback called by libusb when USB ISOC transfer completed */
void record_callback(struct libusb_transfer *transfer)
{
	struct libusb_iso_packet_descriptor pkt;
	struct big_pool_t * pool = (struct big_pool_t *)transfer->user_data;
	int i;
	unsigned char * buf = 0;
	int remain = 0, to_write = 0;
	int err_counter = 0;
	int ret = 0;
	struct ts_node * node = NULL;
	int total_len = 0;
	int off = 0, cnt = 0, ts_off = 0, len = 0;

	// looks like we stopping TS processing. do not submit this transfer
	if(transfer->status == LIBUSB_TRANSFER_CANCELLED)
		return;

	/* update statistics */
	if ( pool->pkt_count && !(pool->pkt_count%1000) ){
		printf("USB ISOC: all/complete=%f/%f transfer/sec %.2f MBytes %f mbits/sec \n", 
				(double)((int64_t)1000000*pool->pkt_count)/(getus() - pool->start_time),
				(double)((int64_t)1000000*pool->pkt_count_complete)/(getus() - pool->start_time),
				(double)pool->bytes/1024/1024,
				(double)((int64_t)1000000*8*pool->bytes/1048576)/(getus() - pool->start_time));
		fflush(stdout);
		pool->pkt_count = 0;
		pool->pkt_count_complete = 0;
		pool->bytes = 0;
		pool->start_time = getus();
	}

	// fill TS list with received data
	for(i = 0; i < transfer->num_iso_packets; i++) {
		pkt = transfer->iso_packet_desc[i];
		if (pkt.status == LIBUSB_TRANSFER_COMPLETED)
			total_len += transfer->iso_packet_desc[i].actual_length;
	}

	node = malloc(sizeof(*node));
	if(!node)
		return;
	memset(node, 0, sizeof(*node));

	node->data = malloc(total_len + TS_SIZE);
	if (!node->data)
		return;

	node->size = 0;
	node->counter = pool->node_counter++;

	// traversal of ISOC packets and copy data to TS list
	// data may be not aligned to TS_SIZE so we use "tail"
	for(i = 0; i < transfer->num_iso_packets; i++) {
		pkt = transfer->iso_packet_desc[i];
		len = transfer->iso_packet_desc[i].actual_length;
		pool->pkt_count++;

		if (pkt.status == LIBUSB_TRANSFER_COMPLETED && len > 0) {
			pool->pkt_count_complete++;
			pool->bytes += len;
			jdebug("ISOC size=%d \n", len );
			if ((buf = libusb_get_iso_packet_buffer(transfer, i))) {
				if (buf[TS_SIZE - pool->tail_size] == TS_SYNC)
					ts_off = TS_SIZE - pool->tail_size; // tail is ok. use it
				else
					ts_off = next_ts_off(buf, len);
				jdebug("	ts_off=%d tail_size=%d\n", ts_off, pool->tail_size);
				if (ts_off < 0)
					break;

				if ((ts_off + pool->tail_size) == TS_SIZE) {
					jdebug("	 tail OK\n");
					// tail from previous round is ok
					// use it
					memcpy(node->data + off, pool->tail, pool->tail_size);
					off += pool->tail_size;

					memcpy(node->data + off, buf, ts_off);
					off += ts_off;

					node->size = node->size + pool->tail_size + ts_off;
					pool->tail_size = 0;
				} else {
					// just drop useless tail
					jdebug("	 tail size=%d DROP\n", pool->tail_size);
					pool->tail_size = 0;
				}

				// process rest of the buffer
				cnt = (len - ts_off)/TS_SIZE;
				jdebug("	 cnt=%d\n", cnt);

				memcpy(node->data + off, buf + ts_off, cnt * TS_SIZE); 
				off += cnt * TS_SIZE;
				node->size += cnt * TS_SIZE;

				// copy tail if exist
				if ( (ts_off + cnt * TS_SIZE) < len) {
					pool->tail_size = len - (ts_off + cnt * TS_SIZE);
					memcpy(pool->tail, buf + (ts_off + cnt * TS_SIZE), pool->tail_size);
					jdebug("tail=0x%x tailoff=0x%x\n", pool->tail[0], (ts_off + cnt * TS_SIZE));
				}

			}
		}
	}

	// add node to the list with locking (safe)
	pthread_mutex_lock(&pool->mux);
	list_add_tail(&node->list, &pool->ts_list);
	pthread_mutex_unlock(&pool->mux);
	pthread_cond_signal(&pool->cond); // wakeup ts procesing thread
	jdebug("TSLIST:added to tslist. total_len=%d \n", total_len);

	// TODO: delete old nodes in TS list


	// return USB ISOC ASAP !
	while(1) {
		if ((ret = libusb_submit_transfer(transfer))) {
			if(!(err_counter%1000))
				printf("CALLBACK: ERROR: libusb_submit_transfer failed ret=%d. err_counter=%d\n", 
						ret, err_counter);
			err_counter++;
			usleep(100);
			if (err_counter > 10000) {
				// TODO: reinit usb device
				printf("too much errors. exiting ... \n");
				return;
			}
		}else{
			// printf("transfer return back done \n");
			break;
		}
	}
}

/* start TS processing thread 
*/
int start_ts(struct joker_t *joker, struct big_pool_t *pool)
{
	libusb_transfer_cb_fn cb = record_callback;
	struct libusb_device_handle *dev = NULL;
	int index = 0;
	int transferred = 0, rc = 0, ret = 0;

	if (!joker || !pool)
		return EINVAL;

	dev = (struct libusb_device_handle *)joker->libusb_opaque;
	if (!dev)
		return EINVAL;

	// sanity check
	if (pool->initialized != BIG_POOL_MAGIC)
		pool_init(pool);

#ifdef __linux__ 
	/* set FIFO schedule priority
	 * for faster USB ISOC transfer processing */
	struct sched_param p;
	p.sched_priority = sched_get_priority_max(SCHED_FIFO);
	sched_setscheduler(0, SCHED_FIFO, &p);
#endif

	// create isochronous transfers
	// USB isoc transfer should be delivered to Joker TV 
	// every microframe (125usec)
	// One isoc transfer size is 512 bytes (max 1024)
	for (index = 0; index < NUM_USB_BUFS; index++) {
		pool->usb_buffers[index] = (uint8_t*)malloc(NUM_USB_PACKETS * USB_PACKET_SIZE);
		memset(pool->usb_buffers[index], 0, NUM_USB_PACKETS * USB_PACKET_SIZE);

		pool->transfers[index] = libusb_alloc_transfer(NUM_USB_PACKETS);    
		libusb_fill_iso_transfer(pool->transfers[index], dev, USB_EP3_IN, pool->usb_buffers[index], NUM_USB_PACKETS * USB_PACKET_SIZE, NUM_USB_PACKETS, cb, (void *)pool, 1000);
		libusb_set_iso_packet_lengths(pool->transfers[index], USB_PACKET_SIZE);

		if ((ret = libusb_submit_transfer(pool->transfers[index]))) {
			printf("ERROR:%d libusb_submit_transfer failed\n", ret);
			return EIO;
		}
		jdebug("submit usb transfer %d (%p) done\n", index, pool->transfers[index]);
	}
	
	// start ISOC USB transfers processing thread
	pool->pkt_count = 0;
	pool->pkt_count_complete = 0;
	pool->start_time = getus();
	pool->bytes = 0;
	pool->cancel = 0;

	rc = pthread_create(&pool->usb_thread, NULL, process_usb, (void *)pool);
	if (rc){
		printf("ERROR: can't start USB processing thread. code=%d\n", rc);
		return rc;
	}

	// start TS processing thread
	rc = pthread_create(&pool->ts_thread, NULL, process_ts, (void *)pool);
	if (rc){
		printf("ERROR: can't start TS processing thread. code=%d\n", rc);
		pool->cancel = 1; // will stop usb processing
		return rc;
	}

	return 0;
}

/* stop ts processing 
 * TODO*/
int stop_ts(struct joker_t *joker, struct big_pool_t * pool)
{
	int index = 0;

	for (index = 0; index < NUM_USB_BUFS; index++) {
		if(pool->transfers[index] && libusb_cancel_transfer(pool->transfers[index]))
			printf("can't cancel usb transfer %d (%p) \n", index, pool->transfers[index]);
		else
			jdebug("cancel usb transfer %d (%p) \n", index, pool->transfers[index]);
		pool->transfers[index] = NULL;
	}

	// stop USB and TS processing threads
	pool->cancel = 1;
	pthread_cond_signal(&pool->cond); // wakeup TS procesing thread

	// lock until thread ended
	pthread_join(pool->usb_thread, NULL);

	return 0;
}

void drop_ts_data(struct ts_node * node)
{
	list_del(&node->list);
	free(node->data);
	free(node);
}

/* find next TS packet start
 * 0x47 - sync byte
 * TS packet size hardcoded to 188 bytes
 * return offset of TS packet start 
 * return -1 if no TS packets start found */
int next_ts_off(unsigned char *buf, size_t size)
{
	int off = 0;

	if (size < TS_SIZE)
		return -1;

	for (off = 0; off <= (size - TS_SIZE); )
	{
		if (buf[off] == TS_SYNC && (off + TS_SIZE) == size)
			return off;
		if (buf[off] == TS_SYNC && buf[off + TS_SIZE] == TS_SYNC)
			return off;
		off++;
	 }

	return -1;
}

int read_ts_data(struct big_pool_t *pool, unsigned char *data, int size)
{
	struct ts_node *node = NULL, *tmp = NULL;
	int off = 0, len = 0, res_off = 0, pid = 0;
	unsigned char * ptr = NULL, *ts_pkt = NULL, *res = NULL;
	int remain = size;

	if (!data)
		return -EINVAL;

	// sanity check
	if (pool->initialized != BIG_POOL_MAGIC)
		return -EINVAL;

	while(remain) {
		// get node from the list with locking (safe)
		pthread_mutex_lock(&pool->mux_all);
		if(list_empty(&pool->ts_list_all))
			pthread_cond_wait(&pool->cond_all, &pool->mux_all);
		
		if(!list_empty(&pool->ts_list_all)) {
			jdebug("req:%d \n", size);
			list_for_each_entry_safe(node, tmp, &pool->ts_list_all, list)
			{
				jdebug("	node=%d size:%d read_off=%d\n", node->counter, node->size, node->read_off);
				if (remain > (node->size - node->read_off)) {
					// copy all node to output
					memcpy(data + res_off, node->data + node->read_off, node->size - node->read_off);
					// node fully processed. drop it
					remain -= (node->size - node->read_off);
					res_off += (node->size - node->read_off);
					pool->ts_list_size -= node->size;
					drop_ts_data(node);
				} else {
					memcpy(data + res_off, node->data + node->read_off, remain);
					node->read_off += remain;
					res_off += remain;
					remain = 0;
				}

				if(!remain)
					break;
			}
		}
		pthread_mutex_unlock(&pool->mux_all);
	}

	return res_off;
}
