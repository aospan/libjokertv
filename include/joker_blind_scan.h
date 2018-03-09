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

#define SR_DEFAULT_COEFF 1.00007273257

typedef enum {
	EVENT_DETECT,    /**< Detect channel. */
	EVENT_PROGRESS,  /**< Update progress. */
	EVENT_POWER,	/**< Power info for spectrum draw */
	EVENT_CAND		/**< Candidates info */
} blind_scan_res_event_id_t;

typedef struct blind_scan_res_t {
    blind_scan_res_event_id_t event_id;
    int progress;
    struct joker_t *joker;
    struct tune_info_t *info; // found transponder
    struct list_head *programs; // found programs inside transponder
} blind_scan_res_t;

int blind_scan(struct joker_t *joker, struct tune_info_t *info);

#ifdef __cplusplus
}
#endif

#endif /* end */
