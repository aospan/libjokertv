/* 
 * this file contains driver for Joker TV card
 * Supported standards:
 *
 * DVB-S/S2 – satellite, is found everywhere in the world
 * DVB-T/T2 – mostly Europe
 * DVB-C/C2 – cable, is found everywhere in the world
 * ISDB-T – Brazil, Latin America, Japan, etc
 * ATSC – USA, Canada, Mexico, South Korea, etc
 * DTMB – China, Cuba, Hong-Kong, Pakistan, etc
 *
 * (c) Abylay Ospan <aospan@jokersys.com>, 2017
 * LICENSE: GPLv2
 * https://tv.jokersys.com
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <libusb.h>
#include <pthread.h>

#include <queue>
#include "joker_tv.h"
#include "u_drv_tune.h"
#include "u_drv_data.h"

int main ()
{
  struct tune_info_t info;
  struct big_pool * bp = 0;
  int status = 0, ret = 0;
  struct joker_t joker;

  /* open Joker TV on USB bus */
	if ((ret = joker_open(&joker)))
    return ret;

	if ((ret = joker_i2c_init(&joker)))
    return ret;

  if (tune(&joker, &info))
    return -1;

  // bp = start_ts();
  while(1) {
    status = read_status(&info);
    printf("LOCK status=%d error=%s \n", status, strerror(status) );
    sleep (1);

    if (!status) {
      // LOCKED
    }
  }

}
