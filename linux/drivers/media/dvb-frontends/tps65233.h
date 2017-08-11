/*
 * tps65233.c
 *
 * Driver for LNB supply and control IC TPS65233
 *
 * Copyright (C) 2017 Abylay Ospan <aospan@netup.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef TPS65233_H
#define TPS65233_H

#include <linux/i2c.h>
#include <linux/kconfig.h>
#include <linux/dvb/frontend.h>


/* From TPS65233-1 datasheet */

/* Control Register 1 - Address: 0x00H
 * 1: I2C control enabled;
 * 0: I2C control disabled */
#define	TPS65233_I2C_CONTROL	1<<7

/* Tone Gate. Allows either the internal or external 22-kHz
 * tone signals to be gated.
 * 1: Tone Gate on use;
 * 0: Tone gate off */
#define	TPS65233_TONE_GATE	1<<5

/* Tone mode. Select between the use of an external 22-kHz
 * or internal 22-kHz signal.
 * 1: internal;
 * 0: external */
#define	TPS65233_TONE_MODE	1<<4

/* LNB output voltage Enable
 * 1: output enabled;
 * 0: output disabled */
#define	TPS65233_EN		1<<3

/* See Table 4 for output voltage selection
 * bit2 bit1 bit0 voltage
 * 0 0 0	13
 * 0 0 1	13.4
 * 0 1 0	13.8
 * 0 1 1	14.2
 * 1 0 0	18
 * 1 0 1	18.6
 * 1 1 0	19.2
 * 1 1 1	19.8 */
#define	TPS65233_13V		1
#define	TPS65233_18V		5

/* Control Register 2 - Address: 0x01H */
/* 1: current limit set by external resistor;
 * 0: current limit set by register */
#define	TPS65233_CL_EXT		1<<0

/* Current limit set bits 
 * bit2 bit1 limit (mA)
 * 0 0 400
 * 0 1 600
 * 1 0 750
 * 1 1 1000 */
#define	TPS65233_LIMIT_400	0<<1
#define	TPS65233_LIMIT_600	1<<1
#define	TPS65233_LIMIT_750	2<<1
#define	TPS65233_LIMIT_1000	3<<1

/* Status Register 1 - Address: 0x02H */
/* T125
 * 1: if die temperature T > 125°C;
 * 0: if die temperature T < 125°C */
#define	TPS65233_T125	1<<7

/* 1: thermal shutdown occurs;
 * 0: thermal shutdown does not occur */
#define	TPS65233_TSD	1<<3

/* Overcurrent protection. If over current conditions last for
 * more than 48 ms.
 * 1: Overcurrent protection triggered.
 * 0: Overcurrent protection conditions released */
#define	TPS65233_OCP	1<<2

/* Cable connection good.
 * 1: Output current above 50 mA;
 * 0: Output current less than 50 mA */
#define	TPS65233_CABLE_GOOD	1<<1

/* LNB output voltage in range.
 * 1: In range;
 * 0: Out of range */
#define	TPS65233_VOUT_GOOD	1<<0

struct tps65233_config {
	u8	i2c_address;
};

#if IS_REACHABLE(CONFIG_DVB_TPS65233)
struct dvb_frontend *tps65233_attach(
	struct dvb_frontend *fe,
	struct tps65233_config *cfg,
	struct i2c_adapter *i2c);
#else
static inline struct dvb_frontend *tps65233_attach(
	struct dvb_frontend *fe,
	struct tps65233_config *cfg,
	struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif
