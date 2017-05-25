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
 * @config:		Registers configuration:
 *			offset 0: 1st register address, always 0x02 (DATA1)
 *			offset 1: DATA1 register value
 *			offset 2: DATA2 register value
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
		// dev_dbg(&priv->i2c->dev,
				// "%s(): I2C transfer %d done (%d)\n",
				// __func__, i, ret);
		if (ret >= 0 && ret != 1)
			ret = -EIO;
		if (ret < 0) {
			dev_dbg(&priv->i2c->dev,
				"%s(): I2C transfer %d failed (%d)\n",
				__func__, i, ret);
			return ret;
		}
	}
	printk("%s: reg=0x%x val=0x%x\n",
			__func__, reg, *val);

#if 0
	if ((status[0] & (TPS65233_STATUS_OFL | TPS65233_STATUS_VMON)) != 0) {
		dev_err(&priv->i2c->dev,
			"%s(): voltage in failure state, status reg 0x%x\n",
			__func__, status[0]);
		return -EIO;
	}
#endif
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

static int tps65233_set_voltage(struct dvb_frontend *fe,
			      enum fe_sec_voltage voltage)
{
	int ret;
	u8 data1_reg;
	const char *vsel;
	struct tps65233_priv *priv = fe->sec_priv;
	u8 val = 0;

	tps65233_read_reg(priv, 0x02, &val);

	if (val == 0x0) {
		printk("WARNING ! LNB voltage 0 ! Reset \n");
		tps65233_write_reg(priv, 0x0, 0xad);
		tps65233_write_reg(priv, 0x1, 0x08);
	}
	return val;

#if 0
	struct i2c_msg msg = {
		.addr = priv->i2c_address,
		.flags = 0,
		.len = sizeof(priv->config),
		.buf = priv->config
	};

	switch (voltage) {
	case SEC_VOLTAGE_OFF:
		data1_reg = 0x00;
		vsel = "Off";
		break;
	case SEC_VOLTAGE_13:
		data1_reg = TPS65233_VSEL_13;
		vsel = "13V";
		break;
	case SEC_VOLTAGE_18:
		data1_reg = TPS65233_VSEL_18;
		vsel = "18V";
		break;
	default:
		return -EINVAL;
	}
	priv->config[1] = data1_reg;
	dev_dbg(&priv->i2c->dev,
		"%s(): %s, I2C 0x%x write [ %02x %02x %02x ]\n",
		__func__, vsel, priv->i2c_address,
		priv->config[0], priv->config[1], priv->config[2]);
	ret = i2c_transfer(priv->i2c, &msg, 1);
	if (ret >= 0 && ret != 1)
		ret = -EIO;
	if (ret < 0) {
		dev_err(&priv->i2c->dev, "%s(): I2C transfer error (%d)\n",
			__func__, ret);
		return ret;
	}
	if (voltage != SEC_VOLTAGE_OFF) {
		msleep(120);
		// ret = tps65233_read(priv);
	} else {
		msleep(20);
		ret = 0;
	}
	return ret;
#endif
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
	// priv->i2c_address = (cfg->i2c_address >> 1);
	priv->i2c = i2c;
	priv->config[0] = 0x02;
	priv->config[1] = 0x00;
	priv->config[2] = cfg->data2_config;
	fe->sec_priv = priv;

	/* for(i = 0; i < 3; i++) {
		tps65233_read_reg(priv, i, &val);
	} */

	// tps65233_write_reg(priv, 0x0, 0xbc);
	tps65233_write_reg(priv, 0x0, 0xad);
	tps65233_write_reg(priv, 0x1, 0x08);

	tps65233_read_reg(priv, i, &val);
#if 0
	while (1) {
		printk("****\n");
		for(i = 0; i < 3; i++) {
			tps65233_read_reg(priv, i, &val);
		}
		sleep(1);
	}
#endif

#if 0
	if (tps65233_set_voltage(fe, SEC_VOLTAGE_OFF)) {
		dev_err(&i2c->dev,
			"%s(): no TPS65233 found at I2C addr 0x%02x\n",
			__func__, priv->i2c_address);
		kfree(priv);
		fe->sec_priv = NULL;
		return NULL;
	}
#endif

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
