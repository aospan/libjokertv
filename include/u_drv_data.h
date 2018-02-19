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
// Linux kernel (drivers/usb/core/devio.c) has limit of 128 iso packets at once
#define NUM_USB_PACKETS 128
#define NUM_USB_PACKETS_HIGH_BW_ISOC 128
#define USB_PACKET_SIZE 1024
#define USB_PACKET_SIZE_HIGH_BW_ISOC 3072
#define ISOC_TRANSFER_SIZE 1024

#define BIG_POOL_GAIN	16

// Max size (in bytes) for TS storage (list)
#define TS_LIST_SIZE_DEFAULT 1024*1024*128

// Maximum size for TS loopback
#define TS_LOOP_SIZE 16384

// hook function
struct big_pool_t;
struct program_t;
typedef void(*ts_hook_t)(void *opaque, unsigned char *pkt);
typedef void(*service_name_callback_t)(struct program_t *program);

#ifdef __cplusplus
extern "C" {
#endif

struct ts_node {
	int counter;
	unsigned char * data;
	int size;
	int read_off;
	int pat_replaced;
	struct list_head list;
};

#define BIG_POOL_MAGIC 0xbb0000aa

/* threading stuff "masked" inside */
struct thread_opaq_t;

/* ring buffer for TS data */
struct big_pool_t {
	unsigned char * ptr;
	unsigned char * ptr_end;
	unsigned char * read_ptr;
	unsigned char * write_ptr;
	int size;
	int node_counter;

	uint8_t *usb_buffers[NUM_USB_BUFS];
	struct libusb_transfer *transfers[NUM_USB_BUFS];

	/* threading stuff "masked" inside */
	struct thread_opaq_t *threading;

	/* hooks */
	ts_hook_t hooks[8192];
	void * hooks_opaque[8192];

	/* statistics */
	int calls_count;
	int pkt_count;
	int pkt_count_complete;
	int bytes;
	uint64_t start_time;

	/* TS list */
	struct list_head ts_list;
	struct list_head ts_list_all;
	int tail_size;
	unsigned char tail[TS_SIZE];
	int cancel;
	int ts_list_size;
	int ts_list_size_max;

	/* PSI related stuff */
	struct list_head selected_programs_list;
	struct list_head programs_list;
	service_name_callback_t service_name_callback;
	void *pat_dvbpsi;
	void *sdt_dvbpsi;
	void *atsc_dvbpsi;
	char *generated_pat;
	char *generated_pat_pkt;
	uint8_t pat_counter;

	// SDT
	char *sdt_pkt_array;
	int sdt_count;
	int cur_sdt;
	uint8_t sdt_counter;

	uint32_t initialized;
	struct joker_t *joker;
};

/* init pool */
int pool_init(struct joker_t *joker, struct big_pool_t * pool);

/* start TS processing thread 
 */
int start_ts(struct joker_t *joker, struct big_pool_t *pool);

/* stop ts processing */
int stop_ts(struct joker_t *joker, struct big_pool_t * pool);

int next_ts_off(unsigned char *buf, size_t size);
void drop_ts_data(struct ts_node * node);

/* read TS data
 * data - output buffer for data. should be allocated by caller and at least
 * size bytes long
 * size - maximum available space in data
 *
 * return - copied bytes into data (can be less than requested size or zero if
 * no data available) */
int read_ts_data(struct big_pool_t *pool, unsigned char *data, int size);

/* start TS loop thread 
 * loop TS traffic
 * send to Joker TV over USB (EP4 OUT, bulk)
 * receive from Joker TV over USB (EP3 IN, isoch)
 *
 * TS traffic can'be routed through CAM module as well
 */
int start_ts_loop(struct joker_t *joker);
int stop_ts_loop(struct joker_t *joker);

#ifdef __cplusplus
}
#endif

#endif /* end */
