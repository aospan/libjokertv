/* 
 * https://jokersys.com
 * (c) Abylay Ospan, 2017
 * aospan@jokersys.com
 * GPLv2
 */

#ifndef _U_DRV_DATA
#define _U_DRV_DATA	1

#define NUM_USB_BUFS 16
// under CentOS 5.5 limit for URB size (?)
// so, choose 64 here (original was 128)
#define NUM_USB_PACKETS 64
#define USB_PACKET_SIZE 1024

#define BIG_POOL_GAIN	16

/* ring buffer for TS data */
struct big_pool_t {
	unsigned char * ptr;
	unsigned char * ptr_end;
	unsigned char * read_ptr;
	unsigned char * write_ptr;
	int size;
  
  uint8_t *usb_buffers[NUM_USB_BUFS];

  /* threads stuff */
  pthread_t thread;
  pthread_cond_t cond;
  pthread_mutex_t mux;

  /* statistics */
  int pkt_count;
  int pkt_count_complete;
  int bytes;
  uint64_t start_time;
};

#ifdef __cplusplus
extern "C" {
#endif

/* start TS processing thread 
 */
int start_ts(struct joker_t *joker, struct big_pool_t *pool);

/* stop ts processing */
int stop_ts(struct joker_t *joker, struct big_pool_t * pool);

/* read TS into buf
 * maximum available space in size bytes.
 * return read bytes or negative error code if failed
 * */
ssize_t read_data(struct joker_t *joker, struct big_pool_t * pool, unsigned char *buf, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* end */
