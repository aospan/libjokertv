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
};

/* main pointer to Joker TV */
struct joker_t {
	void *libusb_opaque;
	void *i2c_opaque;
	void *fe_opaque;
	struct service_thread_opaq_t *service_threading;
	int libusb_verbose;
	int unreset;
	/* status callback */
	status_callback_t status_callback;
	struct stat_t stat;
};

#ifdef __cplusplus
extern "C" {
#endif

/* open Joker TV on USB
 * return negative error code if fail
 * or 0 if success
 */
int joker_open(struct joker_t *joker);

/* release Joker TV device */
int joker_close(struct joker_t *joker);

#ifdef __cplusplus
}
#endif

#endif /* end */
