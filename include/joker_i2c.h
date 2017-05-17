/* 
 * Access to Joker TV FPGA
 * https://jokersys.com
 * (c) Abylay Ospan, 2017
 * aospan@jokersys.com
 * GPLv2
 */

#include <stdio.h>
#include <libusb.h>
#include <joker_tv.h>

#ifndef _JOKER_I2C
#define _JOKER_I2C	1

struct joker_i2c_t 
{
  void * libusb_opaque;
};

#ifdef __cplusplus
extern "C" {
#endif

/* Open joker I2C
 * return 0 if failed */
int joker_i2c_init(struct joker_t *joker);

int joker_i2c_close(struct joker_t *joker);

/* write bytes to i2c chip
 * chip - chip address (7 bit notation)
 * data, size - actual data to write
 * return 0 if success
 * return error code if fail
 *
 * resulting actual transaction on i2c bus:
 *	Start/chip - data[0] - data[1] ... data[n]/Stop
 */
int joker_i2c_write(struct joker_t *joker, uint8_t chip, unsigned char * data, int size);

/* read bytes from i2c chip
 * chip - chip address (7 bit notation)
 * return 0 if success
 * return error code if fail
 *
 * resulting actual transaction on i2c bus:
 *	Start/chip - data[0] - data[1] ... data[n]/Stop
 */
int joker_i2c_read(struct joker_t *joker, uint8_t chip, unsigned char * data, int size);

/* "ping" i2c address.
 * return 0 (success) if ACKed
 * return -1 (fail) if no ACK received
 */
int joker_i2c_ping(struct joker_t *joker, uint8_t chip);

#ifdef __cplusplus
}
#endif

#endif /* end */
