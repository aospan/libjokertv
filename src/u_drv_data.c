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
	INIT_LIST_HEAD(&pool->ts_list);
	INIT_LIST_HEAD(&pool->programs_list);

	return 0;
}

/* thread for 'kicking' libusb polling 
 * TODO: not only default libusb context polling
 * */
void* process_usb(void * data) {
	int completed = 0;

#ifdef __linux__ 
	/* set FIFO schedule priority
	 * for faster USB ISOC transfer processing */
	struct sched_param p;
	p.sched_priority = sched_get_priority_max(SCHED_FIFO);
	sched_setscheduler(0, SCHED_FIFO, &p);
#endif

	while(1) {
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

	list_add_tail(&node->list, &pool->ts_list);
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
	struct libusb_transfer *t;
	int index = 0;
	int transferred = 0, rc = 0, ret = 0;

	if (!joker || !pool)
		return EINVAL;

	dev = (struct libusb_device_handle *)joker->libusb_opaque;
	if (!dev)
		return EINVAL;

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

		t = libusb_alloc_transfer(NUM_USB_PACKETS);    
		libusb_fill_iso_transfer(t, dev, USB_EP3_IN, pool->usb_buffers[index], NUM_USB_PACKETS * USB_PACKET_SIZE, NUM_USB_PACKETS, cb, (void *)pool, 1000);
		libusb_set_iso_packet_lengths(t, USB_PACKET_SIZE);

		if ((ret = libusb_submit_transfer(t))) {
			printf("ERROR:%d libusb_submit_transfer failed\n", ret);
			return EIO;
		}
	}
	
	// start ISOC USB transfers processing thread
	pthread_mutex_init(&pool->mux, NULL);
	pthread_cond_init(&pool->cond, NULL);
	pool->pkt_count = 0;
	pool->pkt_count_complete = 0;
	pool->start_time = getus();
	pool->bytes = 0;
	rc = pthread_create(&pool->thread, NULL, process_usb, (void *)&pool);
	if (rc){
		printf("ERROR; return code from pthread_create() is %d\n", rc);
		return rc;
	}
	return 0;
}

/* stop ts processing 
 * TODO*/
int stop_ts(struct joker_t *joker, struct big_pool_t * pool)
{
	return 0;
}

void drop_ts_data(struct ts_node * node)
{
	list_del(&node->list);
	free(node->data);
	free(node);
}

struct ts_node * read_ts_data(struct big_pool_t * pool)
{
	struct ts_node *node = NULL;
	
	if(!list_empty(&pool->ts_list))
		return list_first_entry(&pool->ts_list, struct ts_node, list);

	return NULL;
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

int read_ts_data_pid(struct big_pool_t *pool, int req_pid, unsigned char *data, int size)
{
	struct ts_node *node = NULL, *tmp = NULL;
	int off = 0, len = 0, res_off = 0, pid = 0;
	unsigned char * ptr = NULL, *ts_pkt = NULL, *res = NULL;

	if (!data)
		return -EINVAL;

	list_for_each_entry_safe(node, tmp, &pool->ts_list, list)
	{
		ptr = node->data;

		// nodes already aligned to TS_SIZE
		for (off = node->read_off; off < node->size; off += TS_SIZE ){
			// no more space in output buffer. stop
			if ( (res_off + TS_SIZE) > size)
				break;

			ts_pkt = ptr + off;
			// cannot use this data as TS
			if (ts_pkt[0] != TS_SYNC)
				continue; 

			pid = (ts_pkt[1] & 0x1f)<<8 | ts_pkt[2]; 
			if (pid == req_pid || req_pid == TS_WILDCARD_PID) {
				memcpy(data + res_off, ts_pkt, TS_SIZE);
				res_off += TS_SIZE;
			}

			node->read_off += TS_SIZE;
		}

		// node fully processed. drop it
		if (node->read_off == node->size)
			drop_ts_data(node);
	}

	return res_off;
}
