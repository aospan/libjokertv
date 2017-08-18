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
  uint32_t	frequency; /* in HZ */
  uint32_t	symbol_rate;
  uint32_t	bandwidth_hz;   /* 0 = AUTO */
  void*		fe_opaque;
  int		refresh; /* status refresh interval in ms */
};

#define SIGNAL_BAD 0
#define SIGNAL_WEAK 1
#define SIGNAL_GOOD 2

/* struct used to periodic status checking */
struct stat_t {
	struct joker_t *joker;
	struct tune_info_t *info;
	int32_t cancel;

	/* signal monitoring */

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
int read_status(struct tune_info_t *info);

/* Read all stats related to receiving signal
 * RF level
 * SNR (CNR)
 * Quality
 * Uncorrected blocks
 *
 * return 0 if success
 * other values is errors */
int read_signal_stat(struct tune_info_t *info, struct stat_t *stat);

/* stop tune */
// int stop(struct joker_t *joker);

#ifdef __cplusplus
}
#endif

#endif /* end */
