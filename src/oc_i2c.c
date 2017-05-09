/* 
 * Opencores I2C master driver
 * for Joker Eco-system
 * https://jokersys.com
 * (c) Abylay Ospan, 2017
 * aospan@jokersys.com
 */

#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <oc_i2c.h>

static int dosleep = 1;

/* USB part */
/* open usb device
 *
 * return: device descriptor
 * or NULL if failed
 */
struct libusb_device_handle * joker_open() {
	struct libusb_context *ctx = NULL;
	struct libusb_device **usb_list = NULL;
	struct libusb_device_handle *devh = NULL;
	struct libusb_device_descriptor desc;
	int usb_devs, i, r, ret;

	ret = libusb_init(NULL);
	if (ret < 0) {
		fprintf(stderr, "libusb_init failed\n");
		return 0;
	}
	libusb_set_debug(NULL, 3);
	// libusb_set_debug(NULL, 4); // more debug !

	usb_devs = libusb_get_device_list(ctx, &usb_list);
	for(i = 0 ; i < usb_devs ; ++i) {
		r = libusb_get_device_descriptor(usb_list[i], &desc);
		if(r < 0) {
			fprintf(stderr, "couldn't get usb descriptor for dev #%d!\n", i);
			fprintf(stderr, "desc.idVendor=0x%x desc.idProduct=0x%x\n",
				desc.idVendor, desc.idProduct);
		}

		if (desc.idVendor == NETUP_VID && desc.idProduct == JOKER_TV_PID)
		{
			r = libusb_open(usb_list[i], &devh);
			if (r)
				libusb_error_name(r);
		}
	}
	if (devh <= 0) {
		fprintf(stderr, "usb device not found\n");
		return 0;
	}

	printf("usb device found\n");

	ret = libusb_set_configuration(devh, 1);
	if (ret < 0) {
		fprintf(stderr, "Can't set config: %s\n", libusb_error_name(ret));
		libusb_close(devh);
		return 0;
	}

	ret = libusb_claim_interface(devh, 0);
	if (ret < 0) {
		fprintf(stderr, "Can't claim interface: %s\n", libusb_error_name(ret));
		libusb_close(devh);
		return 0;
	}

	return devh;
}

/* read i2c opencore module offset 
 * return 0 if success
 */
int oc_i2c_read_off(struct libusb_device_handle *dev, int offset, char *data) {
	int cnt = 20 /* 200 msec timeout */, ret = 0, transferred = 0;
	unsigned char buf[BUF_LEN];

	while ( cnt-- > 0) {
		buf[0] = OC_I2C_SR_READ; /* trigger SR reading */
		buf[1] = offset; 
		ret = libusb_bulk_transfer(dev, USB_EP2_OUT, buf, 2, &transferred, 0);
		// printf("WRITE ret=%d transferred=%d data=0x%x %x\n", ret, transferred, buf[0], buf[1] );
		if (ret < 0)
			return -1;

		// printf("reading ...\n");
		ret = libusb_bulk_transfer(dev, USB_EP1_IN, buf, 1, &transferred, 0);
		if (ret < 0) {
			// printf("ERR READ ret=%d transferred=%d \n", ret, transferred );
			return -1;
		}
		// printf("READ ret=%d transferred=%d data=0x%x\n", ret, transferred, buf[0] );
		if (offset == OC_I2C_SR) { /* status reg */
			/* do some status check */
			if (buf[0] & OC_I2C_TIP) {
				// printf("Transaction in progress\n");
				usleep (10000);
			} else {
				if (buf[0] & OC_I2C_ACK) {
					// printf("I2C NACK buf=0x%x\n", buf[0]);
					// sleep(3600);
					*data = buf[0];
					return 0; /* not a problem ? */
				} else if (buf[0] & OC_I2C_AL) {
					printf("I2C ARB LOST buf=0x%x\n", buf[0]);
					return -1; 
				} else {
					*data = buf[0];
					// printf("I2C read success\n");
					return 0;
				}
			}
		} else {
			*data = buf[0];
			return 0;
		}
	}

	if (cnt <= 0)
		ret = -1; /* timeout */

	return ret;
}

/* write i2c opencore module offset 
 * return 0 if success
 */
int oc_i2c_write_off(struct libusb_device_handle *dev, int offset, char data) {
	int ret = 0, transferred = 0;
	unsigned char buf[BUF_LEN];

	buf[0] = offset;
	buf[1] = data; 
	ret = libusb_bulk_transfer(dev, USB_EP2_OUT, buf, 2, &transferred, 0);
	// printf("WRITE offset=%d transferred=%d data=0x%x %x\n", offset, transferred, buf[0], buf[1] );

#if 0
	if(offset == OC_I2C_CR && dosleep) {
		printf("Press any key to continue ...\n");
		read(0, &buf, 1);
	}
#endif

	return ret;
}

/* write bytes to i2c chip
 * chip - chip address (7 bit notation)
 * data, size - actual data to write
 * resulting actual transaction on i2c bus:
 *	Start/chip - data[0] - data[1] ... data[n]/Stop
 */
int oc_i2c_write(struct libusb_device_handle *dev, uint8_t chip, unsigned char * data, int size) {
	int i = 0;
	unsigned char buf[BUF_LEN];
	unsigned char cmd;
	chip = chip << 1; /* convert i2c addr to 8 bit notation */
	// printf("writing chip=0x%x size=%d data=0x%x %x\n", chip, size, data[0], data[1]);

	/* write device address first */
	if (oc_i2c_write_off(dev, OC_I2C_TXR, chip))
		return -1;
	
	/* actual bus transfer */
	cmd = OC_I2C_START | OC_I2C_WRITE;
	if (size == 0)
		cmd |= OC_I2C_STOP;
	if (oc_i2c_write_off(dev, OC_I2C_CR, cmd))
		return -1;

	if (oc_i2c_read_off(dev, OC_I2C_SR, &buf))
		return -1;

	/* no ACK received */
	if (buf[0] & OC_I2C_ACK)
		return -1;

	for (i = 0; i < size; i++) {
		/* set data to bus */
		// printf("writing i=%d data=0x%x \n", i, data[i]);
		if (oc_i2c_write_off(dev, OC_I2C_TXR, data[i]))
			return -1;

		/* actual bus transfer */
		cmd = OC_I2C_WRITE;
		if ( (i+1) == size ) /* last byte */
			cmd |= OC_I2C_STOP;

		if (oc_i2c_write_off(dev, OC_I2C_CR, cmd))
			return -1;

		if(oc_i2c_read_off(dev, OC_I2C_SR, &buf))
			return -1;
	}
	// printf("	writing chip=0x%x size=%d DONE\n", chip, size);
	// sleep(5);

	return 0;
}

/* read bytes from i2c chip
 * chip - chip address (7 bit notation)
 *
 * resulting actual transaction on i2c bus:
 *	Start/chip - data[0] - data[1] ... data[n]/Stop
 */
int oc_i2c_read(struct libusb_device_handle *dev, uint8_t chip, unsigned char * data, int size) {
	int i = 0;
	unsigned char buf[BUF_LEN];
	unsigned char cmd;
	chip = ((chip << 1) | 0x01); /* convert i2c addr to 8 bit notation and add Read bit */

	// printf("reading %d bytes from chip=0x%x \n", size, chip);

	/* write device address first */
	if (oc_i2c_write_off(dev, OC_I2C_TXR, chip))
		return -1;
	
	/* actual bus transfer */
	cmd = OC_I2C_START | OC_I2C_WRITE;
	if (size == 0)
		cmd |= OC_I2C_STOP;
	if (oc_i2c_write_off(dev, OC_I2C_CR, cmd))
		return -1;

	if (oc_i2c_read_off(dev, OC_I2C_SR, &buf))
		return -1;

	/* no ACK received */
	if (buf[0] & OC_I2C_ACK)
		return -1;

	for (i = 0; i < size; i++) {
		/* read data from bus */
		// if (oc_i2c_write_off(dev, OC_I2C_TXR, data[i]))
			// return -1;

		/* actual bus transfer */
		cmd = OC_I2C_READ;
		if ( (i+1) == size ) /* last byte */
			cmd |= OC_I2C_STOP | OC_I2C_NACK;
			// cmd |= OC_I2C_STOP;

		if (oc_i2c_write_off(dev, OC_I2C_CR, cmd))
			return -1;

		/* read status byte */
		if(oc_i2c_read_off(dev, OC_I2C_SR, &buf))
			return -1;

		// printf("	WRITING ACTUAL BYTE DONE\n");
		// sleep(5);
		/* read saved byte */
		if(oc_i2c_read_off(dev, OC_I2C_RXR, &data[i]))
			return -1;
		// printf("	READING ACTUAL BYTE DONE\n");
		// sleep(5);
	}
	// printf("	DONE reading %d bytes from chip=0x%x data=0x%x\n",
			// size, chip, data[0]);
	// if (data[0] == 0xbf)
		// dosleep = 1;
	// sleep(5);
	return 0;
}

/* "ping" i2c address.
 * return 0 (success) if ACKed
 * return -1 (fail) if no ACK received
 */
int oc_i2c_ping(struct libusb_device_handle *dev, uint8_t chip) {
	int i = 0;
	unsigned char buf[BUF_LEN];
	unsigned char cmd;
	chip = (chip << 1); /* convert i2c addr to 8 bit notation */

	/* write device address first */
	if (oc_i2c_write_off(dev, OC_I2C_TXR, chip))
		return -1;
	
	/* actual bus transfer */
	cmd = OC_I2C_START | OC_I2C_WRITE | OC_I2C_STOP;
	if (oc_i2c_write_off(dev, OC_I2C_CR, cmd))
		return -1;

	if (oc_i2c_read_off(dev, OC_I2C_SR, &buf))
		return -1;

	/* no ACK received */
	if (buf[0] & OC_I2C_ACK)
		return -1;

	return 0;
}

/*
 * return: device descriptor
 * or NULL if failed
 */
struct libusb_device_handle * i2c_init() {
	struct libusb_device_handle * dev = NULL;
	
	if ( ! (dev = joker_open()) )
		return 0;

	/* enable core */
	if (oc_i2c_write_off(dev, OC_I2C_CTR, OC_I2C_CORE_ENABLE | OC_I2C_IRQ_ENABLE))
		return -1;

	/* set i2c bus to 400kHz */
	// if (oc_i2c_write_off(dev, OC_I2C_PRELO, OC_I2C_100K))
	if (oc_i2c_write_off(dev, OC_I2C_PRELO, OC_I2C_400K))
		return -1;
	if (oc_i2c_write_off(dev, OC_I2C_PREHI, 0x00))
		return -1;

	return dev;
}

/* release usb device */
void i2c_close(struct libusb_device_handle *dev) {
        libusb_release_interface(dev, 0);
	if(dev)
		libusb_close(dev);
	dev = NULL; /* dev not valid anymore */
}
