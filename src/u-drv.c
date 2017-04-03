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

/* i2c adapter is FT232H based adapter:
 * https://www.adafruit.com/product/2264
 *
 * following packages should be installed:
 * apt install libftdi-dev
 *
 * and
 * https://github.com/devttys0/libmpsse.git
 */

#include <stdio.h>
typedef unsigned char           uint8_t;
typedef unsigned short int      uint16_t;
typedef unsigned int            uint32_t;
#include <mpsse.h>

// Linux kernel header files
#include <drivers/media/dvb-frontends/helene.h>
#include <drivers/media/dvb-frontends/cxd2841er.h>
#include <drivers/media/dvb-frontends/lgdt3306a.h>
#include <drivers/media/dvb-frontends/atbm888x.h>
#include <drivers/media/dvb-core/dvb_frontend.h>
#include <drivers/media/pci/netup_unidvb/netup_unidvb.h>
#include <linux/dvb/frontend.h>
#include <linux/moduleparam.h>
#include <linux/i2c.h>

unsigned long phys_base = 0;
static struct mpsse_context *i2c_h = NULL;
const struct kernel_param_ops param_ops_int;

static struct cxd2841er_config demod_config = {
	.i2c_addr = 0xc8,
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

void __dynamic_dev_dbg(struct _ddebug *descriptor,
		const struct device *dev, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vprintf( fmt, args );
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

int i2c_start()
{
	//if((i2c_h = MPSSE(I2C, ONE_HUNDRED_KHZ, MSB)) != NULL && i2c_h->open)
	if((i2c_h = MPSSE(I2C, FOUR_HUNDRED_KHZ, MSB)) != NULL && i2c_h->open)
	{
		printf("%s initialized at %dHz (I2C)\n", GetDescription(i2c_h), GetClock(i2c_h));
		return 0;
	} else {
		printf("FAIL open I2C\n");
		return -1;
	}
}

int i2c_stop()
{
	Close(i2c_h);
	return 0;
}

int i2c_write(uint16_t addr, const uint8_t * data, uint16_t len)
{
	unsigned char cmd[len + 1];

	Start(i2c_h);
	cmd[0] = (uint8_t)addr<<1;
	for (int i = 0; i < len; i++)
		cmd[i+1] = data[i];

	Write(i2c_h, cmd, len+1);

	if(!(GetAck(i2c_h) == ACK)) {
		printf("%s: failed !\n", __func__);
		return -1;
	}

	Stop(i2c_h);
	return 0;
}

int i2c_read(uint16_t addr, const uint8_t * data_out, uint16_t len)
{
	unsigned char cmd[512] = {0};
	char *data = NULL;

	Start(i2c_h);
	cmd[0] = ((uint8_t)addr<<1) + 1;
	Write(i2c_h, cmd, 1);

	if(GetAck(i2c_h) == ACK)
	{ 
		data = Read(i2c_h, len);
		if (data) {
			memcpy(data_out, data, len);

			free(data);
		} else {
			printf("Can't get data from slave \n");
			return -1;
		}

		SendNacks(i2c_h);
		/* Read in one dummy byte, with a NACK */
		Read(i2c_h, 1);
	} else {
		printf("Can't get GetAck from slave \n");
		return -1;
	}

	Stop(i2c_h);
	return 0;
}

int i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	int i = 0;

	// printf("%s num=%d\n", __func__, num);
	for (i = 0; i < num; i++) {
		// printf("\ti=%i addr=0x%x len=%d flags=0x%x\n",
		// i, msgs[i].addr, msgs[i].len, msgs[i].flags);
		if (msgs[i].flags & I2C_M_RD) {
			if (i2c_read(msgs[i].addr, msgs[i].buf, msgs[i].len))
				return -1;
		} else {
			if (i2c_write(msgs[i].addr, msgs[i].buf, msgs[i].len))
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

int main ()
{
	struct dvb_frontend *fe = NULL;
	struct i2c_adapter i2c;
	struct vb2_dvb_frontend *fes[2];
	int fe_count = 2;
	int i = 0, num = 0;
	struct netup_unidvb_dev *ndev = NULL;
	enum fe_status status;
	unsigned int delay = 0;

	if (i2c_start())
		return -1;

#if 0
	/* DVB-C */
	// fe = cxd2841er_attach_t_c(&demod_config, &i2c);
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
	}
#endif

#if 0
	/* ATSC */
	fe = lgdt3306a_attach(&lgdt3306a_config, &i2c);
	if (!fe) {
		printf("can't attach demod\n");
		return -1;
	}

	helene_attach(fe, &helene_conf, &i2c);

	fe->ops.init(fe);
	/* set tune info */
	fe->dtv_property_cache.delivery_system = SYS_ATSC;
	fe->dtv_property_cache.bandwidth_hz = 6000000;
	fe->dtv_property_cache.frequency = 575000000; /* freq in Hz */
	fe->dtv_property_cache.modulation = VSB_8;
	// fe->ops.tune(fe, 1 /*re_tune*/, 0 /*flags*/, &delay, &status);
	fe->ops.search(fe);
	printf("status=0x%x \n", status);

	while (1) {
		fe->ops.read_status(fe, &status);
		printf("status=0x%x \n", status);
		sleep(1);
	}
#endif

#if 1
	/* DTMB */
	fe = atbm888x_attach(&atbm888x_config, &i2c);
	if (!fe) {
		printf("can't attach demod\n");
		return -1;
	}

	helene_attach(fe, &helene_conf, &i2c);

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

	i2c_stop();
	printf("done\n");
}
