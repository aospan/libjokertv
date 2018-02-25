/* 
 * Access to Joker TV
 * https://jokersys.com
 * (c) Abylay Ospan, 2017
 * aospan@jokersys.com
 * GPLv2
 */

#include <stdio.h>
#include <stdint.h>

#ifndef _JOKER_TV
#define _JOKER_TV 1

/* TODO: debug system */
// #define DBG
#ifdef DBG
#define jdebug(...) printf(__VA_ARGS__);
#else
#define jdebug(...) {};
#endif

/* constants */
#define FNAME_LEN		512
#define TS_SIZE			188
// should be more than 128KB
#define TS_BUF_MAX_SIZE		TS_SIZE*700
#define TS_SYNC			0x47
#define TS_WILDCARD_PID		0x2000

typedef void(*status_callback_t)(void *data);
typedef void(*ci_callback_t)(void *data);
typedef void(*mmi_callback_t)(void *data, unsigned char *buf, int len);
typedef void(*blind_scan_callback_t)(void *data);
struct tune_info_t;

#define JOKER_LOCK 0
#define JOKER_NOLOCK 11

#define SIGNAL_BAD 0
#define SIGNAL_WEAK 1
#define SIGNAL_GOOD 2


/* struct used to periodic status checking */
struct stat_t {
	// struct joker_t *joker;
	// struct tune_info_t *info;
	int32_t cancel;

	/* signal monitoring */

	/* enable status refresh */
	int refresh_enable;

	/* status refresh interval in ms */
	int refresh_ms;

	/* LOCK status: JOKER_LOCK/JOKER_NOLOCK */
	int status;

	/* RF level
	 * RF level given in dBm*1000 
	 * signed value
	 */
	int32_t rf_level;

	/* SNR or CNR 
	 * given in dB*1000 */
	int32_t snr;

	/* uncorrected blocks
	 * can happen if signal is weak or noisy
	 */
	int32_t ucblocks;

	/* Signal quality
	 * SIGNAL_BAD - 'bad' or no signal
	 * SIGNAL_WEAK - 'weak'
	 * SIGNAL_GOOD - 'good'
	 */
	int32_t signal_quality;

	/* BER (bit error rate)
	 *
	 * calculate BER as:
	 * BER = bit_error/bit_count
	 */
	int bit_error;
	int bit_count;

	/* LNB status */
	int lnb_err;

	/* Average values */
	int64_t avg_rf_level;
	int64_t avg_ucblocks;
	int64_t avg_snr;
	int64_t avg_count;
};

/* main pointer to Joker TV */
struct joker_t {
	void *libusb_opaque;
	void *i2c_opaque;
	void *fe_opaque;
	struct service_thread_opaq_t *service_threading;
	struct big_pool_t *pool;
	void *io_mux_opaq;
	uint16_t fw_ver; // firmware version

	int libusb_verbose;
	/* hold chip list that should be in reset state */
	int reset;
	/* status callback */
	status_callback_t status_callback;
	struct stat_t stat;
	/* last params used for tune call */
	struct tune_info_t *info;

	/* CAM module */
	struct ci_thread_opaq_t *ci_threading;
	void *joker_ci_opaque;
	void *joker_en50221_opaque;
	ci_callback_t ci_info_callback;
	ci_callback_t ci_caid_callback;
	int ci_verbose; /* non 0 for debugging CI */
	int ci_enable; /* enable CAM module */
	int cam_query_send; /* send QUERY CA PMT to check subscription */

	/* CAM module (EN50221) TCP server */
	int ci_server_port;
	struct ci_server_thread_opaq_t *ci_server_threading;
	int ci_client_fd;
	int ci_ts_enable; // Enable TS traffic through CAM module

	/* TS check vars */
	int last_pattern;

	/* loop TS traffic 
	 * send to Joker TV over USB (EP2 OUT)
	 * receive from Joker TV over USB (EP1 IN)
	 */
	unsigned char *loop_ts_filename;
	struct loop_thread_opaq_t *loop_threading;

	/* XML file with lock instructions */
	char *xml_in_filename;

	/* XML file with lock results (RF Level, BER, etc) */
	char *csv_out_filename;
	FILE *csv_out_filename_fd;

	/* blind scan (DVB-S/S2 only) */
	int blind_scan;
	char *blind_out_filename;
	FILE *blind_out_filename_fd;
	char *blind_programs_filename;
	FILE *blind_programs_filename_fd;
	char *blind_power_file_prefix;
	blind_scan_callback_t blind_scan_cb;

	/* Raw data from usb */
	char *raw_data_filename;
	FILE *raw_data_filename_fd;
	
	/* CAM interaction dump file */
	char *cam_pcap_filename;
	FILE *cam_pcap_filename_fd;

	/* Enabled if high bandwidth usb isochronous transfer supported 
	 * First implemented in Joker TV fw revision 0x2d
	 * for more detail see xHCI spec Table B-2
	 * */
	int high_bandwidth_isoc_support;
	/* Maximum isochronous packets size per microframe (125usec) */
	int max_isoc_packets_size;
	/* Maximum isochronous packets per URB (used for libusb_submit_transfer) */
	int max_isoc_packets_count;
};

#ifdef __cplusplus
extern "C" {
#endif

/* open Joker TV on USB
 * return error code if fail
 * or 0 if success
 */
int joker_open(struct joker_t *joker);

/* release Joker TV device */
int joker_close(struct joker_t *joker);

#ifdef __cplusplus
}
#endif

#endif /* end */
