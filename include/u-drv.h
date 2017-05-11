/* 
 * https://jokersys.com
 * (c) Abylay Ospan, 2017
 * aospan@jokersys.com
 */

#ifndef _U_DRV
#define _U_DRV	1

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
	// pthread_mutex_lock mux;
	// pthread_cond_t cond;

	/* callback for libusb */
	void * cb;
};

#ifdef __cplusplus
extern "C" {
#endif

int u_drv_main (struct big_pool * bp);

#ifdef __cplusplus
}
#endif

#endif /* end */
