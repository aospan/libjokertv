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

/* helper func 
 * get current time in usec */
uint64_t getus() {
	struct timeval tv;
	gettimeofday(&tv,NULL);
	return tv.tv_sec*(uint64_t)1000000+tv.tv_usec;
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

	/* update statistics */
	if ( !(pool->pkt_count%1000) ){
		printf("USB ISOC: all/complete=%f/%f transfer/sec %f mbits/sec \n", 
				(double)((int64_t)1000000*pool->pkt_count)/(getus() - pool->start_time),
				(double)((int64_t)1000000*pool->pkt_count_complete)/(getus() - pool->start_time),
				(double)((int64_t)1000000*8*pool->bytes/1048576)/(getus() - pool->start_time));
		pool->pkt_count = 0;
		pool->pkt_count_complete = 0;
		pool->bytes = 0;
		pool->start_time = getus();
	}

	jdebug("NUM num_iso_packets=%d \n", transfer->num_iso_packets);
	// copy data and 
	// return USB ISOC ASAP (can't wait here) !
	// otherwise we loose ISOC synchronization and get buffer overrun on device !
	for(i = 0; i < transfer->num_iso_packets; i++) {
		pkt = transfer->iso_packet_desc[i];
		pool->pkt_count++;

		if (pkt.status == LIBUSB_TRANSFER_COMPLETED) {
			pool->pkt_count_complete++;
			jdebug("ISOC size=%d \n", transfer->iso_packet_desc[i].actual_length );
			if (buf = libusb_get_iso_packet_buffer(transfer, i)) {
				pool->bytes += transfer->iso_packet_desc[i].actual_length;
				remain = transfer->iso_packet_desc[i].actual_length;
				while(remain > 0) {
					pthread_mutex_lock(&pool->mux);
					to_write = ((pool->write_ptr + remain) > pool->ptr_end) ? (pool->ptr_end - pool->write_ptr) : remain;
					memcpy(pool->write_ptr, buf, to_write);
					pool->write_ptr += to_write;
					if(pool->write_ptr >= pool->ptr_end) {
						// rollover ring buffer
						pool->write_ptr = pool->ptr;
					}
					remain -= to_write;
					pthread_mutex_unlock(&pool->mux);
				}
				pthread_cond_signal(&pool->cond); // wake processing thread
			}
		} else {
			jdebug("ISOC NOT COMPLETE. pkt.status=0x%x\n", pkt.status);
		}
	}

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

	/* Allocate big pool memory 
	 * TS will be stored here
	 * TODO: dynamic allocation
	 * */
	pool->size = NUM_USB_BUFS * NUM_USB_PACKETS * USB_PACKET_SIZE * BIG_POOL_GAIN;
	pool->ptr = (unsigned char*)malloc(pool->size);
	if (!pool->ptr) {
		printf("ERROR:can't alloc memory to big pool \n");
		return ENOMEM;
	}
	pool->ptr_end = pool->ptr + pool->size;
	pool->read_ptr = pool->ptr;
	pool->write_ptr = pool->ptr;

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
	rc = pthread_create(&pool->thread, NULL, process_usb, (void *)&pool);
	if (rc){
		printf("ERROR; return code from pthread_create() is %d\n", rc);
		return rc;
	}
}

/* stop ts processing 
 * TODO*/
int stop_ts(struct joker_t *joker, struct big_pool_t * pool)
{
	return 0;
}

/* read TS into buf
 * maximum available space in size bytes.
 * return read bytes or negative error code if failed
 * */
ssize_t read_data(struct joker_t *joker, struct big_pool_t * pool, unsigned char *buf, size_t size)
{
	int remain = size;
	int to_write = 0, avail = 0;
	unsigned char * ptr = buf;

	while (remain > 0) {
		// just wait more data
		pthread_mutex_lock(&pool->mux);
		if (pool->read_ptr == pool->write_ptr) // no new data in ring
			pthread_cond_wait(&pool->cond, &pool->mux);  // just wait

		pthread_mutex_unlock(&pool->mux);

		// process ring data
		// without locking for faster USB ISOC release
		// this can cause data loss/overwrite if we are too slow
		// and write_ptr catch read_ptr !
		if (pool->read_ptr != pool->write_ptr) {
			/* some data in ringbuffer detected */
			if (pool->read_ptr < pool->write_ptr) {
				avail = pool->write_ptr - pool->read_ptr;
				to_write = (avail > remain) ? remain : avail;
				memcpy(ptr, pool->read_ptr, to_write);
				/* update pointers/sizes */
				ptr += to_write;
				pool->read_ptr += to_write;
				remain -= to_write;
			} else if (pool->read_ptr > pool->write_ptr) {
				/* ring rollover detected */
				avail = pool->ptr_end - pool->read_ptr;
				to_write = (avail > remain) ? remain : avail;
				memcpy(ptr, pool->read_ptr, to_write);
				ptr += to_write;
				if (to_write == avail)
					pool->read_ptr = pool->ptr; /* ring end reached */
				else
					pool->read_ptr += to_write;
				remain -= to_write;
			}
		}
	}
}
