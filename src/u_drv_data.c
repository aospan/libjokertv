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
#include "joker_ts.h"
#include "joker_ts_filter.h"
#include "joker_fpga.h"
#include "u_drv_data.h"
#include "joker_utils.h"

struct thread_opaq_t
{
	/* USB processing thread */
	pthread_t usb_thread;
	/* TS processing thread */
	pthread_t ts_thread;
	pthread_cond_t cond_all;
	pthread_mutex_t mux_all;
	pthread_cond_t cond;
	pthread_mutex_t mux;
};

struct loop_thread_opaq_t
{
	/* TS loopback thread */
	pthread_t loop_thread;
	pthread_cond_t cond;
	pthread_mutex_t mux;
	int cancel;
};

/* init pool */
int pool_init(struct joker_t *joker, struct big_pool_t * pool)
{
	if (!joker || !pool)
		return -EINVAL;

	pool->joker = joker;
	joker->pool = pool;
	pool->node_counter = 0;
	pool->tail_size = 0;
	pool->ts_list_size = 0;
	if (pool->ts_list_size_max <= TS_LIST_SIZE_DEFAULT)
		pool->ts_list_size_max = TS_LIST_SIZE_DEFAULT;

	INIT_LIST_HEAD(&pool->ts_list);
	INIT_LIST_HEAD(&pool->ts_list_all);
	INIT_LIST_HEAD(&pool->programs_list);
	INIT_LIST_HEAD(&pool->ca_list);

	if (!pool->selected_programs_list.next)
		INIT_LIST_HEAD(&pool->selected_programs_list);

	// alloc threading stuff
	pool->threading = malloc(sizeof(struct thread_opaq_t));
	if (!pool->threading)
		return -ENOMEM;

	pthread_mutex_init(&pool->threading->mux_all, NULL);
	pthread_mutex_init(&pool->threading->mux, NULL);
	pthread_cond_init(&pool->threading->cond_all, NULL);
	pthread_cond_init(&pool->threading->cond, NULL);

	memset(&pool->hooks, 0, sizeof(pool->hooks));
	memset(&pool->hooks_opaque, 0, sizeof(pool->hooks_opaque));
	memset(&pool->transfers, 0, sizeof(pool->transfers));

	pool->initialized = BIG_POOL_MAGIC;

	return 0;
}

int pool_uninit(struct big_pool_t * pool)
{
	// sanity check
	if (pool && pool->initialized != BIG_POOL_MAGIC)
		return -EINVAL;

	// TODO: clean programs_list, ts_list*

	free(pool->threading);
	pool->threading = NULL;
	pool->initialized = 0;
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
		pthread_mutex_lock(&pool->threading->mux);
		if(list_empty(&pool->ts_list))
			pthread_cond_wait(&pool->threading->cond, &pool->threading->mux);
		if(!list_empty(&pool->ts_list)) {
			node = list_first_entry(&pool->ts_list, struct ts_node, list);
			list_del(&node->list);
		} else {
			node = NULL;
		}
		pthread_mutex_unlock(&pool->threading->mux);

		if (!node)
			continue;

		// process hooks
		for (i = 0; i < node->size; i += TS_SIZE) {
			pkt = node->data + i;
			pid = (pkt[1]&0x1f) << 8 | pkt[2];

			if(pool->hooks[pid]) {
				jdebug("calling hook pid=0x%x pool=%p pkt=%p\n", pid, pool, pkt);
				pool->hooks[pid]( pool->hooks_opaque [ pid ] ?
						pool->hooks_opaque[pid] : pool, pkt);
			}
		}

		// save node to list 
		pthread_mutex_lock(&pool->threading->mux_all);
		list_add_tail(&node->list, &pool->ts_list_all);
		pool->ts_list_size += node->size;

		// keep list in desired memory limit (remove old nodes)
		jdebug("TS:all: ts_list_size=%d\n", pool->ts_list_size);
		while (pool->ts_list_size > pool->ts_list_size_max && !list_empty(&pool->ts_list_all)) {
			node = list_first_entry(&pool->ts_list_all, struct ts_node, list);
			pool->ts_list_size -= node->size;
			printf("Memory limit: dropping TS node %p. ts_list_size=%d\n", node, pool->ts_list_size);
			drop_ts_data(node);
		}

		jdebug("TS:all: node %p inserted. ts_list_size=%d\n", node, pool->ts_list_size);
		pthread_mutex_unlock(&pool->threading->mux_all);
		pthread_cond_signal(&pool->threading->cond_all); // wakeup read threads
	}
}

/* thread for 'kicking' libusb polling 
 * TODO: not only default libusb context polling
 * */
void* process_usb(void * data) {
	int completed = 0;
	struct big_pool_t * pool = (struct big_pool_t *)data;

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
	struct joker_t *joker = NULL;
        
	// free this transfer
	if (!transfer->user_data) {
		jdebug("%s: no user data\n", __func__);
		transfer->flags |= LIBUSB_TRANSFER_FREE_BUFFER;
		libusb_free_transfer(transfer);
		return;
	}
    joker = pool->joker;

	// looks like we stopping TS processing. do not submit this transfer
	if(transfer->status == LIBUSB_TRANSFER_CANCELLED) {
		printf("%s: LIBUSB_TRANSFER_CANCELLED\n", __func__);
		return;
	}

	if(transfer->status == LIBUSB_TRANSFER_ERROR) {
		printf("%s: LIBUSB_TRANSFER_ERROR\n", __func__);
		return;
	}

	/* update statistics */
	if ( (getus() - pool->start_time) > 2000000 ) {
		printf("USB ISOC: all/complete=%f/%f transfer/sec %.2f MBytes %f mbits/sec, %f calls/sec\n", 
				(double)((int64_t)1000000*pool->pkt_count)/(getus() - pool->start_time),
				(double)((int64_t)1000000*pool->pkt_count_complete)/(getus() - pool->start_time),
				(double)pool->bytes/1024/1024,
				(double)((int64_t)1000000*8*pool->bytes/1048576)/(getus() - pool->start_time),
				(double)((int64_t)1000000*pool->calls_count)/(getus() - pool->start_time)
				);
		fflush(stdout);
		pool->calls_count = 0;
		pool->pkt_count = 0;
		pool->pkt_count_complete = 0;
		pool->bytes = 0;
		pool->start_time = getus();
	}
	pool->calls_count++;

	// fill TS list with received data
	for(i = 0; i < transfer->num_iso_packets; i++) {
		pkt = transfer->iso_packet_desc[i];
		if (pkt.status == LIBUSB_TRANSFER_COMPLETED)
			total_len += transfer->iso_packet_desc[i].actual_length;
	}

	node = malloc(sizeof(*node));
	if(!node) {
		printf("%s: can't alloc mem for node \n", __func__);
		return;
	}
	memset(node, 0, sizeof(*node));

	node->data = malloc(total_len + TS_SIZE);
	if (!node->data) {
		printf("%s: can't alloc mem for data \n", __func__);
		return;
	}

	node->size = 0;
	node->counter = pool->node_counter++;

	// traversal of ISOC packets and copy data to TS list
	// data may be not aligned to TS_SIZE so we use "tail"
	for(i = 0; i < transfer->num_iso_packets; i++) {
		pkt = transfer->iso_packet_desc[i];
		len = transfer->iso_packet_desc[i].actual_length;
		jdebug("ISO pkt length=%d actual_length=%d status=0x%x\n",
				transfer->iso_packet_desc[i].length,
				transfer->iso_packet_desc[i].actual_length, pkt.status);
		pool->pkt_count++;

		if (pkt.status == LIBUSB_TRANSFER_COMPLETED && len > 0) {
			pool->pkt_count_complete++;
			pool->bytes += len;
			if ((buf = libusb_get_iso_packet_buffer(transfer, i))) {
				if (joker->raw_data_filename_fd > 0)
					fwrite(buf, len, 1, joker->raw_data_filename_fd);

				if (buf[TS_SIZE - pool->tail_size] == TS_SYNC)
					ts_off = TS_SIZE - pool->tail_size; // tail is ok. use it
				else
					ts_off = next_ts_off(buf, len);
				jdebug("	foff=%lld ts_off=%d tail_size=%d len=%d\n",
						ftell(joker->raw_data_filename_fd),
						ts_off, pool->tail_size, len);
				if (ts_off < 0)
					continue;

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
	pthread_mutex_lock(&pool->threading->mux);
	list_add_tail(&node->list, &pool->ts_list);
	pthread_mutex_unlock(&pool->threading->mux);
	pthread_cond_signal(&pool->threading->cond); // wakeup ts procesing thread
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
	unsigned char buf[JCMD_BUF_LEN];
	int allocated = 0, max_isoc_packets_count_avail = 0;

	if (!joker || !pool)
		return EINVAL;

	dev = (struct libusb_device_handle *)joker->libusb_opaque;
	if (!dev)
		return EINVAL;

	// sanity check
	if (pool->initialized != BIG_POOL_MAGIC) {
		if (pool_init(joker, pool)) {
            printf("Can't init pool ! Stop TS processing ... \n");
            return EINVAL;
        }
    }

#ifdef __WIN32__
	// USB isoch packets can lost under Windows if we do not increase priority
	if(!SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS)) {
		printf("Can't set high priority for current process error=%d \n", GetLastError());
		printf("USB isoc packets can be lost \n");
	} else {
		printf("High priority was set \n");
	}
#endif
	
	joker_clean_ts(joker); // clean FIFO from previous TS

	// enable TS PID filtering if programs specified
	// here we enable only service PID's
	// PID's related to program (PMT, Video, Audio, etc) will be enabled later
	if (!list_empty(&pool->selected_programs_list)) {
		ts_filter_only_service_pids(joker);
	} else {
		// unblock all PID's if no programs selected
		ts_filter_all(joker, TS_FILTER_UNBLOCK); // receive full TS
	}

	// disable TS traffic through CAM
	buf[0] = J_CMD_CI_TS;
	buf[1] = 0; // disable here. will be enabled later
	if ((ret = joker_cmd(joker, buf, 2, NULL /* in_buf */, 0 /* in_len */)))
		return ret;
	
	// create isochronous transfers
	// USB isoc transfer (DATA_IN token) should be delivered to Joker TV 
	// every microframe (125usec)
	// One isoc transfer size is 1024 bytes (max 1024)
	// Hight bandwidth isoc transfer can support up to 3 DATA token's in one microframe
	for (index = 0; index < NUM_USB_BUFS; index++) {
		// iterate number of isoc packets to find maximum allowed on this system
		// usually it depends on OS available contig. memory
		allocated = 0;
		max_isoc_packets_count_avail = joker->max_isoc_packets_count;
		while (max_isoc_packets_count_avail > 4 && !allocated) {
			pool->usb_buffers[index] = (uint8_t*)malloc(max_isoc_packets_count_avail * joker->max_isoc_packets_size);
			memset(pool->usb_buffers[index], 0, max_isoc_packets_count_avail * joker->max_isoc_packets_size);

			pool->transfers[index] = libusb_alloc_transfer(max_isoc_packets_count_avail);    
			libusb_fill_iso_transfer(pool->transfers[index], dev, USB_EP3_IN,
					pool->usb_buffers[index], max_isoc_packets_count_avail * joker->max_isoc_packets_size,
					max_isoc_packets_count_avail, cb, (void *)pool, 1000);
			libusb_set_iso_packet_lengths(pool->transfers[index], joker->max_isoc_packets_size);

			ret = libusb_submit_transfer(pool->transfers[index]);
			if (ret) {
				libusb_free_transfer(pool->transfers[index]);
				pool->transfers[index] = NULL;
				// try lower 
				jdebug("%d packets not available. Will try lower \n", 
						max_isoc_packets_count_avail);
				max_isoc_packets_count_avail = max_isoc_packets_count_avail/2;
				continue;
			}
			jdebug("%d packets available. usb transfer %d (%p) done\n",
					max_isoc_packets_count_avail,
					index, pool->transfers[index]);
			allocated = 1;
		}
	}
	
	// start ISOC USB transfers processing thread
	pool->calls_count = 0;
	pool->pkt_count = 0;
	pool->pkt_count_complete = 0;
	pool->start_time = getus();
	pool->bytes = 0;
	pool->cancel = 0;

	rc = pthread_create(&pool->threading->usb_thread, NULL, process_usb, (void *)pool);
	if (rc){
		printf("ERROR: can't start USB processing thread. code=%d\n", rc);
		return rc;
	}

	// start TS processing thread
	rc = pthread_create(&pool->threading->ts_thread, NULL, process_ts, (void *)pool);
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
	int ret = 0;
	struct ts_node * node = NULL;

	// sanity check
	if (pool->initialized != BIG_POOL_MAGIC || !joker || pool->cancel)
		return -EINVAL;

	set_refresh(joker, 0);

	for (index = 0; index < NUM_USB_BUFS; index++) {
		// signal callback to free this transfer. We can't clean it here
		// because this cancel is async and transaction actually cancelled later
		if(pool->transfers[index])
			pool->transfers[index]->user_data = NULL;

		if(pool->transfers[index] && libusb_cancel_transfer(pool->transfers[index]))
			printf("can't cancel usb transfer %d (%p) \n", index, pool->transfers[index]);
		else
			jdebug("cancel usb transfer %d (%p) \n", index, pool->transfers[index]);
		pool->transfers[index] = NULL;
	}

	// stop USB and TS processing threads
	pool->cancel = 1;
	pthread_cond_signal(&pool->threading->cond); // wakeup TS procesing thread

	// lock until threads ended
	pthread_join(pool->threading->usb_thread, NULL);
	pthread_join(pool->threading->ts_thread, NULL);

	if ((ret = libusb_release_interface((struct libusb_device_handle *)joker->libusb_opaque, 0))) {
		printf("%s: can't release USB interface ! \n", __func__ );
		return -EIO;
	}

	if ((ret = libusb_claim_interface((struct libusb_device_handle *)joker->libusb_opaque, 0))) {
		printf("%s: can't claim USB interface ! \n", __func__ );
		return -EIO;
	}

	// cleanup collected TS data
	while (!list_empty(&pool->ts_list_all)) {
		node = list_first_entry(&pool->ts_list_all, struct ts_node, list);
		pool->ts_list_size -= node->size;
		drop_ts_data(node);
		jdebug("TS:all: drop node %p. ts_list_size=%d\n", node, pool->ts_list_size);
	}

	pool_uninit(pool);

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

int replace_pat(struct big_pool_t *pool, unsigned char *data, int size)
{
	int off = 0;
	int pid = 0;
	char *pkt = NULL;
	char *pat = NULL;
	char *sdt = NULL;

	if(!pool)
		return -EINVAL;
	
	pat = pool->generated_pat_pkt;

	jdebug("PKT: size:%d \n", size);
	// data already aligned to TS packets
	for (off = 0; off <= (size - TS_SIZE); ) {
		if (data[off] == TS_SYNC) {
			pid = ((int)data[off+1]&0x1f)<<8 | data[off+2];
			jdebug(" PKT: off=%d pid=0x%x \n", off, pid);
			if (pid == 0x0 /* PAT */) {
				if (pool->pat_counter == 0x10)
					pool->pat_counter = 0;
				pat[3] = (pat[3]&0xf0) | (pool->pat_counter&0x0f);
				pool->pat_counter++;
				memcpy(&data[off], pat, TS_SIZE);
				jdebug("	PAT replaced. off=%d pid=0x%x\n", off, pid);
			} else if (pid == 0x11 /* SDT */) {
				// current hackish SDT generator disabled
				// all SDT will be replaced to null packets (hack !)
				// no SDT info in output stream !
				// TODO: rework SDT generator ! 
				sdt = get_next_sdt(pool);
				if (!sdt) {
					// hack: replace SDT to null packet !
					pkt = &data[off];
					pkt[0] = 0x47;
					pkt[1] = 0x1F;
					pkt[2] = 0xFF;
					pkt[3] = 0;
					off += TS_SIZE;
					continue;
				}

				if (pool->sdt_counter == 0x10)
					pool->sdt_counter = 0;
				sdt[3] = (sdt[3]&0xf0) | (pool->sdt_counter&0x0f);
				pool->sdt_counter++;
				memcpy(&data[off], sdt, TS_SIZE);
				printf("	SDT replaced. off=%d pid=0x%x\n", off, pid);
			}

			off += TS_SIZE;
		} else {
			off++;
		}
	}

	return 0;
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
		pthread_mutex_lock(&pool->threading->mux_all);
		if(list_empty(&pool->ts_list_all))
			pthread_cond_wait(&pool->threading->cond_all, &pool->threading->mux_all);
		
		if(!list_empty(&pool->ts_list_all)) {
			jdebug("req:%d \n", size);
			list_for_each_entry_safe(node, tmp, &pool->ts_list_all, list)
			{
				if (pool->generated_pat_pkt && !node->pat_replaced) {
					// replace PAT to our own
					// this hackish way should be refactored (regenerate TS ?)
					replace_pat(pool, node->data, node->size);
					node->pat_replaced = 1;
				} else {
					jdebug("Do not replace PAT \n");
				}

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
		pthread_mutex_unlock(&pool->threading->mux_all);
	}


	return res_off;
}

/* TS loopback thread */
void* process_ts_loop(void * data) {
	struct joker_t * joker = (struct joker_t *)data;
	int rc = 0;
	FILE *fd = NULL;
	int nbytes = 0;
	unsigned char buf[TS_LOOP_SIZE];
	int len = 0, ret = 0, count = 0;
	long long total = 0;
	time_t t0 = time(0);
	int print_interval = 5;

	if (!joker || !joker->loop_ts_filename) {
		printf("%s: invalid args \n", __func__ );
		return (void *)-EINVAL;
	}

	fd = fopen((char*)joker->loop_ts_filename, "r+b");
	if (!fd) {
		printf("TS loop file: %s \n", joker->loop_ts_filename);
		perror("Can't open TS loop file:");
		return (void *)-EIO;
	}

	printf("%s: %s opened \n", __func__, joker->loop_ts_filename);
	memset(buf, 0, TS_LOOP_SIZE);
	len = TS_LOOP_SIZE; // actual TS data len
	while(!joker->loop_threading->cancel) {
		if ((nbytes = fread(buf, 1, len, fd)) <= 0) {
			printf("TS loop: TS file processing done\n");
			return (void *)-EIO;
		}

		jdebug("%s: send %d bytes \n", __func__, nbytes);
		if ((ret = joker_send_ts_loop(joker, buf, nbytes))) {
			printf("%s: can't send %d bytes \n", __func__, nbytes);
			return (void *)-EIO;
		}
		total += nbytes;
		count++;
		if ((time(0) - t0) > print_interval) {
			printf("TS loop: %.2f MBytes sent\n", (float)total/1048576);
			t0 = time(0);
		}
	}

	return 0;
}

/* start TS loop thread 
 * loop TS traffic 
 * send to Joker TV over USB (EP2 OUT)
 * receive from Joker TV over USB (EP1 IN)
 */
int start_ts_loop(struct joker_t *joker)
{
	int rc = 0;

	if (!joker)
		return EINVAL;

	/* start loop thread */
	if (!joker->loop_threading) {
		joker->loop_threading = malloc(sizeof(struct loop_thread_opaq_t));
		memset(joker->loop_threading, 0, sizeof(struct loop_thread_opaq_t));
		pthread_mutex_init(&joker->loop_threading->mux, NULL);
		pthread_cond_init(&joker->loop_threading->cond, NULL);

		pthread_attr_t attrs;
		pthread_attr_init(&attrs);
		pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_JOINABLE);
		rc = pthread_create(&joker->loop_threading->loop_thread, &attrs, process_ts_loop, (void *)joker);
		if (rc){
			printf("ERROR: can't start TS loop thread. code=%d\n", rc);
			return rc;
		}
	}
	return 0;
}

// fast stop of loop thread
int stop_ts_loop(struct joker_t * joker)
{
	int ret = 0;

	if (!joker || !joker->loop_threading)
		return -EINVAL;

	// fast stop of service thread
	joker->loop_threading->cancel = 1;
	pthread_cond_signal(&joker->loop_threading->cond);
	ret = pthread_join(joker->loop_threading->loop_thread, NULL);

	free(joker->loop_threading);
	joker->loop_threading = NULL;
}
