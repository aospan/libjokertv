/* 
 * Access to Joker TV SPI bus
 * m25p80 128M SPI flash used (M25P128-VME6GB)
 *
 * https://jokersys.com
 * (c) Abylay Ospan, 2017
 * aospan@jokersys.com
 * GPLv2
 */

#include <stdio.h>
#include <libusb.h>
#include "joker_tv.h"

#ifndef _JOKER_SPI
#define _JOKER_SPI	1

/* m25p128 constants */
#define J_SPI_PAGE_SIZE 256
#define J_SPI_SECTOR_SIZE 262144

#ifdef __cplusplus
extern "C" {
#endif

/* check SPI flash id
 * return 0 if OK
 */
int joker_flash_checkid(struct joker_t * joker);

/* write file to SPI flash
 * return 0 if OK
 */
int joker_flash_write(struct joker_t * joker, char * filename);

#ifdef __cplusplus
}
#endif

#endif /* end */
