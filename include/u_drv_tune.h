/* 
 * https://jokersys.com
 * (c) Abylay Ospan, 2017
 * aospan@jokersys.com
 * GPLv2
 */

#include <joker_i2c.h>

#ifndef _U_DRV_TUNE
#define _U_DRV_TUNE	1

#ifdef __cplusplus
extern "C" {
#endif

#define LNB_HEALTH_INTERVAL 2

/* constants copy from Linux: include/uapi/linux/dvb/frontend.h 
 * do not change order !
 */
enum joker_fe_delivery_system {
  JOKER_SYS_UNDEFINED,
  JOKER_SYS_DVBC_ANNEX_A,
  JOKER_SYS_DVBC_ANNEX_B,
  JOKER_SYS_DVBT,
  JOKER_SYS_DSS,
  JOKER_SYS_DVBS,
  JOKER_SYS_DVBS2,
  JOKER_SYS_DVBH,
  JOKER_SYS_ISDBT,
  JOKER_SYS_ISDBS,
  JOKER_SYS_ISDBC,
  JOKER_SYS_ATSC,
  JOKER_SYS_ATSCMH,
  JOKER_SYS_DTMB,
  JOKER_SYS_CMMB,
  JOKER_SYS_DAB,
  JOKER_SYS_DVBT2,
  JOKER_SYS_TURBO,
  JOKER_SYS_DVBC_ANNEX_C,
};

enum joker_fe_modulation {
  JOKER_QPSK,
  JOKER_QAM_16,
  JOKER_QAM_32,
  JOKER_QAM_64,
  JOKER_QAM_128,
  JOKER_QAM_256,
  JOKER_QAM_AUTO,
  JOKER_VSB_8,
  JOKER_VSB_16,
  JOKER_PSK_8,
  JOKER_APSK_16,
  JOKER_APSK_32,
  JOKER_DQPSK,
  JOKER_QAM_4_NR,
};

enum joker_fe_sec_voltage {
	JOKER_SEC_VOLTAGE_13,
	JOKER_SEC_VOLTAGE_18,
	JOKER_SEC_VOLTAGE_OFF
};

/* LNB */
struct joker_lnb_t {
	int lowfreq;
	int highfreq;
	int switchfreq;
};

/* 22 kHz tone */
enum joker_fe_sec_tone_mode { 
	JOKER_SEC_TONE_ON,
	JOKER_SEC_TONE_OFF
};

/* frontend parameters (standard, freq, etc)
 * copy from Linux: drivers/media/dvb-core/dvb_frontend.h
 */
struct tune_info_t {
	enum	joker_fe_delivery_system delivery_system;
	enum	joker_fe_modulation      modulation;
	enum	joker_fe_sec_voltage	voltage;
	enum joker_fe_sec_tone_mode	tone;
	struct joker_lnb_t	lnb;
	uint64_t	frequency; /* in HZ, 64-bit int used for freqs higher than 4GHz */
	uint32_t	symbol_rate;
	uint32_t	bandwidth_hz;   /* 0 = AUTO */

};

/* tune to specified source (DVB, ATSC, etc)
 * this call is non-blocking (returns after configuring frontend)
 * return negative error code if failed
 * return 0 if configure success
 * use read_status call for checking LOCK/NOLOCK status
 */
int tune(struct joker_t *joker, struct tune_info_t *info);

/* read status 
 * return 0 if LOCKed 
 * return -EAGAIN if NOLOCK
 * return negative error code if error
 */
int read_status(struct joker_t *joker);

/* Read all stats related to receiving signal
 * RF level
 * SNR (CNR)
 * Quality
 * Uncorrected blocks
 *
 * return 0 if success
 * other values is errors */
int read_signal_stat(struct joker_t *joker, struct stat_t *stat);

/* stop service thread */
int stop_service_thread(struct joker_t * joker);

/* stop tune */
// int stop(struct joker_t *joker);

#ifdef __cplusplus
}
#endif

#endif /* end */
