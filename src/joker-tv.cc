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
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <libusb.h>
#include <pthread.h>

#include <queue>
#include "u-drv.h"

static int count = 0;
static time_t start = 0, start_tr = 0;
static int64_t delta = 0;
static int64_t total = 0;
static int stop = 0;

static pthread_cond_t cond;
static pthread_mutex_t mux;
static std::queue<struct libusb_transfer *> tsq;

uint64_t getus() {
	struct timeval tv;
	gettimeofday(&tv,NULL);
	return tv.tv_sec*(uint64_t)1000000+tv.tv_usec;
}

void* process_tr(void * data) {
	struct libusb_transfer *transfer = NULL;
	struct big_pool * bp = (struct big_pool *)data;
	unsigned char * buf = 0;
	int i = 0;
	struct libusb_iso_packet_descriptor pkt;
	time_t start_cb = getus();
	int to_write = 0;

	FILE * out = fopen("out.ts", "w+");
	if (!out){
		fprintf(stderr, "Can't open out file \n");
		pthread_exit(NULL);
	}

	while(!stop) {
		// just wait more data
		pthread_mutex_lock(&mux);
		if (bp->read_ptr == bp->write_ptr) // no new data in ring
			pthread_cond_wait(&cond, &mux);  // just wait

		pthread_mutex_unlock(&mux);

		// process ring data
		// without locking for faster USB ISOC release
		// this can cause data loss/overwrite if we are too slow
		// and write_ptr catch read_ptr !
		if (bp->read_ptr != bp->write_ptr) {
			/* some data in ringbuffer detected */
			if (bp->read_ptr < bp->write_ptr) {
				to_write = bp->write_ptr - bp->read_ptr;
				fwrite(bp->read_ptr, to_write, 1, out);
				bp->read_ptr += to_write;
			} else if (bp->read_ptr > bp->write_ptr) {
				/* ring rollover detected */
				to_write = bp->ptr_end - bp->read_ptr;
				fwrite(bp->read_ptr, to_write, 1, out);
				bp->read_ptr = bp->ptr;
				to_write = bp->write_ptr - bp->read_ptr;
				fwrite(bp->read_ptr, to_write, 1, out);
				bp->read_ptr += to_write;
			}
		}
	}
}

void record_callback(struct libusb_transfer *transfer)
{
    int i;
    struct libusb_iso_packet_descriptor pkt;
    time_t start_cb = getus();
    unsigned char * buf = 0;
    struct big_pool * bp = (struct big_pool *)transfer->user_data;
    int remain = 0, to_write = 0;

    count++;
    if ( !(count%500) ){
	    printf("USB ISOC count=%d transfer/sec=%f \n", 
			    count, (double)((int64_t)1000000*count)/(getus() - start));
	    count = 0;
	    start = getus();
    }

    // copy data and 
    // return USB ISOC ASAP (can't wait here) !
    // otherwise we loose ISOC synchronization and get buffer overrun on device !
    for(i = 0; i < transfer->num_iso_packets; i++) {
	    pkt = transfer->iso_packet_desc[i];

	    if (pkt.status == LIBUSB_TRANSFER_COMPLETED) {
		    if (buf = libusb_get_iso_packet_buffer(transfer, i)) {
			    total += transfer->iso_packet_desc[i].actual_length;
			    remain = transfer->iso_packet_desc[i].actual_length;
			    while(remain > 0) {
				    pthread_mutex_lock(&mux);
				    to_write = ((bp->write_ptr + remain) > bp->ptr_end) ? (bp->ptr_end - bp->write_ptr) : remain;
				    memcpy(bp->write_ptr, buf, to_write);
				    bp->write_ptr += to_write;
				    if(bp->write_ptr >= bp->ptr_end) {
					    // rollover ring buffer
					    bp->write_ptr = bp->ptr;
				    }
				    remain -= to_write;
				    pthread_mutex_unlock(&mux);
			    }
			    pthread_cond_signal(&cond); // wake processing thread
		    }
	    }
    }

    // return USB ISOC ASAP !
    if (libusb_submit_transfer(transfer)) {
	    printf("CALLBACK: ERROR: libusb_submit_transfer failed\n");
	    exit(1);
    }
}

int main ()
{
	int rc = 0;
	pthread_t thread;
	struct big_pool bp;

	bp.size = NUM_RECORD_BUFS * NUM_REC_PACKETS * REC_PACKET_SIZE * BIG_POOL_GAIN;
	bp.ptr = (unsigned char*)malloc(bp.size);
	if (!bp.ptr) {
		printf("ERROR:can't alloc memory to big pool \n");
		return -1;
	}
	bp.ptr_end = bp.ptr + bp.size;
	bp.read_ptr = bp.ptr;
	bp.write_ptr = bp.ptr;
	bp.cb = (void*)record_callback;

	rc = pthread_create(&thread, NULL, process_tr, (void *)&bp);
	if (rc){
		printf("ERROR; return code from pthread_create() is %d\n", rc);
		exit(-1);
	}

	u_drv_main(&bp);
}
