/* 
 * Access to Joker TV FPGA
 * https://jokersys.com
 * (c) Abylay Ospan, 2017
 * aospan@jokersys.com
 * GPLv2
 */

#include <stdio.h>
#include <libusb.h>
#include "joker_tv.h"

#ifndef _JOKER_FPGA
#define _JOKER_FPGA	1

/* registers addrs */
#define OC_I2C_PRELO		(0x00) /* Speed of the i2c bus */
#define OC_I2C_PREHI		(0x01) /* Speed of the i2c bus */
#define OC_I2C_CTR		(0x02) /* Write: control register */
#define OC_I2C_TXR		(0x03) /* Write: data byte for sending */
#define OC_I2C_RXR		(0x03) /* Read: received data byte */
#define OC_I2C_CR		(0x04) /* Write: command register */
#define OC_I2C_SR		(0x04) /* Read: status register */
#define JOKER_READ  (0x05) /* Trigger reading */
#define OC_I2C_RESET_CTRL	(0x06) /* write: Control chips reset */
#define OC_I2C_INSEL_CTRL	(0x07) /* write: Choose TS input */
#define JOKER_USB_ISOC_LEN_HI (0x08) /* write: Choose USB isoc pkt len. default 512 */
#define JOKER_USB_ISOC_LEN_LO (0x09) /* write: Choose USB isoc pkt len. default 512 */

/* PRE registers bits */
#define OC_I2C_100K		(0x63) /* 100kHz bus speed */
#define OC_I2C_400K		(0x18) /* 400kHz bus speed */

/* CTR reg bits */
#define OC_I2C_CORE_ENABLE	(0x80)
#define OC_I2C_IRQ_ENABLE	(0x40)

/* command register bits */
#define OC_I2C_START		1 << 7 /* 0x80 */
#define OC_I2C_STOP		1 << 6 /* 0x40 */
#define OC_I2C_READ		1 << 5 /* 0x20 */
#define OC_I2C_WRITE		1 << 4 /* 0x10 */
#define OC_I2C_NACK		1 << 3 /* 0x08 */

/* status register bits */
#define OC_I2C_TIP		1 << 1 /* transaction in progress */
#define OC_I2C_AL		1 << 5 /* arbitration lost */
#define OC_I2C_BUSY		1 << 6
#define OC_I2C_ACK		1 << 7

/* reset control bits
 * chip in reset state if flag not set (0)
 * */
#define OC_I2C_RESET_GATE	1 << 7 /* tuner i2c gate enable */ 
#define OC_I2C_RESET_TPS_CI	1 << 6 /* TPS on CI bus */ 
#define OC_I2C_RESET_TPS	1 << 5 /* TPS on Terr antenna power 5V */ 
#define OC_I2C_RESET_USB	1 << 4 /* USB PHY ULPI */ 
#define OC_I2C_RESET_ATBM	1 << 3 /* Altobeam DTMB demod */ 
#define OC_I2C_RESET_LG		1 << 2 /* LG ATSC demod */ 
#define OC_I2C_RESET_TUNER	1 << 1 /* Sony Helene tuner */ 
#define OC_I2C_RESET_SONY	1 << 0 /* Sony DVB demod */ 

/* Choose TS input */
#define OC_I2C_INSEL_SONY	(0x00)
#define OC_I2C_INSEL_ATBM	(0x01)
#define OC_I2C_INSEL_LG		(0x02)

/* USB defines */
#define	USB_EP1_IN		(0x81) /* i2c, etc */
#define	USB_EP2_OUT		(0x02) /* i2c, etc */
#define	USB_EP3_IN		(0x83) /* transport stream */
#define NETUP_VID		(0x2D6B)
#define JOKER_TV_PID		(0x7777)

#define BUF_LEN 5120

/**** Joker binary protocol for exchange with FPGA ***/
/* keep sync command codes with Verilog firmware !*/
#define J_CMD_VERSION		0 /* return fw version */				
#define J_CMD_I2C_WRITE		10 /* i2c read/write */				
#define	J_CMD_I2C_READ		11
#define	J_CMD_RESET_CTRL_WRITE	12 /* reset control register  r/w */
#define	J_CMD_RESET_CTRL_READ	13
#define	J_CMD_TS_INSEL_WRITE	14 /* ts input select */
#define	J_CMD_TS_INSEL_READ	15
#define	J_CMD_ISOC_LEN_WRITE_HI	16 /* USB isoc transfers length */
#define	J_CMD_ISOC_LEN_WRITE_LO	17

/* limited by usb transfer size. 512 for bulk, 1024 for isoc */
#define JCMD_BUF_LEN 1024

struct jcmd_t {
	int cmd; /* command code. see defines above */
	/* out buffer. will be sent to device */
	unsigned char buf[JCMD_BUF_LEN];
	int len;
	/* input buffer. received bytes stored in this buf */
	unsigned char in_buf[JCMD_BUF_LEN];
	int in_len; /* amount of expected bytes */
};

#ifdef __cplusplus
extern "C" {
#endif

/* exchange with FPGA over USB
 * EP2 OUT EP used as joker commands (jcmd) source
 * EP1 IN EP used as command reply storage
 * return 0 if success
 */
int joker_io(struct joker_t * joker, struct jcmd_t * jcmd);
int joker_cmd(struct joker_t * joker, unsigned char *data, int len, unsigned char * in_buf, int in_len);

#ifdef __cplusplus
}
#endif

#endif /* end */
