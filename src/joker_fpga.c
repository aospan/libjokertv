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
	int usb_devs, i, r, ret;

  if (!joker)
    return EINVAL;

	ret = libusb_init(NULL);
	if (ret < 0) {
		fprintf(stderr, "libusb_init failed\n");
		return ENODEV;
	}

	libusb_set_debug(NULL, 1);
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

/* read byte at offset 
 * return 0 if success 
 * resulting byte in *data
 */
int joker_read_off(struct joker_t * joker, int offset, char *data) {
  struct libusb_device_handle *dev = NULL;
	int cnt = 20 /* 200 msec timeout */, ret = -1, transferred = 0;
	unsigned char buf[BUF_LEN];

  if (!joker)
    return EINVAL;

  dev = (struct libusb_device_handle *)joker->libusb_opaque;

	while ( cnt-- > 0) {
		buf[0] = JOKER_READ; /* trigger read */
		buf[1] = offset; 
		ret = libusb_bulk_transfer(dev, USB_EP2_OUT, buf, 2, &transferred, 0);
		if (ret < 0)
      break;

    /* read collected data */
		ret = libusb_bulk_transfer(dev, USB_EP1_IN, buf, 1, &transferred, 0);
		if (ret < 0 || transferred != 1)
      break;

    *data = buf[0];
    return 0;
  }

	return ret;
}

/* write byte to offset 
 * return 0 if success
 */
int joker_write_off(struct joker_t * joker, int offset, char data) {
  struct libusb_device_handle *dev = NULL;
	int ret = 0, transferred = 0;
	unsigned char buf[BUF_LEN];

  if (!joker)
    return EINVAL;

  dev = (struct libusb_device_handle *)joker->libusb_opaque;

	buf[0] = offset;
	buf[1] = data; 
	// printf("addr buf=%p &buf=%p dev=%p \n", buf, &buf[0], dev );
	ret = libusb_bulk_transfer(dev, USB_EP2_OUT, buf, 2, &transferred, 0);

	return ret;
}

