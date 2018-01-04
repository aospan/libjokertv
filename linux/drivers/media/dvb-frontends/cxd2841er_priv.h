/*
 * cxd2841er_priv.h
 *
 * Sony CXD2441ER digital demodulator driver internal definitions
 *
 * Copyright 2012 Sony Corporation
 * Copyright (C) 2014 NetUP Inc.
 * Copyright (C) 2014 Sergey Kozlov <serjk@netup.ru>
 * Copyright (C) 2014 Abylay Ospan <aospan@netup.ru>
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

#ifndef CXD2841ER_PRIV_H
#define CXD2841ER_PRIV_H

#define I2C_SLVX			0
#define I2C_SLVT			1

#define CXD2837ER_CHIP_ID		0xb1
#define CXD2838ER_CHIP_ID		0xb0
#define CXD2841ER_CHIP_ID		0xa7
#define CXD2843ER_CHIP_ID		0xa4
#define CXD2854ER_CHIP_ID		0xc1

#define CXD2841ER_DVBS_POLLING_INVL	10

struct cxd2841er_cnr_data {
	u32 value;
	int cnr_x1000;
};

enum cxd2841er_dvbt2_profile_t {
	DVBT2_PROFILE_ANY = 0,
	DVBT2_PROFILE_BASE = 1,
	DVBT2_PROFILE_LITE = 2
};

/* DVB-C constellation */
enum sony_dvbc_constellation_t {
	SONY_DVBC_CONSTELLATION_16QAM,
	SONY_DVBC_CONSTELLATION_32QAM,
	SONY_DVBC_CONSTELLATION_64QAM,
	SONY_DVBC_CONSTELLATION_128QAM,
	SONY_DVBC_CONSTELLATION_256QAM
};

enum cxd2841er_state {
	STATE_SHUTDOWN = 0,
	STATE_SLEEP_S,
	STATE_ACTIVE_S,
	STATE_SLEEP_TC,
	STATE_ACTIVE_TC
};

struct cxd2841er_priv {
	struct dvb_frontend		frontend;
	struct i2c_adapter		*i2c;
	u8				i2c_addr_slvx;
	u8				i2c_addr_slvt;
	const struct cxd2841er_config	*config;
	enum cxd2841er_state		state;
	u8				system;
	enum cxd2841er_xtal		xtal;
	enum fe_caps caps;
	u32				flags;
	u8				chip_id;
	int				blind_scan_cancel;
	int				system_sony;
	/* The count of calculation for power spectrum calculation in BlindScan and TuneSRS
            Value:
            - 0: Reduce smoothing time from normal
            - 1: Normal (Default)
            - 2: 2 times smoother than normal
            - 3: 4 times smoother than normal
            - 4: 8 times smoother than normal
            - 5: 16 times smoother than normal
            - 6: 32 times smoother than normal
            - 7: 64 times smoother than normal
	*/
	uint8_t dvbss2PowerSmooth;

};

int cxd2841er_write_reg(struct cxd2841er_priv *priv,
			       u8 addr, u8 reg, u8 val);

int cxd2841er_set_frontend_s(struct dvb_frontend *fe);

int cxd2841er_dvbs2_set_symbol_rate(struct cxd2841er_priv *priv,
					   u32 symbol_rate);

int cxd2841er_read_regs(struct cxd2841er_priv *priv,
			       u8 addr, u8 reg, u8 *val, u32 len);

int cxd2841er_write_regs(struct cxd2841er_priv *priv,
				u8 addr, u8 reg, const u8 *data, u32 len);

u16 cxd2841er_read_agc_gain_s(struct cxd2841er_priv *priv);
#endif
