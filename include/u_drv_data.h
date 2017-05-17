/* 
 * https://jokersys.com
 * (c) Abylay Ospan, 2017
 * aospan@jokersys.com
 * GPLv2
 */

#ifndef _U_DRV_DATA
#define _U_DRV_DATA	1

#define NUM_RECORD_BUFS 16
#define NUM_REC_PACKETS 128
#define REC_PACKET_SIZE 512

#define BIG_POOL_GAIN	16

/* ring buffer for TS data */
struct big_pool {
	unsigned char * ptr;
	unsigned char * ptr_end;
	unsigned char * read_ptr;
	unsigned char * write_ptr;
	int size;
  
	/* callback for libusb */
	// void * cb;
};

#ifdef __cplusplus
extern "C" {
#endif

/* start TS processing thread 
 */
struct big_pool * start_ts();

/* stop ts processing */
int stop_ts(struct big_pool * bp);

/* read TS into buf
 * maximum available space in size bytes.
 * opaque returned by start_ts
 * return read bytes or negative error code if failed
 * */
ssize_t read_data(void * opaque, unsigned char *buf, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* end */
