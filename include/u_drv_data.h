/* 
 * https://jokersys.com
 * (c) Abylay Ospan, 2017
 * aospan@jokersys.com
 * GPLv2
 */

#ifndef _U_DRV_DATA
#define _U_DRV_DATA	1

#include "joker_list.h"

#define NUM_USB_BUFS 16
// under CentOS 5.5 limit for URB size (?)
// so, choose 64 here (original was 128) for CentOS 5.5
#define NUM_USB_PACKETS 128
#define USB_PACKET_SIZE 1024

#define BIG_POOL_GAIN	16

struct ts_node {
	int counter;
	unsigned char * data;
	int size;
	struct list_head list;
};

/* ring buffer for TS data */
struct big_pool_t {
	unsigned char * ptr;
	unsigned char * ptr_end;
	unsigned char * read_ptr;
	unsigned char * write_ptr;
	int size;
	int node_counter;

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

	/* TS list */
	struct list_head ts_list;
	int tail_size;
	unsigned char tail[TS_SIZE];

	/* PSI related stuff */
	struct list_head programs_list;
};

#ifdef __cplusplus
extern "C" {
#endif

/* start TS processing thread 
 */
int start_ts(struct joker_t *joker, struct big_pool_t *pool);

/* stop ts processing */
int stop_ts(struct joker_t *joker, struct big_pool_t * pool);

int next_ts_off(unsigned char *buf, size_t size);
struct ts_node * read_ts_data(struct big_pool_t * pool);
void drop_ts_data(struct ts_node * node);
int read_ts_data_pid(struct big_pool_t *pool, int pid, unsigned char *data);

#ifdef __cplusplus
}
#endif

#endif /* end */
