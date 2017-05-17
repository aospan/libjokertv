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

#ifdef __cplusplus
extern "C" {
#endif

/* read byte at offset 
 * return 0 if success 
 * resulting byte in *data
 */
int joker_read_off(struct joker_t *joker, int offset, char *data);

/* write byte to offset 
 * return 0 if success
 */
int joker_write_off(struct joker_t *joker, int offset, char data);

#ifdef __cplusplus
}
#endif

#endif /* end */
