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
#include <drivers/media/pci/netup_unidvb/netup_unidvb.h>
#include <linux/dvb/frontend.h>
#include <linux/moduleparam.h>
#include <time.h>
#include <linux/i2c.h>
#include "joker_i2c.h"
#include "joker_fpga.h"
#include "u_drv_tune.h"

unsigned long phys_base = 0;
const struct kernel_param_ops param_ops_int;

static struct tps65233_config lnb_config = {
	.i2c_address = 0x60,
	.data2_config = 0
};

static struct cxd2841er_config demod_config = {
	.i2c_addr = 0xc8,
	.ts_mode = SONY_TS_SERIAL,
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

int printk(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vprintf( fmt, args );
	va_end(args);
	return 0;
}

/* uncomment if you want to see dev_dbg messages */
void __dynamic_dev_dbg(struct _ddebug *descriptor,
		const struct device *dev, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	// vprintf( fmt, args );
	va_end(args);
	return 0;
}

void *__kmalloc(size_t size, gfp_t flags)
{
	return malloc(size);
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
	return 0;
}

void _dev_info(const struct device *dev, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vprintf( fmt, args );
	va_end(args);
	return 0;
}

void dev_warn(const struct device *dev, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vprintf( fmt, args );
	va_end(args);
	return 0;
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
	printf("%s min=%d max=%d\n", 
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
 * return 0 if LOCKed 
 * return -EAGAIN if NOLOCK
 * return negative error code if error
 */
int read_status(struct tune_info_t *info)
{
	enum fe_status status;
	struct dvb_frontend *fe = (struct dvb_frontend *)info->fe_opaque;

  if (!fe)
    return EINVAL;

  fe->ops.read_status(fe, &status);
  printf("%s: status=0x%x \n", __func__, status);

  if (status == 0x1f)
    return 0;

  return EAGAIN;
}

/* return signal strength
 * range 0x0000 - 0xffff
 * 0x0000 - weak signal
 * 0xffff - stong signal
 * */
int read_signal(struct tune_info_t *info)
{
	u16 strength = 0;
	struct dvb_frontend *fe = (struct dvb_frontend *)info->fe_opaque;

  if (!fe)
    return -EINVAL;

  fe->ops.read_signal_strength(fe, &strength);
  printf("strength=0x%x \n", strength);

  return (int)strength;
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
	struct i2c_adapter i2c;
	struct vb2_dvb_frontend *fes[2];
	int fe_count = 2;
	int i = 0, num = 0;
	struct netup_unidvb_dev *ndev = NULL;
	unsigned int delay = 0;
	int ret = 0;
	unsigned char buf[BUF_LEN];

	struct dvb_diseqc_master_cmd dcmd = {
		.msg = {0xFF},
		.msg_len = 6
	};

  if (!joker || !joker->i2c_opaque)
    return EINVAL;

  i2c.algo_data = (void*)joker;

	/* reset tuner and demods */
	if (joker_write_off(joker, OC_I2C_RESET_CTRL, \
				OC_I2C_RESET_GATE | OC_I2C_RESET_TPS_CI | OC_I2C_RESET_TPS | OC_I2C_RESET_USB))
		return -EIO;

	msleep(10);

	if (joker_write_off(joker, OC_I2C_RESET_CTRL, \
				OC_I2C_RESET_GATE | OC_I2C_RESET_TPS_CI | OC_I2C_RESET_TPS | OC_I2C_RESET_USB | \
				OC_I2C_RESET_TUNER | OC_I2C_RESET_LG ))
				// OC_I2C_RESET_TUNER | OC_I2C_RESET_SONY ))
		return -EIO;

	/* choose TS input */
	// if (oc_i2c_write_off(dev, OC_I2C_INSEL_CTRL, OC_I2C_INSEL_SONY ))
	if (joker_write_off(joker, OC_I2C_INSEL_CTRL, OC_I2C_INSEL_LG))
		return -EIO;

#if 0
	/* DVB-S/S2 */
	fe = cxd2841er_attach_s(&demod_config, &i2c);
	if (!fe) {
		printf("can't attach DVB-S demod\n");
		return -1;
	}

	helene_attach_s(fe, &helene_conf, &i2c);

	fe->ops.init(fe);

	/* set tune info */
	fe->dtv_property_cache.delivery_system = SYS_DVBS;
	fe->dtv_property_cache.frequency = 1216000000; /* freq in Hz */
	fe->dtv_property_cache.bandwidth_hz = 0; /* 0 - AUTO */
	fe->dtv_property_cache.symbol_rate = 22000000; /* symbols/sec */
	fe->ops.tune(fe, 1 /*re_tune*/, 0 /*flags*/, &delay, &status);
	printf("status=0x%x \n", status);

	/* enable LNB */
	fe = tps65233_attach(fe, &lnb_config, &i2c);
	if (!fe) {
		printf("can't attach LNB\n");
		return -1;
	}

#if 0
	ret = fe->ops.set_voltage(fe, SEC_VOLTAGE_OFF);
	printf("lnb status 0x%x \n", ret);
	if (ret != 0x23 && ret != 0x21) {
		printf("Failed to set LNB voltage !\n");
		return -1;
	}
#endif

	while (1) {
		fe->ops.read_status(fe, &status);
		fe->ops.read_signal_strength(fe, &strength);
		// ret = fe->ops.set_voltage(fe, SEC_VOLTAGE_OFF);
		printf("status=0x%x strength=0x%x LNB status=0x%x\n",
				status, strength, ret);
		sleep(1);
	}
#endif

#if 0
	/* DISEQC */
	/* send diseqc message */
	fe->ops.diseqc_send_master_cmd(fe, &dcmd);
	/* set 22khz tone */
	fe->ops.set_tone(fe, SEC_TONE_ON);
#endif

#if 0
	/* DVB-C */
	fe = cxd2841er_attach_c(&demod_config, &i2c);
	if (!fe) {
		printf("can't attach demod\n");
		return -1;
	}

	helene_attach(fe, &helene_conf, &i2c);

	fe->ops.init(fe);

	/* set tune info */
	fe->dtv_property_cache.delivery_system = SYS_DVBC_ANNEX_A;
	fe->dtv_property_cache.bandwidth_hz = 8000000;
	fe->dtv_property_cache.frequency = 150000000; /* freq in Hz */
	//fe->dtv_property_cache.frequency = 50000000; /* freq in Hz */
	fe->ops.tune(fe, 1 /*re_tune*/, 0 /*flags*/, &delay, &status);
	printf("status=0x%x \n", status);

	while (1) {
		fe->ops.read_status(fe, &status);
		printf("status=0x%x \n", status);
		sleep(1);
		fflush(stdout);

		if (status == 0x1f) {
			process_ts(bp);
		}
	}
#endif

#if 1
	/* ATSC */
	fe = lgdt3306a_attach(&lgdt3306a_config, &i2c);
	if (!fe) {
		printf("can't attach demod\n");
		return -ENODEV;
	}
  info->fe_opaque = (void *)fe;

	helene_attach(fe, &helene_conf, &i2c);

	fe->ops.init(fe);
	/* set tune info */
	fe->dtv_property_cache.delivery_system = SYS_ATSC;
	fe->dtv_property_cache.bandwidth_hz = 6000000;
	// fe->dtv_property_cache.frequency = 533000000; /* studio city: freq in Hz */
	// fe->dtv_property_cache.frequency = 551000000; /* lost hills: freq in Hz */
	// fe->dtv_property_cache.frequency = 485000000; /* lv: freq in Hz */
	fe->dtv_property_cache.frequency = 575000000; /* miami. freq in Hz */
	fe->dtv_property_cache.modulation = VSB_8;
	// fe->ops.tune(fe, 1 /*re_tune*/, 0 /*flags*/, &delay, &status);
	fe->ops.search(fe);
#if 0
	printf("Tune done. LG status=0x%x \n", status);
	while (info->timeout) {
		fe->ops.read_status(fe, &status);
		fe->ops.read_signal_strength(fe, &strength);
		printf("status=0x%x strength=0x%x\n", status, strength);
		sleep(1);
		fflush(stdout);

		if (status == 0x1f) {
			// process_ts(bp);
			// sleep (1);
		}
	}
#endif
#endif

#if 0
	/* DTMB */
	fe = atbm888x_attach(&atbm888x_config, &i2c);
	if (!fe) {
		printf("can't attach demod\n");
		return -1;
	}

	helene_attach(fe, &helene_conf, &i2c);
#endif

#if 0
	// fe->ops.sleep(fe);
	// fe->ops.init(fe);
	/* set tune info */
	fe->dtv_property_cache.delivery_system = SYS_DTMB;
	fe->dtv_property_cache.bandwidth_hz = 8000000;
	fe->dtv_property_cache.frequency = 650000000; /* freq in Hz */
	fe->ops.set_frontend(fe);

	sleep(2);
	fe->ops.read_status(fe, &status);
	printf("first status=0x%x \n", status);
	sleep(2);

	while (1) {
		fe->ops.read_status(fe, &status);
		printf("status=0x%x \n", status);
		sleep(1);
	}
#endif

	return 0;
}
