/* 
 * https://jokersys.com
 * (c) Abylay Ospan, 2017
 * aospan@jokersys.com
 * GPLv2
 */

#include <u_drv_data.h>
#include <joker_i2c.h>

#ifndef _U_DRV_TUNE
#define _U_DRV_TUNE	1

/* frontend parameters (standard, freq, etc)
 */
struct tune_info_t {
  void * fe_opaque;
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

/* stop tune */
// int stop(struct joker_t *joker);

#ifdef __cplusplus
}
#endif

#endif /* end */
