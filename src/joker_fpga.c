/* 
 * Access to Joker TV FPGA
 * for Joker Eco-system
 * https://jokersys.com
 * (c) Abylay Ospan, 2017
 * aospan@jokersys.com
 * GPLv2
 */

#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <sys/types.h>
#include <joker_tv.h>
#include <joker_fpga.h>
#include <libusb.h>

/* USB part */
/* open usb device
 *
 * return: device descriptor
 * or NULL if failed
 */
int joker_open(struct joker_t *joker)
{
	struct libusb_context *ctx = NULL;
	struct libusb_device **usb_list = NULL;
	struct libusb_device_handle *devh = NULL;
	struct libusb_device_descriptor desc;
	int usb_devs, i, r, ret, transferred;
	unsigned char in_buf[JCMD_BUF_LEN];

	if (!joker)
		return EINVAL;

	ret = libusb_init(NULL);
	if (ret < 0) {
		fprintf(stderr, "libusb_init failed\n");
		return ENODEV;
	}

	if (joker->libusb_verbose == 0)
		joker->libusb_verbose = 1;
	libusb_set_debug(NULL, joker->libusb_verbose);

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
		return ENODEV;
	}

	printf("usb device found\n");

	ret = libusb_set_configuration(devh, 1);
	if (ret < 0) {
		fprintf(stderr, "Can't set config: %s\n", libusb_error_name(ret));
		libusb_close(devh);
		return EIO;
	}

	ret = libusb_claim_interface(devh, 0);
	if (ret < 0) {
		fprintf(stderr, "Can't claim interface: %s\n", libusb_error_name(ret));
		libusb_close(devh);
		return EIO;
	}

	joker->libusb_opaque = (void *)devh;
	printf("open:dev=%p \n", devh);


	/* prophylactic cleanup EP1 IN */
	libusb_bulk_transfer(devh, USB_EP1_IN, in_buf, JCMD_BUF_LEN, &transferred, 1);
	libusb_bulk_transfer(devh, USB_EP1_IN, in_buf, JCMD_BUF_LEN, &transferred, 1);
	libusb_bulk_transfer(devh, USB_EP1_IN, in_buf, JCMD_BUF_LEN, &transferred, 1);

	return 0;
}

/* release usb device */
int joker_close(struct joker_t * joker) {
	struct libusb_device_handle *dev = NULL;

	if (!joker)
		return EINVAL;

	dev = (struct libusb_device_handle *)joker->libusb_opaque;

	libusb_release_interface(dev, 0);
	if(dev)
		libusb_close(dev);
	joker->libusb_opaque = NULL; /* dev not valid anymore */
}

/* exchange with FPGA over USB
 * EP2 OUT EP used as joker commands (jcmd) source
 * EP1 IN EP used as command reply storage
 * return 0 if success
 */
int joker_io(struct joker_t * joker, struct jcmd_t * jcmd) {
	struct libusb_device_handle *dev = NULL;
	unsigned char buf[JCMD_BUF_LEN];
	int cnt = 20 /* 200 msec timeout */, ret = 0, transferred = 0;

	if (!joker)
		return EINVAL;

	dev = (struct libusb_device_handle *)joker->libusb_opaque;

	ret = libusb_bulk_transfer(dev, USB_EP2_OUT, jcmd->buf, jcmd->len, &transferred, 0);
	if (ret < 0 || transferred != jcmd->len)
		return ret;

	while ( cnt-- > 0) {
		/* jcmd expect some reply */
		if (jcmd->in_len > 0) {
			/* read collected data */
			ret = libusb_bulk_transfer(dev, USB_EP1_IN, jcmd->in_buf, jcmd->in_len, &transferred, 1);
			if (ret < 0 || transferred != jcmd->in_len)
				break;
		}
		return 0;
	}

	return ret;
}

int joker_cmd(struct joker_t * joker, unsigned char *data, int len, unsigned char * in_buf, int in_len) {
	int ret = 0;
	struct jcmd_t jcmd;
	int i = 0;

	if (len > JCMD_BUF_LEN || in_len > JCMD_BUF_LEN)
		return EINVAL;

	memcpy(jcmd.buf, data, len);
	jcmd.len = len;
	jcmd.in_len = in_len;

	if ((ret != joker_io(joker, &jcmd)))
		return ret;

	if (in_buf && in_len)
		memcpy(in_buf, jcmd.in_buf, in_len);

	return 0;
}
