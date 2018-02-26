/* 
 * Access to Joker TV CI (Common Interface)
 * EN50221 part
 * 
 * Conditional Access Module for scrambled streams (pay tv)
 * Based on EN 50221-1997 standard
 * 
 * https://jokersys.com
 * (c) Abylay Ospan, 2017
 * aospan@jokersys.com
 * GPLv2
 */

#include <stdio.h>
#include "joker_tv.h"
#include "joker_ts.h"

#ifndef _JOKER_EN50221
#define _JOKER_EN50221	1

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_EN50221_BUF 4096

/* stop EN50221
 * return 0 if success
 * other return values indicates error
 */
int joker_ci_en50221_stop(struct joker_t * joker);

/* start EN50221
 * return 0 if success
 * other return values indicates error
 */
int joker_ci_en50221_start(struct joker_t * joker);

/* initialize EN50221
 * return 0 if success
 * other return values indicates error
 */
int joker_ci_en50221_init(struct joker_t * joker);

/* add program to descramble list
 * return 0 if success
 */
int joker_en50221_descramble_add(struct joker_t * joker, int program);

/* clear descramble program list
 * return 0 if success
 */
int joker_en50221_descramble_clear(struct joker_t * joker);

/* update PMT for program
 * call this if PMT changed
 * return 0 if success
 */
int joker_en50221_pmt_update(struct program_t *_program, void* _pmt, int len);

/* init MMI menu 
 * call this before any joker_en50221_mmi_call
 * cb will be called when CAM module send reply
 * return 0 if success
 */
int joker_en50221_mmi_enter(struct joker_t * joker, mmi_callback_t cb);

/* send user choice to CAM module
 * return 0 if success */
int joker_en50221_mmi_call(struct joker_t * joker, const unsigned char *buf, int len);

/* set dvb time arrived in TDT/TOT PSI packets */
void joker_en50221_set_dvbtime(struct big_pool_t *pool, time_t datetime_dvbtime);

#ifdef __cplusplus
}
#endif

#endif /* end */
