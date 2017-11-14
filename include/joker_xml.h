/* 
 * process input XML file with lock instructions (frequencies, etc)
 *
 * https://jokersys.com
 * (c) Abylay Ospan, 2017
 * aospan@jokersys.com
 * GPLv2
 */

#include <stdio.h>
#include "joker_tv.h"

#ifndef _JOKER_XML
#define _JOKER_XML	1

#ifdef __cplusplus
extern "C" {
#endif

/* process input XML
 * return 0 if success
 */
int joker_process_xml(struct joker_t * joker);

#ifdef __cplusplus
}
#endif

#endif /* end */
