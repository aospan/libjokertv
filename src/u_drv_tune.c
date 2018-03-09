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
#include <drivers/media/dvb-frontends/cxd2841er_blind_scan.h>
#include <drivers/media/dvb-frontends/lgdt3306a.h>
#include <drivers/media/dvb-frontends/atbm888x.h>
#include <drivers/media/dvb-frontends/tps65233.h>
#include <drivers/media/dvb-core/dvb_frontend.h>
#include <linux/dvb/frontend.h>
#include <time.h>
#include <linux/i2c.h>
#include <sys/time.h>
#include "pthread.h"
#include "joker_i2c.h"
#include "joker_fpga.h"
#include "u_drv_tune.h"
#include "joker_blind_scan.h"

static int joker_i2c_gate_ctrl(struct dvb_frontend *fe, int enable);

struct service_thread_opaq_t
{
	/* service thread for periodic tasks */
	pthread_t service_thread;
	pthread_cond_t cond;
	pthread_mutex_t mux;
	int cancel;
};

/* service thread for periodic tasks */
void* process_service(void * data) {
	struct joker_t * joker = (struct joker_t *)data;
	struct timespec ts;
	struct timeval now;
	int rc = 0;
	enum fe_status status = 0;
	struct dvb_frontend *fe = NULL;
	int last_lnb_check = time(0);
	uint64_t new_nsec = 0;

	if (!joker) {
		printf("%s: invalid args \n", __func__ );
		return -EINVAL;
	}

	printf("process_service started \n");
	while(!joker->service_threading->cancel) {
		// wait until refresh not enabled
		pthread_mutex_lock(&joker->service_threading->mux);
		while (!joker->stat.refresh_enable
				&& !joker->service_threading->cancel)
			pthread_cond_wait(&joker->service_threading->cond,
					&joker->service_threading->mux);

		// exit if cancelled
		if (joker->service_threading->cancel) {
			pthread_mutex_unlock(&joker->service_threading->mux);
			break;
		}

		// get status
		fe = (struct dvb_frontend *)joker->fe_opaque;
		joker->stat.status = _read_status(joker);
		jdebug("%s: status=0x%x \n", __func__, status);

		// get statistics
		_read_signal_stat(joker, &joker->stat);

		// control LNB health, not too often
		if ((time(0) - last_lnb_check) > LNB_HEALTH_INTERVAL) {
			/* this call will update LNB settings
			 * if they lost during LNB chip reset (because of power failure)
			 *
			 * ops.set_voltage return different error codes for different error conditions 
			 * -ENFILE for "LNB output voltage out of range"
			 * -ERANGE for "Output current less than 50 mA"
			 * -EMFILE for "Overcurrent protection triggered"
			 *  0 if no errors detected
			 */
			if(fe->ops.set_voltage) {
				joker->stat.lnb_err = fe->ops.set_voltage(fe, joker->info->voltage);
			}
			last_lnb_check = time(0);
		}

		// call callback
		if (joker->status_callback)
			joker->status_callback(joker);

		pthread_mutex_unlock(&joker->service_threading->mux);

		// wait signal or timeout
		pthread_mutex_lock(&joker->service_threading->mux);
		gettimeofday(&now,NULL);
		new_nsec = (uint64_t)(joker->stat.refresh_ms/1000 + now.tv_sec) * 1000UL * 1000UL * 1000UL
			+ (1000UL*(uint64_t)(joker->stat.refresh_ms%1000) + now.tv_usec) * 1000UL;
		ts.tv_sec = new_nsec/(1000UL * 1000UL * 1000UL);
		ts.tv_nsec = (new_nsec % (1000UL * 1000UL * 1000UL));
		jdebug("now sec=%d usec=%d new_nsec=%llu\n", now.tv_sec, now.tv_usec, new_nsec);
		rc = pthread_cond_timedwait(&joker->service_threading->cond,
				&joker->service_threading->mux, &ts);
		jdebug("timedwait rc=%d \n", rc);
		pthread_mutex_unlock(&joker->service_threading->mux);
	}
	printf("%s: bye !\n", __func__);
}

// fast stop of service thread
int stop_service_thread(struct joker_t * joker)
{
	int ret = 0;

	if (!joker || !joker->service_threading)
		return -EINVAL;

	// fast stop of service thread
	joker->service_threading->cancel = 1;
	pthread_cond_signal(&joker->service_threading->cond);
	ret = pthread_join(joker->service_threading->service_thread, NULL);
}

unsigned long phys_base = 0;

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

void msleep_msecs(unsigned int msecs)
{
	usleep(1000*msecs);
}
#define msleep(x) msleep_msecs(x);

u64 ktime_get_ns(void)
{
	return (u64)1000 * getus();
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
int _read_status(struct joker_t *joker)
{
	enum fe_status status;
	struct dvb_frontend *fe = (struct dvb_frontend *)joker->fe_opaque;

	if (!fe)
		return -EINVAL;

	fe->ops.read_status(fe, &status);
	jdebug("%s: status=0x%x \n", __func__, status);

	if (status == 0x1f)
		return JOKER_LOCK;

	return JOKER_NOLOCK;
}

/* safe read_status for external use */
int read_status(struct joker_t *joker)
{
	int ret = 0;
	pthread_mutex_lock(&joker->service_threading->mux);
	ret = _read_status(joker);
	pthread_mutex_unlock(&joker->service_threading->mux);
	return ret;
}

/* Read all stats related to receiving signal
 * RF level
 * SNR (CNR)
 * Quality
 *
 * return 0 if success
 * other values is errors */
int _read_signal_stat(struct joker_t *joker, struct stat_t *stat)
{
	struct dvb_frontend *fe = (struct dvb_frontend *)joker->fe_opaque;
	struct dtv_frontend_properties *prop = &fe->dtv_property_cache;
	uint8_t ifagcreg = 0, rfagcreg = 0, if_bpf_gain = 0;
	int32_t rssi = 0;

	if (!fe)
		return -EINVAL;

	// special case for LGDT3306A to read SNR, etc
	if (prop->delivery_system == JOKER_SYS_ATSC || 
			prop->delivery_system == JOKER_SYS_DVBC_ANNEX_B)
	{
		u16 snr = 0;
		fe->ops.read_snr(fe, &snr);
		stat->snr = 100 * snr;

		u32 ber = 0;
		fe->ops.read_ber(fe, &ber);
		stat->bit_error = ber;
		stat->bit_count = 1;

		u32 ucblocks = 0;
		fe->ops.read_ucblocks(fe, &ucblocks);
		stat->ucblocks = ucblocks;
	} else {
		// cleanup previous readings
		prop->strength.stat[0].uvalue = 0;
		prop->cnr.stat[0].svalue = 0;
		prop->block_error.stat[0].uvalue = 0;

		// read all stats from frontend
		fe->ops.get_frontend(fe, prop);

		/* transfer values to stat_t */
		stat->rf_level = (int32_t)prop->strength.stat[0].uvalue;
		stat->snr = (int32_t)prop->cnr.stat[0].svalue;
		stat->ucblocks = prop->block_error.stat[0].uvalue;
		stat->bit_error = prop->post_bit_error.stat[0].uvalue;
		stat->bit_count = prop->post_bit_count.stat[0].uvalue;
	}


	// if we have special method to read RSSI from tuner
	// overwrite values obtained from demod then
	if (fe->ops.tuner_ops.get_rssi) {
		joker_i2c_gate_ctrl(fe, 1);
		fe->ops.tuner_ops.get_rssi(fe, &rssi);
		joker_i2c_gate_ctrl(fe, 0);
		prop->strength.stat[0].uvalue = rssi;
		stat->rf_level = (int32_t)prop->strength.stat[0].uvalue;
	}

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

/* safe read_signal_stat for external use */
int read_signal_stat(struct joker_t *joker, struct stat_t *stat)
{
	int ret = 0;
	pthread_mutex_lock(&joker->service_threading->mux);
	ret = _read_signal_stat(joker, stat);
	pthread_mutex_unlock(&joker->service_threading->mux);
	return ret;
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

	jdebug("%s: enable=%d \n", __func__, enable);
	if (enable)
		joker_unreset(joker, OC_I2C_RESET_GATE);
	else
		joker_reset(joker, OC_I2C_RESET_GATE);

	return 0;
}

int set_refresh(struct joker_t *joker, int enable)
{
	if (!joker || !joker->service_threading)
		return -EINVAL;

	pthread_mutex_lock(&joker->service_threading->mux);
	joker->stat.refresh_enable = enable;
	pthread_cond_signal(&joker->service_threading->cond);
	pthread_mutex_unlock(&joker->service_threading->mux);

	return 0;
}

/* set LNB voltage directly to chip 
 * return:
 * -ENFILE for "LNB output voltage out of range"
 * -ERANGE for "Output current less than 50 mA"
 * -EMFILE for "Overcurrent protection triggered"
 *  0 if no errors detected
 */
int set_lnb_voltage(struct joker_t * joker, enum joker_fe_sec_voltage voltage)
{
	struct dvb_frontend *fe = NULL;

	if (!joker || !joker->fe_opaque)
		return -EINVAL;
	fe = (struct dvb_frontend *)joker->fe_opaque;

	if(fe->ops.set_voltage) {
		pthread_mutex_lock(&joker->service_threading->mux);
		joker->stat.lnb_err = fe->ops.set_voltage(fe, voltage);
		joker->info->voltage = voltage;
		pthread_mutex_unlock(&joker->service_threading->mux);
		printf("LNB setting voltage = %d\n", joker->info->voltage);
		return joker->stat.lnb_err;
	} else {
		return -EINVAL;
	}
}

/* send diseqc messages
 * len valid values are 3...6
 * return
 *  0 if no errors detected
 */
int send_diseqc_message(struct joker_t * joker, char * message, int len)
{
	struct dvb_frontend *fe = NULL;
	struct dvb_diseqc_master_cmd cmd;
	int i = 0;

	if (!joker || !joker->fe_opaque || len < 3 || len > 6)
		return -EINVAL;

	fe = (struct dvb_frontend *)joker->fe_opaque;

	if(fe->ops.diseqc_send_master_cmd ) {
		for (i = 0; i < len; i++)
			cmd.msg[i] = message[i];
		cmd.msg_len = len;
		if (fe->ops.diseqc_send_master_cmd(fe, &cmd))
			return -EIO;
	}
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
	int i = 0, num = 0, lo_freq;
	struct netup_unidvb_dev *ndev = NULL;
	unsigned int delay = 0;
	int ret = 0;
	unsigned char buf[BUF_LEN];
	int input = 0, need_lnb = 0, rc = 0;
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

	// save tuning parameters for later use
	if (!joker->info) {
		joker->info = malloc(sizeof(struct tune_info_t));
		if (!joker->info)
			return -ENOMEM;
	}
	memcpy(joker->info, info, sizeof(*info));

	/* start service thread to monitor status (lock), etc */
	if (!joker->service_threading) {
		joker->service_threading = malloc(sizeof(*joker->service_threading));
		memset(joker->service_threading, 0, sizeof(*joker->service_threading));
		pthread_mutex_init(&joker->service_threading->mux, NULL);
		pthread_cond_init(&joker->service_threading->cond, NULL);
		memset(&joker->stat, 0, sizeof(joker->stat));
		joker->stat.refresh_ms = 200; // initial interval is 200 msec
		joker->stat.refresh_enable = 0; // enable later 

		pthread_attr_t attrs;
		pthread_attr_init(&attrs);
		pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_JOINABLE);
		rc = pthread_create(&joker->service_threading->service_thread, &attrs, process_service, (void *)joker);
		if (rc){
			printf("ERROR: can't start service thread. code=%d\n", rc);
			return rc;
		}
	}
	joker->stat.status = JOKER_NOLOCK;

	joker_clean_ts(joker); // clean FIFO from previous TS

	// pause service thread while we configure frontend
	pthread_mutex_lock(&joker->service_threading->mux);
	joker->stat.refresh_enable = 0;
	pthread_cond_signal(&joker->service_threading->cond);
	pthread_mutex_unlock(&joker->service_threading->mux);

	i2c->algo_data = (void*)joker;

	switch (info->delivery_system)
	{
		case JOKER_SYS_ATSC:
		case JOKER_SYS_DVBC_ANNEX_B:
			input = J_INSEL_LG;
			joker_unreset(joker, OC_I2C_RESET_GATE | OC_I2C_RESET_TUNER | OC_I2C_RESET_LG);
			break;
		case JOKER_SYS_DTMB:
			joker_unreset(joker, OC_I2C_RESET_GATE | OC_I2C_RESET_TUNER | OC_I2C_RESET_ATBM);
			input = J_INSEL_ATBM;
			break;
		case JOKER_SYS_DVBS:
		case JOKER_SYS_DVBS2:
			need_lnb = 1;
		case JOKER_SYS_DVBC_ANNEX_A:
		case JOKER_SYS_DVBT:
		case JOKER_SYS_DVBT2:
		case JOKER_SYS_ISDBT:
			joker_unreset(joker, OC_I2C_RESET_GATE | OC_I2C_RESET_TUNER | OC_I2C_RESET_SONY);
			input = J_INSEL_SONY;
			break;
		default:
			printf("delivery system %d not supported \n", info->delivery_system);
			return ENODEV;
	}

	msleep(50); /* wait chips to wakeup after reset */

	/* choose TS input */
	buf[0] = J_CMD_TS_INSEL_WRITE;
	buf[1] = input;
	if ((ret = joker_cmd(joker, buf, 2, NULL /* in_buf */, 0 /* in_len */)))
		return ret;

	switch (info->delivery_system)
	{
		case JOKER_SYS_ATSC:
		case JOKER_SYS_DVBC_ANNEX_B:
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
	joker_i2c_gate_ctrl(fe, 1);

	joker->fe_opaque = (void *)fe;

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

		// do not make actual tune when blind scanning
		if (joker->blind_scan)
			return 0;

		/* use LNB settings to calculate correct frequency */
		if (info->lnb.switchfreq) {
			if (info->frequency / 1000 > info->lnb.switchfreq * 1000) {
				lo_freq = info->lnb.highfreq * 1000;
				// switch to high band enabling 22kHz tone
				info->tone = JOKER_SEC_TONE_ON;
			} else {
				lo_freq = info->lnb.lowfreq * 1000;
			}
		} else {
			lo_freq = info->lnb.lowfreq * 1000;
		}
		fe->dtv_property_cache.frequency = abs(info->frequency / 1000 - lo_freq) * 1000;

		fe->ops.set_tone(fe, info->tone);
		fe->ops.set_voltage(fe, info->voltage);

		printf("Channel freq %.2f MHz, LO %.2f MHz, L-Band freq %.2f MHz 22kHz tone %s\n",
				info->frequency / 1000000., lo_freq / 1000., fe->dtv_property_cache.frequency / 1000000.,
				(info->tone == JOKER_SEC_TONE_ON) ? "On":"Off");
	}

 	/* actual tune call */
	fe->ops.tune(fe, 1 /*re_tune*/, 0 /*flags*/, &delay, &status);
	joker_i2c_gate_ctrl(fe, 0);

	// now wakeup service thread
	jdebug("Wakeup service thread \n");
	pthread_mutex_lock(&joker->service_threading->mux);
	joker->stat.refresh_enable = 1;
	pthread_cond_signal(&joker->service_threading->cond);
	pthread_mutex_unlock(&joker->service_threading->mux);

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
