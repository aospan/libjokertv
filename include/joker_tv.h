/* 
 * Access to Joker TV
 * https://jokersys.com
 * (c) Abylay Ospan, 2017
 * aospan@jokersys.com
 * GPLv2
 */

#include <stdio.h>
#include <libusb.h>

#ifndef _JOKER_TV
#define _JOKER_TV 1

/* TODO: debug system */
// #define DBG
#ifdef DBG
#define jdebug(...) printf(__VA_ARGS__);
#else
#define jdebug(...) {};
#endif

/* constants */
#define FNAME_LEN		512
#define TS_SIZE			188
// should be more than 128KB
#define TS_BUF_MAX_SIZE		TS_SIZE*700
#define TS_SYNC			0x47
#define TS_WILDCARD_PID		0x2000

/* main pointer to Joker TV */
struct joker_t {
  void * libusb_opaque;
  void * i2c_opaque;
  int libusb_verbose;
  int unreset;
};

#ifdef __cplusplus
extern "C" {
#endif

/* open Joker TV on USB
 * return negative error code if fail
 * or 0 if success
 */
int joker_open(struct joker_t *joker);

/* release Joker TV device */
int joker_close(struct joker_t *joker);

#ifdef __cplusplus
}
#endif

#endif /* end */
