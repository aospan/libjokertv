/*
 *    Support for AltoBeam ATBM888x DMB-TH demodulator
 *
 *    Copyright (C) 2016 Abylay Ospan <aospan@netup.ru>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __ATBM888x_H__
#define __ATBM888x_H__

#include <linux/kconfig.h>
#include <linux/dvb/frontend.h>
#include <linux/i2c.h>

#define ATBM888x_PROD_888x 0
#define ATBM888x_PROD_8831 1

struct atbm888x_config {
	/* the demodulator's i2c address */
	u8 demod_address;

	/* parallel or serial transport stream */
	u8 serial_ts;

	/* transport stream clock output only when receiving valid stream */
	u8 ts_clk_gated;

	/* Decoder sample TS data at rising edge of clock */
	u8 ts_sampling_edge;

	/* Decoder sample TS data bit 0 or 7 (for serial mode only) */
	u8 ts_data_bit;

	/* Oscillator clock frequency */
	u32 osc_clk_freq; /* in kHz */

	/* IF frequency */
	u32 if_freq; /* in kHz */

	/* Swap I/Q for zero IF */
	u8 zif_swap_iq;

	/* Tuner AGC settings */
	u8 agc_min;
	u8 agc_max;
	u8 agc_hold_loop;
};

#if IS_REACHABLE(CONFIG_DVB_ATBM888x)
extern struct dvb_frontend *atbm888x_attach(const struct atbm888x_config *config,
		struct i2c_adapter *i2c);
#else
static inline
struct dvb_frontend *atbm888x_attach(const struct atbm888x_config *config,
		struct i2c_adapter *i2c) {
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif /* CONFIG_DVB_ATBM888x */

#endif /* __ATBM888x_H__ */
