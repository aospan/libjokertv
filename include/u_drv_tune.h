/* 
 * https://jokersys.com
 * (c) Abylay Ospan, 2017
 * aospan@jokersys.com
 * GPLv2
 */

#include <joker_i2c.h>

#ifndef _U_DRV_TUNE
#define _U_DRV_TUNE	1

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

/* frontend parameters (standard, freq, etc)
 * copy from Linux: drivers/media/dvb-core/dvb_frontend.h
 */
struct tune_info_t {
  enum joker_fe_delivery_system delivery_system;
  enum joker_fe_modulation      modulation;
  uint32_t                     frequency; /* in HZ */
  uint32_t                     symbol_rate;
  uint32_t                     bandwidth_hz;   /* 0 = AUTO */
  void * fe_opaque;
  int				refresh; /* status refresh interval in ms */
};

struct stat_t {
	struct joker_t *joker;
	struct tune_info_t *info;
};

#ifdef __cplusplus
extern "C" {
#endif

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

/* return signal strength
 * range 0x0000 - 0xffff
 * 0x0000 - weak signal
 * 0xffff - stong signal
 * */
int read_signal(struct tune_info_t *info);

/* return uncorrected blocks
 * can happen if signal is weak or noisy
 */
int read_ucblocks(struct tune_info_t *info);

/* stop tune */
// int stop(struct joker_t *joker);

#ifdef __cplusplus
}
#endif

#endif /* end */
