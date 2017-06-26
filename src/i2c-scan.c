/* 
 * Scan I2C bus
 * Joker Eco-system
 * https://jokersys.com
 * (c) Abylay Ospan, 2017
 * aospan@jokersys.com
 * GPLv2
 */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <joker_tv.h>
#include <joker_i2c.h>

int main() {
	int i = 0;
	int ret = 0;
	struct joker_t joker;

	joker.verbose = 0;
	joker.libusb_verbose = 0;

	if ((ret = joker_open(&joker)))
		return ret;

	if ((ret = joker_i2c_init(&joker)))
		return ret;

	for(i = 0; i < 0x7F; i++) {
		if (!(ret = joker_i2c_ping(&joker, i))) {
			printf("0x%x address ACKed on i2c bus\n", i );
		} else {
			jdebug("0x%x address err=%d (%s)\n", i, ret, strerror(ret));
		}
	}

	joker_i2c_close(&joker);
	joker_close(&joker);
}
