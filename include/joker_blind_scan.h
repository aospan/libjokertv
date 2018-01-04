/* 
 * https://jokersys.com
 * (c) Abylay Ospan, 2017
 * aospan@jokersys.com
 * GPLv2
 */

#include <joker_i2c.h>

#ifndef _JOKER_BLIND_SCAN
#define _JOKER_BLIND_SCAN	1

#ifdef __cplusplus
extern "C" {
#endif

typedef void(*blind_scan_callback_t)(void *data);

int blind_scan(struct joker_t *joker, struct tune_info_t *info,
		struct dvb_frontend *fe);

#ifdef __cplusplus
}
#endif

#endif /* end */
