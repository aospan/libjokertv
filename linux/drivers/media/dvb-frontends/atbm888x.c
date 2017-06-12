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

#include <asm/div64.h>
#include "dvb_frontend.h"

#include "atbm888x.h"
#include "atbm888x_priv.h"

uint8_t ui8AATBM888XCommonReg[]=
{
        0x02, 0x45, 0x33,
        0x02, 0x4a, 0x96,
        0x02, 0xc6, 0x00,
        0x02, 0xc7, 0x01
};

uint8_t ui8AATBM888XDtmbSet[]=
{
        0x02, 0x28, 0x01,
        0x02, 0xa6, 0x10,
        0x02, 0xa9, 0x60,
        0x02, 0xcb, 0x68,
        0x02, 0xcc, 0x70,
        0x02, 0x49, 0x30
};

uint8_t ui8AATBM888XDtmb24MSet[]=
{
        0x02, 0xC5, 0x15,
        0x02, 0x4E, 0x10,
        0x02, 0x3E, 0x28,
        0x02, 0x3B, 0x28,
        0x0A, 0xFB, 0x01,
        0x02, 0x3C, 0x1C,
        //0x13, 0x4e, 0x00,
        //0x13, 0x75, 0x00
};

uint8_t ui8ADtmbInternal[]=
{
        0x00, 0x16, 0x1c, //DTMB mode   
        0x09, 0x88, 0x08,
        0x09, 0x89, 0x0c,
        0x09, 0x9a, 0x40,
        0x09, 0x35,0x14,
        0x0a, 0xf9, 0x00,
        0x0e, 0x01, 0x08,
        0x08, 0x52, 0x28,
        0x0c, 0x24, 0x0a,
        0x0c, 0x26, 0x0a
};

uint8_t uiAATBMDynamicSetting[]={
        0x09, 0x88, 0x08,
        0x09, 0x89, 0x08,
        0x09, 0x8b, 0x08,
        0x09, 0x04, 0x01,
        0x09, 0x05, 0x02,
        0x09, 0x57, 0x00,
        0x09, 0x5a, 0x02,
        0x09, 0x59, 0x40,
        0x09, 0x5b, 0x40,
        0x14, 0x47, 0xc0,
        0x13, 0x55, 0x00,
        0x08, 0x6b, 0x00,//staic cfo c1  2015.1.28
        0x08, 0x6d, 0x10,//static cfo c2
        0x08, 0x8c, 0x00,//dy cfo c1
        0x08, 0x8e, 0x10,//dy cfo c2
        0x14, 0x47, 0x80//channel alpha filter
};

static int atbm888x_write_reg(struct atbm_state *priv, u16 reg, u8 data)
{
	int ret = 0;
	u8 dev_addr;
	u8 buf1[] = { reg >> 8, reg & 0xFF };
	u8 buf2[] = { data };
	struct i2c_msg msg1 = { .flags = 0, .buf = buf1, .len = 2 };
	struct i2c_msg msg2 = { .flags = 0, .buf = buf2, .len = 1 };

	dev_addr = priv->config->demod_address;
	msg1.addr = dev_addr;
	msg2.addr = dev_addr;

	dev_dbg(&priv->i2c->dev, "%s: reg=0x%04X, data=0x%02X\n", __func__, reg, data);

	ret = i2c_transfer(priv->i2c, &msg1, 1);
	if (ret != 1)
		return -EIO;

	ret = i2c_transfer(priv->i2c, &msg2, 1);
	return (ret != 1) ? -EIO : 0;
}

static int atbm888x_write_reg_array(struct atbm_state *priv, uint8_t *table, int len)
{
	int i;
	dev_dbg(&priv->i2c->dev, "%s: len=%d\n", __func__, len);
	for(i = 0;i < len; i +=3 )
	{
		if((i + 2) < len)
		{
			atbm888x_write_reg(priv, (((u16)table[i]) << 8 | table[i+1]), table[i+2]);
		}
	}
}

static int atbm888x_read_reg(struct atbm_state *priv, u16 reg, u8 *p_data)
{
	int ret;
	u8 dev_addr;

	u8 buf1[] = { reg >> 8, reg & 0xFF };
	u8 buf2[] = { 0 };
	struct i2c_msg msg1 = { .flags = 0, .buf = buf1, .len = 2 };
	struct i2c_msg msg2 = { .flags = I2C_M_RD, .buf = buf2, .len = 1 };

	dev_addr = priv->config->demod_address;
	msg1.addr = dev_addr;
	msg2.addr = dev_addr;

	ret = i2c_transfer(priv->i2c, &msg1, 1);
	if (ret != 1) {
		dev_err(&priv->i2c->dev, "%s: error reg=0x%04x, ret=%i\n", __func__, reg, ret);
		return -EIO;
	}

	ret = i2c_transfer(priv->i2c, &msg2, 1);
	if (ret != 1)
		return -EIO;

	*p_data = buf2[0];
  dev_dbg(&priv->i2c->dev, "%s: reg=0x%04X, data=0x%02X\n",
      __func__, reg, buf2[0]);

	return 0;
}

u8 atbm888x_chip_id(struct atbm_state *priv)
{
	u8 data = 0;
	struct i2c_adapter *i2c = priv->i2c;

	if (atbm888x_read_reg(priv, REG_CHIP_ID, &data) != 0) {
		dev_err(&i2c->dev, "%s atbm888x not found at i2c addr 0x%02X\n",
			__func__, priv->config->demod_address);
		return 0;
	}
	dev_dbg(&i2c->dev, "atbm888x chip id: 0x%02X\n", data);
	return data;
}

/* Lock register latch so that multi-register read is atomic */
static inline int atbm888x_reglatch_lock(struct atbm_state *priv, int lock)
{
	return atbm888x_write_reg(priv, REG_READ_LATCH, lock ? 1 : 0);
}

static int set_osc_freq(struct atbm_state *priv, u32 freq /*in kHz*/)
{
	u32 val;
	u64 t;
  /* TODO: calculate it 
   * this value calculated using
   * 24MHz xtal and 8MHz bw
  */
  int32_t i32RateRatioTmp = 0xca1af;

	/* 0x100000 * freq / 30.4MHz */
	// t = (u64)0x100000 * freq;
	// do_div(t, 30400);
	// val = t;

	// atbm888x_write_reg(priv, REG_OSC_CLK, val);
	// atbm888x_write_reg(priv, REG_OSC_CLK + 1, val >> 8);
	// atbm888x_write_reg(priv, REG_OSC_CLK + 2, val >> 16);
	atbm888x_write_reg(priv, REG_OSC_CLK, (i32RateRatioTmp)&0xFF);
	atbm888x_write_reg(priv, REG_OSC_CLK + 1, (i32RateRatioTmp >> 8)&0xFF);
	atbm888x_write_reg(priv, REG_OSC_CLK + 2, (i32RateRatioTmp >> 16)&0x1F);

	return 0;
}

static int set_if_freq(struct atbm_state *priv, u32 freq /*in kHz*/)
{

	u32 fs = priv->config->osc_clk_freq;
	u64 t;
	u32 val;
	u8 dat;

	if (freq != 0) {
#if 0
		/* 2 * PI * (freq - fs) / fs * (2 ^ 22) */
		t = (u64) 2 * 31416 * (freq - fs);
		t <<= 22;
		do_div(t, fs);
		do_div(t, 1000);
		val = t;
#endif
    /* Low IF */
		/* 2 * PI * (freq ) / fs * (2 ^ 22) */
		t = (u64) 2 * 3141 * freq;
		t <<= 22;
		do_div(t, fs);
		do_div(t, 1000);
		val = t;

		printk("val=0x%x \n", val);

		// atbm888x_write_reg(priv, REG_TUNER_BASEBAND, 1);
		atbm888x_write_reg(priv, REG_IF_FREQ, val);
		atbm888x_write_reg(priv, REG_IF_FREQ+1, val >> 8);
		atbm888x_write_reg(priv, REG_IF_FREQ+2, val >> 16);

	} else {
		/* Zero IF */
		atbm888x_write_reg(priv, REG_TUNER_BASEBAND, 0);

		atbm888x_read_reg(priv, REG_ADC_CONFIG, &dat);
		dat &= 0xFC;
		dat |= 0x02;
		atbm888x_write_reg(priv, REG_ADC_CONFIG, dat);

		if (priv->config->zif_swap_iq)
			atbm888x_write_reg(priv, REG_SWAP_I_Q, 0x03);
		else
			atbm888x_write_reg(priv, REG_SWAP_I_Q, 0x01);
	}

	return 0;
}

static int is_locked(struct atbm_state *priv, u8 *locked)
{
	u8 status;

	atbm888x_read_reg(priv, REG_LOCK_STATUS, &status);

	if (locked != NULL)
		*locked = (status == 1);
	return 0;
}

static int set_agc_config(struct atbm_state *priv,
	u8 min, u8 max, u8 hold_loop)
{
	/* no effect if both min and max are zero */
	if (!min && !max)
	    return 0;

	atbm888x_write_reg(priv, REG_AGC_MIN, min);
	atbm888x_write_reg(priv, REG_AGC_MAX, max);
	atbm888x_write_reg(priv, REG_AGC_HOLD_LOOP, hold_loop);

	return 0;
}

static int set_static_channel_mode(struct atbm_state *priv)
{
	int i;

	for (i = 0; i < 5; i++)
		atbm888x_write_reg(priv, 0x099B + i, 0x08);

	atbm888x_write_reg(priv, 0x095B, 0x7F);
	atbm888x_write_reg(priv, 0x09CB, 0x01);
	atbm888x_write_reg(priv, 0x09CC, 0x7F);
	atbm888x_write_reg(priv, 0x09CD, 0x7F);
	atbm888x_write_reg(priv, 0x0E01, 0x20);

	/* For single carrier */
	atbm888x_write_reg(priv, 0x0B03, 0x0A);
	atbm888x_write_reg(priv, 0x0935, 0x10);
	atbm888x_write_reg(priv, 0x0936, 0x08);
	atbm888x_write_reg(priv, 0x093E, 0x08);
	atbm888x_write_reg(priv, 0x096E, 0x06);

	/* frame_count_max0 */
	atbm888x_write_reg(priv, 0x0B09, 0x00);
	/* frame_count_max1 */
	atbm888x_write_reg(priv, 0x0B0A, 0x08);

	return 0;
}

static int set_ts_config(struct atbm_state *priv)
{
	const struct atbm888x_config *cfg = priv->config;

	/*Set parallel/serial ts mode*/
	atbm888x_write_reg(priv, REG_TS_SERIAL, cfg->serial_ts ? 1 : 0);
	atbm888x_write_reg(priv, REG_TS_CLK_MODE, cfg->serial_ts ? 1 : 0);
	/*Set ts sampling edge*/
  /* 0x00: rising edge TS output; 0x01: falling edge TS output */
	atbm888x_write_reg(priv, REG_TS_SAMPLE_EDGE,
		cfg->ts_sampling_edge ? 1 : 0);
	/*Set ts clock freerun*/
	atbm888x_write_reg(priv, REG_TS_CLK_FREERUN,
		cfg->ts_clk_gated ? 0 : 1);
	atbm888x_write_reg(priv, REG_TS_SPI_MSB_ON_DATA_BIT,
		cfg->ts_data_bit == 7 ? 1 : 0);

	return 0;
}

static int atbm888x_wakeup(struct dvb_frontend *fe)
{
	struct atbm_state *priv = fe->demodulator_priv;
	uint8_t ui8ADCPD,ui8TSEdge,ui8RefStandby,ui8ADCRfv,ui8GPORge;
	uint8_t u8ChipVersion;
	atbm888x_read_reg(priv, 0x0022,&u8ChipVersion);

	atbm888x_read_reg(priv, 0x0600,&ui8RefStandby);
	atbm888x_read_reg(priv, 0x0602,&ui8ADCPD);
	atbm888x_read_reg(priv, 0x0301,&ui8TSEdge);
	if((u8ChipVersion&0xff)!=0xf0)
	{   
		atbm888x_read_reg(priv, 0x060d,&ui8ADCRfv);
	}
	atbm888x_read_reg(priv, 0x10f7,&ui8GPORge);
	ui8RefStandby  &= 0xfd;  //bit1 set 0
	ui8ADCPD       &= 0xfe;     //bit0 set 0
	ui8TSEdge      &= 0xfd;    //bit1 set 0    
	if((u8ChipVersion&0xff)!=0xf0)
	{   
		ui8ADCRfv      &= 0xf7;
	}
	ui8GPORge      &= 0xfe; //bit 0 set 0
	atbm888x_write_reg(priv, 0x0604, 0x00); //open PLL  
	atbm888x_write_reg(priv, 0x0600, ui8RefStandby);
	atbm888x_write_reg(priv, 0x0602, ui8ADCPD);
	atbm888x_write_reg(priv, 0x1500, 0x01);
	atbm888x_write_reg(priv, 0x0301, ui8TSEdge); //all TS output PINs will be in normal mode     
	atbm888x_write_reg(priv, 0x10f7, ui8GPORge);
	if((u8ChipVersion&0xff)!=0xf0)
	{   
		atbm888x_write_reg(priv, 0x060d, ui8ADCRfv);
	}
	atbm888x_write_reg(priv, 0x0019, 0x01);
	atbm888x_write_reg(priv, 0x0005, 0x00);
	msleep(1);
	atbm888x_write_reg(priv, 0x010c, 0x01); //I2C clock switch 
}

static int atbm888x_init(struct dvb_frontend *fe)
{
	struct atbm_state *priv = fe->demodulator_priv;
	const struct atbm888x_config *cfg = priv->config;
	uint8_t ui8pll = 0;
	uint8_t ui8HardwareState = 0;
	uint8_t ui8tmp = 0;
	uint8_t u8ChipVersion, ui8ADCRfv;
	u8 dat;
	dev_dbg(&priv->i2c->dev, "%s:start\n", __func__);

	atbm888x_wakeup(fe);

	/* magic from refference driver */
	atbm888x_write_reg(priv, 0x010c, 0x00);
	atbm888x_write_reg(priv, 0x0606, 0x00);
	atbm888x_write_reg(priv, 0x0103, 0x00);
	atbm888x_read_reg(priv, 0x060e, &ui8pll);
	atbm888x_write_reg(priv, 0x0604, 0x01);
	ui8pll |= 0x01;
	atbm888x_write_reg(priv, 0x060e,ui8pll);
	ui8pll &= 0xfe;
	atbm888x_write_reg(priv, 0x060e,ui8pll);
	atbm888x_write_reg(priv, 0x0604, 0x00);
	msleep(1);

	atbm888x_write_reg(priv, 0x010c, 0x01);//i2c clock using PLL, 1:PLL, 0:Crystal.
	atbm888x_write_reg(priv, 0x0004, 0x00);
	atbm888x_write_reg(priv, 0x10f7, 0xea); // inverted AGC
	// atbm888x_write_reg(priv, 0x10f7, 0xe8);
	atbm888x_write_reg(priv, 0x10fb, 0x06); // PWM mode of AGC output is required (default is PDM)
	//atbm888x_write_reg(priv, 0x10fb, 0x07);

	atbm888x_write_reg_array(priv, ui8AATBM888XCommonReg,sizeof(ui8AATBM888XCommonReg));

	/* no swap IQ */
	atbm888x_write_reg(priv, REG_SWAP_I_Q, 0x01);
	/*Set IF frequency*/
	set_if_freq(priv, cfg->if_freq);

	/*Set oscillator frequency*/
	set_osc_freq(priv, cfg->osc_clk_freq);

	/* TODO: all this values calculated
	 * for 24MHz xtal 
	 * and 8MHz BW */

	/* set ADC rate ratio */
	atbm888x_read_reg(priv, 0x060e, &dat);
	dat |= 0x4;
	atbm888x_write_reg(priv, 0x060e, dat);
	atbm888x_write_reg(priv, 0x0afb, 0x01);

	/* ui16ParamsFSADC */
	atbm888x_write_reg(priv, 0x0233, 0x01);
	atbm888x_write_reg(priv, 0x0234, 0xc0);
	/* ui32ParamsFSADCInverse */
	atbm888x_write_reg(priv, 0x029d, 0x56);
	atbm888x_write_reg(priv, 0x029e, 0x55);
	atbm888x_write_reg(priv, 0x029f, 0x03);
	/* ui16ParamsIFFreq */
	atbm888x_write_reg(priv, 0x0231, 0x01);
	atbm888x_write_reg(priv, 0x0232, 0x28);

	atbm888x_write_reg(priv, 0x0251, 0x00);
	atbm888x_write_reg(priv, 0x0290, 0xd0);
	atbm888x_write_reg(priv, 0x0291, 0x93);
	atbm888x_write_reg(priv, 0x0292, 0x01);

	/*Set AGC Config*/
	set_agc_config(priv, cfg->agc_min, cfg->agc_max,
			cfg->agc_hold_loop);

	/*Set static channel mode*/
	set_static_channel_mode(priv);

	set_ts_config(priv);

	/* magic */
	atbm888x_write_reg(priv, 0x000a, 0x01);
	atbm888x_write_reg(priv, 0x0009, 0x01);         
	atbm888x_write_reg(priv, 0x0013, 0x00); //DTMB mode
	atbm888x_write_reg(priv, 0x1518, 0x00);            
	atbm888x_write_reg(priv, 0x1515, 0x00);
	atbm888x_write_reg(priv, 0x1511, 0x00); 
	atbm888x_write_reg(priv, 0x1512, 0x00);

	atbm888x_write_reg(priv, 0x0252, 0x00);
	/* ui8AATBM888XDtmbSet */
	atbm888x_write_reg_array(priv, ui8AATBM888XDtmbSet,sizeof(ui8AATBM888XDtmbSet));
	/* ui8AATBM888XDtmb24MSet */
	atbm888x_write_reg_array(priv, ui8AATBM888XDtmb24MSet,sizeof(ui8AATBM888XDtmb24MSet));
	atbm888x_write_reg_array(priv, ui8ADtmbInternal,sizeof(ui8ADtmbInternal));
	atbm888x_write_reg_array(priv, uiAATBMDynamicSetting,sizeof(uiAATBMDynamicSetting));

	atbm888x_read_reg(priv, 0x0022, &u8ChipVersion);
	dev_dbg(&priv->i2c->dev, "%s u8ChipVersion=0x%x\n", __func__, u8ChipVersion);
	if((u8ChipVersion&0xff) == 0xf0)
        {
                atbm888x_read_reg(priv, 0x060d, &ui8ADCRfv);
                ui8ADCRfv |=0x08;
                atbm888x_write_reg(priv, 0x060d, ui8ADCRfv);
                atbm888x_write_reg(priv, 0x0c02, 0x02);
        }

	atbm888x_write_reg(priv,0x0800, 0x30);  //Should be set as calibrated from ATBM_PPM_Test() function
	atbm888x_write_reg(priv,0x0801, 0x16);  //Should be set as calibrated from ATBM_PPM_Test() function

#if 0
	/* check state */
	/* After hardware power on properly or reset correctly,      
	 * ui8HardwareState value should be 0x05 when using crystal, 3.3V PLL 
	 * ui8HardwareState value should be 0x07 when using oscillator, 3.3V PLL */
	atbm888x_read_reg(priv, 0x0607, &ui8HardwareState);
	dev_dbg(&priv->i2c->dev, "%s ui8HardwareState=0x%x\n", __func__, ui8HardwareState);

	/* check PLL lock flag */
	atbm888x_read_reg(priv, 0x0611, &ui8tmp);
	dev_dbg(&priv->i2c->dev, "%s ui8Flag=0x%x\n", __func__, ui8tmp);

	/* check Current software version */
	atbm888x_read_reg(priv, 0x020c, &ui8tmp);
	dev_dbg(&priv->i2c->dev, "%s Current software version=0x%x\n", __func__, ui8tmp);

	atbm888x_read_reg(priv, 0x0004, &ui8tmp);
	dev_dbg(&priv->i2c->dev, "%s ui8ConfigDone=0x%x\n", __func__, ui8tmp);

	atbm888x_read_reg(priv, 0x000A, &ui8tmp);
	dev_dbg(&priv->i2c->dev, "%s ui8SDPReset=0x%x\n", __func__, ui8tmp);

	/*Turn off DSP reset*/
	atbm888x_write_reg(priv, 0x000A, 0);

	/*SW version test*/
	atbm888x_write_reg(priv, 0x020C, 141);
#endif

	/* Run */
	atbm888x_write_reg(priv, REG_DEMOD_RUN, 1);

	dev_dbg(&priv->i2c->dev, "%s:done\n", __func__);
	return 0;
}

static void atbm888x_sleep(struct dvb_frontend *fe)
{
	struct atbm_state *priv = fe->demodulator_priv;
	uint8_t ui8ADCPD,ui8TSEdge,ui8RefStandby, ui8ADCRfv,ui8GPORge;
	dev_dbg(&priv->i2c->dev, "%s\n", __func__);

	atbm888x_read_reg(priv, 0x0600,&ui8RefStandby);
	atbm888x_read_reg(priv, 0x0602,&ui8ADCPD);
	atbm888x_read_reg(priv, 0x0301,&ui8TSEdge);
	atbm888x_read_reg(priv, 0x060d,&ui8ADCRfv);
	atbm888x_read_reg(priv, 0x10f7,&ui8GPORge);

	ui8RefStandby |= 0x02;
	ui8ADCPD      |= 0x01;
	ui8TSEdge     |= 0x02;
	ui8ADCRfv     |=0x08;
	ui8GPORge     |=0x01;

	atbm888x_write_reg(priv, 0x0005, 0x01);
	atbm888x_write_reg(priv, 0x060d, ui8ADCRfv);
	atbm888x_write_reg(priv, 0x0600, ui8RefStandby);
	atbm888x_write_reg(priv, 0x0602, ui8ADCPD);
	atbm888x_write_reg(priv, 0x1500, 0x00);
	atbm888x_write_reg(priv, 0x0301, ui8TSEdge); /*all TS output PINs will be high-z*/
	atbm888x_write_reg(priv, 0x10f7, ui8GPORge);
	atbm888x_write_reg(priv, 0x0019, 0x00);
	atbm888x_write_reg(priv, 0x010c, 0x00); //I2C clock switch 
	atbm888x_write_reg(priv, 0x0604, 0x01); //shutdown PLL   
	dev_dbg(&priv->i2c->dev, "%s standby now\n", __func__);
}

static void atbm888x_release(struct dvb_frontend *fe)
{
	struct atbm_state *priv = fe->demodulator_priv;
	dev_dbg(&priv->i2c->dev, "%s\n", __func__);

	kfree(priv);
}

static int atbm888x_set_fe(struct dvb_frontend *fe)
{
	struct atbm_state *priv = fe->demodulator_priv;
	int i;
	u8 locked = 0;
	dev_dbg(&priv->i2c->dev, "%s:start\n", __func__);
	printk("aospan:atbm888x_chip_id=0x%x \n", atbm888x_chip_id(priv));

	/* set frequency */
	if (fe->ops.tuner_ops.set_params) {
		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 1);
		fe->ops.tuner_ops.set_params(fe);
		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 0);
	}

	/* start auto lock */
	for (i = 0; i < 10; i++) {
		mdelay(100);
		dev_dbg(&priv->i2c->dev, "Try %d\n", i);
		is_locked(priv, &locked);
		if (locked != 0) {
			dev_dbg(&priv->i2c->dev, "ATBM888x locked!\n");
			break;
		}
	}

	dev_dbg(&priv->i2c->dev, "%s:done\n", __func__);
	return 0;
}

static int atbm888x_get_fe(struct dvb_frontend *fe,
			   struct dtv_frontend_properties *c)
{
	struct atbm_state *priv = fe->demodulator_priv;
	dev_dbg(&priv->i2c->dev, "%s\n", __func__);

	/* TODO: get real readings from device */
	/* inversion status */
	c->inversion = INVERSION_OFF;

	/* bandwidth */
	c->bandwidth_hz = 8000000;

	c->code_rate_HP = FEC_AUTO;
	c->code_rate_LP = FEC_AUTO;

	c->modulation = QAM_AUTO;

	/* transmission mode */
	c->transmission_mode = TRANSMISSION_MODE_AUTO;

	/* guard interval */
	c->guard_interval = GUARD_INTERVAL_AUTO;

	/* hierarchy */
	c->hierarchy = HIERARCHY_NONE;

	return 0;
}

static int atbm888x_get_tune_settings(struct dvb_frontend *fe,
	struct dvb_frontend_tune_settings *fesettings)
{
	fesettings->min_delay_ms = 0;
	fesettings->step_size = 0;
	fesettings->max_drift = 0;
	return 0;
}

static int atbm888x_read_status(struct dvb_frontend *fe,
				enum fe_status *fe_status)
{
	struct atbm_state *priv = fe->demodulator_priv;
	u8 locked = 0;
	u8 agc_locked = 0;
	uint8_t agc0, agc1;
	uint16_t agc_value;

#if 1
	dev_dbg(&priv->i2c->dev, "%s\n", __func__);
	printk("aospan:atbm888x_chip_id=0x%x \n", atbm888x_chip_id(priv));
#endif

	*fe_status = 0;

	is_locked(priv, &locked);
	if (locked) {
		*fe_status |= FE_HAS_SIGNAL | FE_HAS_CARRIER |
			FE_HAS_VITERBI | FE_HAS_SYNC | FE_HAS_LOCK;
	}
	dev_dbg(&priv->i2c->dev, "%s: fe_status=0x%x\n", __func__, *fe_status);

#if 0
	atbm888x_read_reg(priv, REG_AGC_LOCK, &agc_locked);
	dev_dbg(&priv->i2c->dev, "AGC Lock: %d\n", agc_locked);
#endif

	/* Reading AGC can cause AGC pin to 0V ! */
#if 0
  /* read AGC value:
   * the AGC Value is related with AGC voltage: about (1024/3.3)*V_agc
   */
	atbm888x_write_reg(priv, 0x084d, 1); // lock value
	atbm888x_read_reg(priv, 0x1028, &agc0);
	atbm888x_read_reg(priv, 0x1029, &agc1);
  agc_value = (uint16_t)(((agc1&0x03)<<8)|agc0);
	atbm888x_write_reg(priv, 0x084d, 0); // unlock value
	dev_dbg(&priv->i2c->dev, "AGC value: %d\n", agc_value);
#endif
	return 0;
}

static int atbm888x_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	struct atbm_state *priv = fe->demodulator_priv;
	u32 frame_err;
	u8 t;

	dev_dbg(&priv->i2c->dev, "%s\n", __func__);

	atbm888x_reglatch_lock(priv, 1);

	atbm888x_read_reg(priv, REG_FRAME_ERR_CNT + 1, &t);
	frame_err = t & 0x7F;
	frame_err <<= 8;
	atbm888x_read_reg(priv, REG_FRAME_ERR_CNT, &t);
	frame_err |= t;

	atbm888x_reglatch_lock(priv, 0);

	*ber = frame_err * 100 / 32767;

	dev_dbg(&priv->i2c->dev, "%s: ber=0x%x\n", __func__, *ber);
	return 0;
}

static int atbm888x_read_signal_strength(struct dvb_frontend *fe, u16 *signal)
{
	struct atbm_state *priv = fe->demodulator_priv;
	u32 pwm;
	u8 t;

	dev_dbg(&priv->i2c->dev, "%s\n", __func__);
	atbm888x_reglatch_lock(priv, 1);

	atbm888x_read_reg(priv, REG_AGC_PWM_VAL + 1, &t);
	pwm = t & 0x03;
	pwm <<= 8;
	atbm888x_read_reg(priv, REG_AGC_PWM_VAL, &t);
	pwm |= t;

	atbm888x_reglatch_lock(priv, 0);

	dev_dbg(&priv->i2c->dev, "AGC PWM = 0x%02X\n", pwm);
	pwm = 0x400 - pwm;

	*signal = pwm * 0x10000 / 0x400;

	return 0;
}

static int atbm888x_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct atbm_state *priv = fe->demodulator_priv;
	dev_dbg(&priv->i2c->dev, "%s\n", __func__);
	*snr = 0;
	return 0;
}

static int atbm888x_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	struct atbm_state *priv = fe->demodulator_priv;
	dev_dbg(&priv->i2c->dev, "%s\n", __func__);
	*ucblocks = 0;
	return 0;
}

static int atbm888x_i2c_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	struct atbm_state *priv = fe->demodulator_priv;
	enable = 0;

	return atbm888x_write_reg(priv, REG_I2C_GATE, enable ? 1 : 0);
}

static int atbm888x_tune(struct dvb_frontend *fe, bool re_tune,
			  unsigned int mode_flags, unsigned int *delay,
			  enum fe_status *status)
{
	int ret = 0;
	struct atbm_state *priv = fe->demodulator_priv;

	atbm888x_set_fe(fe);

	return ret;
}


static struct dvb_frontend_ops atbm888x_ops = {
	.delsys = { SYS_DTMB },
	.info = {
		.name = "AltoBeam ATBM888x DMB-TH",
		.frequency_min = 474000000,
		.frequency_max = 858000000,
		.frequency_stepsize = 10000,
		.caps =
			FE_CAN_FEC_AUTO |
			FE_CAN_QAM_AUTO |
			FE_CAN_TRANSMISSION_MODE_AUTO |
			FE_CAN_GUARD_INTERVAL_AUTO
	},

	.release = atbm888x_release,

	.init = atbm888x_init,
	.sleep = atbm888x_sleep,
	.write = NULL,
	.i2c_gate_ctrl = atbm888x_i2c_gate_ctrl,

	.tune	= atbm888x_tune,
	.set_frontend = atbm888x_set_fe,
	.get_frontend = atbm888x_get_fe,
	.get_tune_settings = atbm888x_get_tune_settings,

	.read_status = atbm888x_read_status,
	.read_ber = atbm888x_read_ber,
	.read_signal_strength = atbm888x_read_signal_strength,
	.read_snr = atbm888x_read_snr,
	.read_ucblocks = atbm888x_read_ucblocks,
};


struct dvb_frontend *atbm888x_attach(const struct atbm888x_config *config,
	struct i2c_adapter *i2c)
{
	struct atbm_state *priv = NULL;
	u8 data = 0;

	dev_dbg(&i2c->dev, "%s()\n", __func__);

	if (config == NULL || i2c == NULL)
		return NULL;

	priv = kzalloc(sizeof(struct atbm_state), GFP_KERNEL);
	if (priv == NULL)
		goto error_out;

	priv->config = config;
	priv->i2c = i2c;

	/* check if the demod is there */
	if (atbm888x_read_reg(priv, REG_CHIP_ID, &data) != 0) {
		dev_err(&i2c->dev, "%s atbm888x not found at i2c addr 0x%02X\n",
			__func__, priv->config->demod_address);
		goto error_out;
	}
	dev_dbg(&i2c->dev, "atbm888x chip id: 0x%02X\n", data);

  if (data != 0x40) {
		dev_err(&i2c->dev, "%s atbm888x chip id 0x%x is not supported",
			__func__, data);
		goto error_out;
  }

	memcpy(&priv->frontend.ops, &atbm888x_ops,
	       sizeof(struct dvb_frontend_ops));
	priv->frontend.demodulator_priv = priv;

	atbm888x_init(&priv->frontend);
	printk("aospan:atbm888x_chip_id=0x%x \n", atbm888x_chip_id(priv));
	atbm888x_i2c_gate_ctrl(&priv->frontend, 1);

	printk("aospan:atbm888x_chip_id=0x%x \n", atbm888x_chip_id(priv));
	return &priv->frontend;

error_out:
	dev_err(&priv->i2c->dev, "%s() error\n", __func__);
	kfree(priv);
	return NULL;

}
EXPORT_SYMBOL(atbm888x_attach);

MODULE_DESCRIPTION("AltoBeam ATBM888x demodulator driver");
MODULE_AUTHOR("Abylay Ospan <aospan@netup.ru>");
MODULE_LICENSE("GPL");
