/* 
 * Opencores I2C master driver
 * for Joker Eco-system
 * https://jokersys.com
 * (c) Abylay Ospan, 2017
 * aospan@jokersys.com
 */

#include <stdio.h>
typedef unsigned char           uint8_t;
typedef unsigned short int      uint16_t;
typedef unsigned int            uint32_t;

// #include <stdio.h>
// #include <stdint.h>
// #include <sys/types.h>
// #include <time.h>
#include <libusb.h>

#ifndef _OC_I2C
#define _OC_I2C	1

/* registers addrs */
#define OC_I2C_PRELO		(0x00) /* Speed of the i2c bus */
#define OC_I2C_PREHI		(0x01) /* Speed of the i2c bus */
#define OC_I2C_CTR		(0x02) /* Write: control register */
#define OC_I2C_TXR		(0x03) /* Write: data byte for sending */
#define OC_I2C_RXR		(0x03) /* Read: received data byte */
#define OC_I2C_CR		(0x04) /* Write: command register */
#define OC_I2C_SR		(0x04) /* Read: status register */
#define OC_I2C_SR_READ		(0x05) /* Trigger SR reading */
#define OC_I2C_RESET_CTRL	(0x06) /* Control chips reset */
#define OC_I2C_INSEL_CTRL	(0x07) /* Choose TS input */

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

/* exporting functions */
/* open usb device
 *
 * return: device descriptor
 * or NULL if failed
 */
struct libusb_device_handle * i2c_init();
void i2c_close(struct libusb_device_handle *dev);

/* write bytes to i2c chip
 * chip - chip address (7 bit notation)
 * data, size - actual data to write
 * resulting actual transaction on i2c bus:
 *	Start/chip - data[0] - data[1] ... data[n]/Stop
 */
int oc_i2c_write(struct libusb_device_handle *dev, uint8_t chip, unsigned char * data, int size);

int oc_i2c_read(struct libusb_device_handle *dev, uint8_t chip, unsigned char * data, int size);

/* "ping" i2c address.
 * return 0 (success) if ACKed
 * return -1 (fail) if no ACK received
 */
int oc_i2c_ping(struct libusb_device_handle *dev, uint8_t chip);

#endif /* end */
