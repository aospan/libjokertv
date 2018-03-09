/*
 * tps65233.c
 *
 * Driver for LNB supply and control TPS65233
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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/slab.h>

#include "dvb_frontend.h"
#include "tps65233.h"

/**
 * struct tps65233_priv - TPS65233 driver private data
 * @i2c:		pointer to the I2C adapter structure
 * @i2c_address:	I2C address of TPS65233 SEC chip
 */
struct tps65233_priv {
	struct i2c_adapter	*i2c;
	u8			i2c_address;
	u8			config[3];
};

#define TPS65233_STATUS_OFL	0x1
#define TPS65233_STATUS_VMON	0x4
#define TPS65233_VSEL_13		0x03
#define TPS65233_VSEL_18		0x0a

static int tps65233_read_reg(struct tps65233_priv *priv, u8 reg, u8 *val)
{
	int i, ret;
	struct i2c_msg msg[2] = {
		{
			.addr = priv->i2c_address,
			.flags = 0,
			.len = 1,
			.buf = &reg
		}, {
			.addr = priv->i2c_address,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = val
		}
	};

	for (i = 0; i < 2; i++) {
		ret = i2c_transfer(priv->i2c, &msg[i], 1);
		if (ret >= 0 && ret != 1)
			ret = -EIO;
		if (ret < 0) {
			dev_dbg(&priv->i2c->dev,
				"%s(): I2C transfer %d failed (%d)\n",
				__func__, i, ret);
			return ret;
		}
	}
	return 0;
}

static int tps65233_write_reg(struct tps65233_priv *priv,
		u8 reg, const u8 data)
{
	int ret;
	u8 buf[2];
	struct i2c_msg msg[1] = {
		{
			.addr = priv->i2c_address,
			.flags = 0,
			.len = 2,
			.buf = buf,
		}
	};

	buf[0] = reg;
	buf[1] = data;
	ret = i2c_transfer(priv->i2c, msg, 1);
	if (ret >= 0 && ret != 1)
		ret = -EREMOTEIO;
	if (ret < 0) {
		dev_warn(&priv->i2c->dev,
				"%s: i2c wr failed=%d reg=%02x\n",
				__func__, ret, reg);
		return ret;
	}
	return 0;
}

/* return different error codes for different error conditions 
 * -ENFILE for "LNB output voltage out of range"
 * -ERANGE for "Output current less than 50 mA"
 * -EMFILE for "Overcurrent protection triggered"
 *  0 if no errors detected
 */
static int tps65233_set_voltage(struct dvb_frontend *fe,
			      enum fe_sec_voltage voltage)
{
	int ret = 0;
	u8 data1_reg;
	const char *vsel;
	struct tps65233_priv *priv = fe->sec_priv;
	u8 val = 0;

	// clean bits first
	priv->config[0] &= 0xf0;

	// read current config from chip
	tps65233_read_reg(priv, 0x00, &val);

	switch(voltage) {
		case SEC_VOLTAGE_13:
			priv->config[0] |= (TPS65233_13V | TPS65233_EN);
			break;
		case SEC_VOLTAGE_18:
			priv->config[0] |= (TPS65233_18V | TPS65233_EN);
			break;
		case SEC_VOLTAGE_OFF:
		default:
			priv->config[0] &= ~TPS65233_EN;
			break;
	}

	tps65233_write_reg(priv, 0x0, priv->config[0]);
	dev_dbg(&priv->i2c->dev, "%s() voltage (%d) set done. config = 0x%.2x 0x%.2x\n",
			__func__, voltage, priv->config[0], priv->config[1]);
	msleep(10);

	/* sanity: check status register */
	tps65233_read_reg(priv, 0x02, &val);
	dev_dbg(&priv->i2c->dev, "%s() status=0x%x \n", __func__, val);

	/* return different error codes for different error conditions 
	 * -ENFILE for "LNB output voltage out of range"
	 * -ERANGE for "Output current less than 50 mA"
	 * -EMFILE for "Overcurrent protection triggered"
	 * */
	if (val & TPS65233_VOUT_GOOD) {
		dev_dbg(&priv->i2c->dev, "%s() LNB output voltage in range.\n", __func__);
	} else {
		ret = -ENFILE;
		dev_err(&priv->i2c->dev, "%s() Error: LNB output voltage out of range.\n", __func__);
	}

	if (val & TPS65233_CABLE_GOOD) {
		dev_dbg(&priv->i2c->dev, "%s() cable OK\n", __func__);
	} else {
		ret = -ERANGE;
		dev_err(&priv->i2c->dev, "%s() Error: cable fail. Output current less than 50 mA\n", __func__);
	}

	if (val & TPS65233_OCP) {
		dev_err(&priv->i2c->dev, "%s() Overcurrent protection triggered !\n", __func__);
		ret = -EMFILE;
	}

	return ret;
}

static void tps65233_release(struct dvb_frontend *fe)
{
	struct tps65233_priv *priv = fe->sec_priv;

	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	tps65233_set_voltage(fe, SEC_VOLTAGE_OFF);
	kfree(fe->sec_priv);
	fe->sec_priv = NULL;
}

struct dvb_frontend *tps65233_attach(struct dvb_frontend *fe,
				   struct tps65233_config *cfg,
				   struct i2c_adapter *i2c)
{
	struct tps65233_priv *priv;
	u8 val = 0;
	int i = 0;

	dev_dbg(&i2c->dev, "%s()\n", __func__);
	priv = kzalloc(sizeof(struct tps65233_priv), GFP_KERNEL);
	if (!priv)
		return NULL;
	priv->i2c_address = cfg->i2c_address;
	priv->i2c = i2c;
	priv->config[0] = TPS65233_I2C_CONTROL | TPS65233_TONE_GATE;
	// aospan: dish positioner (SG6100) doesn't recognize diseqc command
	// if setting tone "above".
	priv->config[1] = TPS65233_TONE_BELOW | TPS65233_LIMIT_600; 

	fe->sec_priv = priv;

	/* set registers */
	tps65233_write_reg(priv, 0x0, priv->config[0]);
	tps65233_write_reg(priv, 0x1, priv->config[1]);

	fe->ops.release_sec = tps65233_release;
	fe->ops.set_voltage = tps65233_set_voltage;

	dev_err(&i2c->dev, "%s(): attached at I2C addr 0x%02x\n",
		__func__, priv->i2c_address);
	return fe;
}
EXPORT_SYMBOL(tps65233_attach);

MODULE_DESCRIPTION("TI TPS65233 driver");
MODULE_AUTHOR("aospan@netup.ru");
MODULE_LICENSE("GPL");
