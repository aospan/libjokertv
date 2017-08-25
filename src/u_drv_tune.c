/* u-drv
 * Compile Linux kernel drivers inside user-level with
 * different stubs (i2c, printk, kmalloc, etc).
 * Now we can run and debug drivers as user-level process.
 *
 * (c) Abylay Ospan <aospan@jokersys.com>, 2017
 * LICENSE: GPLv2
 */

/*
 * this file contains drivers for Joker TV card
 * Supported standards:
 *
 * DVB-S/S2 – satellite, is found everywhere in the world
 * DVB-T/T2 – mostly Europe
 * DVB-C/C2 – cable, is found everywhere in the world
 * ISDB-T – Brazil, Latin America, Japan, etc
 * ATSC – USA, Canada, Mexico, South Korea, etc
 * DTMB – China, Cuba, Hong-Kong, Pakistan, etc
 *
 * https://tv.jokersys.com
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
typedef unsigned char           uint8_t;
typedef unsigned short int      uint16_t;
typedef unsigned int            uint32_t;

// Linux kernel header files
#include <drivers/media/dvb-frontends/helene.h>
#include <drivers/media/dvb-frontends/cxd2841er.h>
#include <drivers/media/dvb-frontends/lgdt3306a.h>
#include <drivers/media/dvb-frontends/atbm888x.h>
#include <drivers/media/dvb-frontends/tps65233.h>
#include <drivers/media/dvb-core/dvb_frontend.h>
#include <linux/dvb/frontend.h>
#include <time.h>
#include <linux/i2c.h>
#include "joker_i2c.h"
#include "joker_fpga.h"
#include "u_drv_tune.h"

static int joker_i2c_gate_ctrl(struct dvb_frontend *fe, int enable);

unsigned long phys_base = 0;
// const struct kernel_param_ops param_ops_int;

static struct tps65233_config lnb_config = {
	.i2c_address = 0x60
};

static struct cxd2841er_config demod_config = {
	.i2c_addr = 0xc8,
	.flags = CXD2841ER_TS_SERIAL | CXD2841ER_TSBITS | CXD2841ER_USE_GATECTRL,
	.xtal = SONY_XTAL_24000
};

static struct helene_config helene_conf = {
	.i2c_address = 0xc2,
	.xtal = SONY_HELENE_XTAL_24000,
};

static struct lgdt3306a_config lgdt3306a_config = {
	.i2c_addr               = 0xb2 >> 1,
	.qam_if_khz             = 4000,
	.vsb_if_khz             = 3250,
	.deny_i2c_rptr          = 1, /* Disabled */
	.spectral_inversion     = 0, /* Disabled */
	.mpeg_mode              = LGDT3306A_MPEG_SERIAL,
	.tpclk_edge             = LGDT3306A_TPCLK_RISING_EDGE,
	.tpvalid_polarity       = LGDT3306A_TP_VALID_HIGH,
	.xtalMHz                = 24, /* 24 or 25 */
};

static struct atbm888x_config atbm888x_config = {
	.demod_address = 0x40,
	.serial_ts = 1,
	.ts_sampling_edge = 1, // netup: we need falling edge
	.ts_data_bit = 0, // netup: data should be on TS[0] pin
	.ts_clk_gated = 0, // netup: we need TS_CLOCK_CONST_OUTPUT
	.osc_clk_freq = 24000, /* in kHz */
	.if_freq = 5000, /* kHz */
	.zif_swap_iq = 0,
	.agc_min = 0x1E,
	.agc_max = 0xEF,
	.agc_hold_loop = 0,
};

// for atbm888x driver
void mdelay(int n) {
	usleep((n) * 1000);
}

int printk(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vprintf( fmt, args );
	va_end(args);
	return 0;
}

void __dynamic_dev_dbg(const struct device *dev, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	// vprintf( fmt, args );
	va_end(args);
	return;
}

void *__kmalloc(size_t size, gfp_t flags)
{
	void *ptr = malloc(size);
	if (!ptr)
		return ptr;
	memset(ptr, 0, size);
	return ptr;
}

void *kzalloc(size_t size, gfp_t flags)
{
	return __kmalloc(size, flags);
}

void kfree(const void *p)
{
	free(p);
}

void dev_err(const struct device *dev, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vprintf( fmt, args );
	va_end(args);
	return;
}

void dev_info(const struct device *dev, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vprintf( fmt, args );
	va_end(args);
	return;
}

void dev_warn(const struct device *dev, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vprintf( fmt, args );
	va_end(args);
	return;
}

void warn_slowpath_null(const char *file, int line)
{
	printf("%s: file=%s line=%d\n", 
			__func__, file, line);
}

void msleep(unsigned int msecs)
{
	usleep(1000*msecs);
}

void print_hex_dump(const char *level, const char *prefix_str,
		int prefix_type, int rowsize, int groupsize,
		const void *buf, size_t len, bool ascii)
{
}

int i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	int i = 0;
	struct joker_t *joker = adap->algo_data;

	for (i = 0; i < num; i++) {
		if (msgs[i].flags & I2C_M_RD) {
			if (joker_i2c_read(joker, msgs[i].addr, msgs[i].buf, msgs[i].len))
				return -1;
		} else {
			if (joker_i2c_write(joker, msgs[i].addr, msgs[i].buf, msgs[i].len))
				return -1;
		}
	}
	return num;
}

void usleep_range(unsigned long min, unsigned long max)
{
	jdebug("%s min=%d max=%d\n", 
			__func__, min, max);
	usleep(max);
}

void __const_udelay(unsigned long xloops)
{
	usleep((xloops/0x000010c7));
}

ssize_t __modver_version_show(struct module_attribute *mattr,
		struct module_kobject *mk, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", "u-drv ver 0.1");
}

/* read status 
 * return JOKER_LOCK if LOCKed 
 * return JOKER_NOLOCK if NOLOCK
 * return negative error code if error
 */
int read_status(struct tune_info_t *info)
{
	enum fe_status status;
	struct dvb_frontend *fe = (struct dvb_frontend *)info->fe_opaque;

	if (!fe)
		return -EINVAL;

	fe->ops.read_status(fe, &status);
	jdebug("%s: status=0x%x \n", __func__, status);

	if (status == 0x1f)
		return JOKER_LOCK;

	return JOKER_NOLOCK;
}

/* Read all stats related to receiving signal
 * RF level
 * SNR (CNR)
 * Quality
 *
 * return 0 if success
 * other values is errors */
int read_signal_stat(struct tune_info_t *info, struct stat_t *stat)
{
	struct dvb_frontend *fe = (struct dvb_frontend *)info->fe_opaque;
	struct dtv_frontend_properties *prop = &fe->dtv_property_cache;
	uint8_t ifagcreg = 0, rfagcreg = 0, if_bpf_gain = 0;
	int32_t rssi = 0;

	if (!fe)
		return -EINVAL;

	// cleanup previous readings
	prop->strength.stat[0].uvalue = 0;
	prop->cnr.stat[0].svalue = 0;
	prop->block_error.stat[0].uvalue = 0;

	// read all stats from frontend
	fe->ops.get_frontend(fe, prop);

	// if we have special method to read RSSI from tuner
	// overwrite values obtained from demod then
	if (fe->ops.tuner_ops.get_rssi) {
		joker_i2c_gate_ctrl(fe, 1);
		fe->ops.tuner_ops.get_rssi(fe, &rssi);
		joker_i2c_gate_ctrl(fe, 0);
		prop->strength.stat[0].uvalue = rssi;
	}

	/* transfer values to stat_t */
	stat->rf_level = (int32_t)prop->strength.stat[0].uvalue;
	stat->snr = (int32_t)prop->cnr.stat[0].svalue;
	stat->ucblocks = prop->block_error.stat[0].uvalue;
	stat->bit_error = prop->post_bit_error.stat[0].uvalue;
	stat->bit_count = prop->post_bit_count.stat[0].uvalue;

	/* make signal quality decision using RF Level
	 * this is very 'shallow' estimations
	 *
	 * TODO: rework signal quality using BER, SNR, modulation parameters
	 */
	if (stat->ucblocks > 0 || stat->rf_level < -70000) // very bad
		stat->signal_quality = SIGNAL_BAD;
	else if (stat->rf_level > -40000) /* -40 dBm or greater is good */
		stat->signal_quality = SIGNAL_GOOD;
	else 
		stat->signal_quality = SIGNAL_WEAK;

	jdebug("RF Level %f dBm\n", (double)rssi/1000);

	return 0;
}

/* enable/disable i2c gate
 * Helene tuner lives behind this i2c gate
 */
static int joker_i2c_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	unsigned char buf[BUF_LEN];
	int ret = 0;
	struct joker_t *joker = fe->frontend_priv;

	if (!joker)
		return -EINVAL;

	jdebug("%s(): 0. enable=%d unreset=0x%x \n",
			__func__, enable, joker->unreset);
	if (enable)
		joker->unreset &= ~(OC_I2C_RESET_GATE);
	else
		joker->unreset |= OC_I2C_RESET_GATE;

	jdebug("%s(): enable=%d unreset=0x%x\n", __func__, enable, joker->unreset);

	buf[0] = J_CMD_RESET_CTRL_WRITE;
	buf[1] = joker->unreset;

	if ((ret = joker_cmd(joker, buf, 2, NULL /* in_buf */, 0 /* in_len */)))
		return ret;

	return 0;
}

/* tune to specified source (DVB, ATSC, etc)
 * this call is non-blocking (returns after configuring frontend)
 * return negative error code if failed
 * return 0 if configure success
 * use read_status call for checking LOCK/NOLOCK status
 */
int tune(struct joker_t *joker, struct tune_info_t *info)
{
	struct dvb_frontend *fe = NULL;
	struct i2c_adapter * i2c = NULL;
	struct vb2_dvb_frontend *fes[2];
	enum fe_status status;
	int fe_count = 2;
	int i = 0, num = 0;
	struct netup_unidvb_dev *ndev = NULL;
	unsigned int delay = 0;
	int ret = 0;
	unsigned char buf[BUF_LEN];
	int reset = 0xFF; /* reset all components on the board */
	int input = 0, need_lnb = 0;
	int cnt = 5; /* 5 times try to set LNB voltage */

	struct dvb_diseqc_master_cmd dcmd = {
		.msg = {0xFF},
		.msg_len = 6
	};

	i2c = (struct i2c_adapter *)malloc(sizeof(struct i2c_adapter));
	if (!i2c)
		return ENOMEM;

	if (!joker || !joker->i2c_opaque || !info)
		return EINVAL;

	i2c->algo_data = (void*)joker;

	switch (info->delivery_system)
	{
		case JOKER_SYS_ATSC:
			joker->unreset = OC_I2C_RESET_GATE | OC_I2C_RESET_TUNER | OC_I2C_RESET_LG;
			input = J_INSEL_LG;
			break;
		case JOKER_SYS_DTMB:
			joker->unreset = OC_I2C_RESET_GATE | OC_I2C_RESET_TUNER | OC_I2C_RESET_ATBM;
			input = J_INSEL_ATBM;
			break;
		case JOKER_SYS_DVBS:
		case JOKER_SYS_DVBS2:
			need_lnb = 1;
		case JOKER_SYS_DVBC_ANNEX_A:
		case JOKER_SYS_DVBT:
		case JOKER_SYS_DVBT2:
		case JOKER_SYS_ISDBT:
			joker->unreset = OC_I2C_RESET_GATE | OC_I2C_RESET_TUNER | OC_I2C_RESET_SONY;
			input = J_INSEL_SONY;
			break;
		default:
			printf("delivery system %d not supported \n", info->delivery_system);
			return ENODEV;
	}

	joker->unreset |= OC_I2C_RESET_TPS_CI;
	joker->unreset = ~joker->unreset;

	/* reset tuner and demods */
	buf[0] = J_CMD_RESET_CTRL_WRITE;
	buf[1] = reset;
	if ((ret = joker_cmd(joker, buf, 2, NULL /* in_buf */, 0 /* in_len */)))
		return ret;

	msleep(50);

	buf[0] = J_CMD_RESET_CTRL_WRITE;
	buf[1] = joker->unreset;
	if ((ret = joker_cmd(joker, buf, 2, NULL /* in_buf */, 0 /* in_len */)))
		return ret;

	/* choose TS input */
	buf[0] = J_CMD_TS_INSEL_WRITE;
	buf[1] = input;
	if ((ret = joker_cmd(joker, buf, 2, NULL /* in_buf */, 0 /* in_len */)))
		return ret;

	switch (info->delivery_system)
	{
		case JOKER_SYS_ATSC:
			fe = lgdt3306a_attach(&lgdt3306a_config, i2c);
			if (!fe) {
				printf("can't attach LGDT3306A demod\n");
				return -ENODEV;
			}
			/* attach HELENE universal tuner in TERR mode */
			helene_attach(fe, &helene_conf, i2c);
			break;
		case JOKER_SYS_DTMB:
			fe = atbm888x_attach(&atbm888x_config, i2c);
			if (!fe) {
				printf("Can't attach ATBM888x demod\n");
				return ENODEV;
			}
			/* attach HELENE universal tuner in TERR mode */
			helene_attach(fe, &helene_conf, i2c);
			break;
		case JOKER_SYS_ISDBT:
		case JOKER_SYS_DVBT:
		case JOKER_SYS_DVBT2:
		case JOKER_SYS_DVBC_ANNEX_A:
			fe = cxd2841er_attach_t_c(&demod_config, i2c);
			if (!fe) {
				printf("Can't attach SONY demod\n");
				return ENODEV;
			}
			/* attach HELENE universal tuner in TERR mode */
			helene_attach(fe, &helene_conf, i2c);
			break;
		case JOKER_SYS_DVBS:
		case JOKER_SYS_DVBS2:
			fe = cxd2841er_attach_s(&demod_config, i2c);
			if (!fe) {
				printf("Can't attach SONY demod\n");
				return ENODEV;
			}
			/* attach HELENE universal tuner in DVB-S mode */
			helene_attach_s(fe, &helene_conf, i2c);
			break;
		default:
			printf("delivery system %d not supported \n", info->delivery_system);
			return ENODEV;
	}

	fe->frontend_priv = joker;
	fe->ops.i2c_gate_ctrl = joker_i2c_gate_ctrl;
	// disable i2c gate (will be enabled later when required)
	joker_i2c_gate_ctrl(fe, 0);

	info->fe_opaque = (void *)fe;

	fe->ops.init(fe);

	/* set tune info */
	fe->dtv_property_cache.delivery_system = info->delivery_system;
	fe->dtv_property_cache.bandwidth_hz = info->bandwidth_hz;
	fe->dtv_property_cache.modulation = info->modulation;
	fe->dtv_property_cache.symbol_rate = info->symbol_rate;
	fe->dtv_property_cache.frequency = info->frequency; 

	/* enable LNB */
	if (need_lnb) {
		fe = tps65233_attach(fe, &lnb_config, i2c);
		if (!fe) {
			printf("can't attach LNB\n");
			return -1;
		}

		fe->ops.set_tone(fe, info->tone);

		while (cnt-- > 0) {
			if(!fe->ops.set_voltage(fe, info->voltage))
				break;

			sleep (1);
		}

		/* use LNB settings to calculate correct frequency */
		if (info->lnb.switchfreq) {
			if (info->frequency > info->lnb.switchfreq*1000*1000)
				fe->dtv_property_cache.frequency = info->frequency - info->lnb.highfreq*1000*1000;
			else
				fe->dtv_property_cache.frequency = info->frequency - info->lnb.lowfreq*1000*1000;
		}
		printf("final freq %d \n", fe->dtv_property_cache.frequency);
	}

	/* actual tune call */
	fe->ops.tune(fe, 1 /*re_tune*/, 0 /*flags*/, &delay, &status);


	/* TODO */
#if 0
	/* DISEQC */
	/* send diseqc message */
	fe->ops.diseqc_send_master_cmd(fe, &dcmd);
	/* set 22khz tone */
	fe->ops.set_tone(fe, SEC_TONE_ON);
#endif

	return 0;
}
