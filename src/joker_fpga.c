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
#include <string.h>
#include <sys/types.h>
#include <joker_tv.h>
#include <joker_fpga.h>
#include <joker_ci.h>
#include <joker_i2c.h>
#include <u_drv_data.h>
#include <libusb.h>
#include <pthread.h>

/* USB part */
/* open usb device
 *
 * return: 0 if success
 * or error code
 */
int joker_open(struct joker_t *joker)
{
	struct libusb_context *ctx = NULL;
	struct libusb_device **usb_list = NULL;
	struct libusb_device_handle *devh = NULL;
	struct libusb_device_descriptor desc;
	int usb_devs, i, r, ret, transferred;
	unsigned char buf[JCMD_BUF_LEN];
	unsigned char in_buf[JCMD_BUF_LEN];
	int isoc_len = USB_PACKET_SIZE;

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
			joker->fw_ver = desc.bcdDevice;
			break; // open first available Joker TV
		}
	}
	if (devh <= 0) {
		fprintf(stderr, "usb device not found\n");
		return ENODEV;
	}

	printf("usb device found. firmware version 0x%x\n", joker->fw_ver);

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
	jdebug("open:dev=%p \n", devh);

	joker->io_mux_opaq = malloc(sizeof(pthread_mutex_t));
	if (!joker->io_mux_opaq)
		return ENOMEM;
	pthread_mutex_init((pthread_mutex_t*)joker->io_mux_opaq, NULL);

	/* prophylactic cleanup EP1 IN */
	libusb_bulk_transfer(devh, USB_EP1_IN, in_buf, JCMD_BUF_LEN, &transferred, 1);
	libusb_bulk_transfer(devh, USB_EP1_IN, in_buf, JCMD_BUF_LEN, &transferred, 1);
	libusb_bulk_transfer(devh, USB_EP1_IN, in_buf, JCMD_BUF_LEN, &transferred, 1);

	/* tune usb isoc transaction len */
	buf[0] = J_CMD_ISOC_LEN_WRITE_HI;
	buf[1] = (isoc_len >> 8) & 0x7;
	if ((ret = joker_cmd(joker, buf, 2, NULL /* in_buf */, 0 /* in_len */))) {
		printf("Can't set isoc transfers size (high)\n");
		return ret;
	}

	buf[0] = J_CMD_ISOC_LEN_WRITE_LO;
	buf[1] = isoc_len & 0xFF;
	if ((ret = joker_cmd(joker, buf, 2, NULL /* in_buf */, 0 /* in_len */))) {
		printf("Can't set isoc transfers size (low)\n");
		return ret;
	}

	/* i2c core init */
	if ((ret = joker_i2c_init(joker))) {
		printf("Can't init i2c bus \n");
		return ret;
	}

	/* power down all chips
	 * will be enabled later on-demand */
	joker_reset(joker, 0xFF /* switch all chips to reset */);


	// init EN50221 stuff (no actual CAM module processing starts here)
	if (joker_ci_en50221_init(joker)) {
		printf("Can't init EN50221 \n");
		return -EIO;
	}

	return 0;
}

/* release usb device */
int joker_close(struct joker_t * joker) {
	struct libusb_device_handle *dev = NULL;
	int ret = 0;

	if (!joker)
		return EINVAL;

	// stop CI EN50221 stack
	joker_ci_close(joker);
	printf("%s: CI stack stopped \n", __func__);

	stop_service_thread(joker);
	printf("%s: service thread stopped \n", __func__);

	if((ret = joker_i2c_close(joker)))
		return ret;

	dev = (struct libusb_device_handle *)joker->libusb_opaque;

	libusb_release_interface(dev, 0);
	if(dev)
		libusb_close(dev);
	joker->libusb_opaque = NULL; /* dev not valid anymore */
	printf("%s: done\n", __func__);
}

/* exchange with FPGA over USB
 * EP2 OUT EP used as joker commands (jcmd) source
 * EP1 IN EP used as command reply storage
 * return 0 if success
 *
 * thread safe
 */
int joker_io(struct joker_t * joker, struct jcmd_t * jcmd) {
	struct libusb_device_handle *dev = NULL;
	unsigned char buf[JCMD_BUF_LEN];
	int ret = 0, transferred = 0;
	pthread_mutex_t *mux = NULL;

	if (!joker || !joker->io_mux_opaq)
		return -EINVAL;

	mux = (pthread_mutex_t*)joker->io_mux_opaq;
	dev = (struct libusb_device_handle *)joker->libusb_opaque;

	pthread_mutex_lock(mux);
	ret = libusb_bulk_transfer(dev, USB_EP2_OUT, jcmd->buf, jcmd->len, &transferred, 2000);
	if (ret < 0 || transferred != jcmd->len) {
		pthread_mutex_unlock(mux);
		jdebug("%s: USB bulk transaction failed. cmd=0x%x len=%d ret=%d transferred=%d \n",
				__func__, jcmd->buf[0], jcmd->len, ret, transferred);
		return -EIO;
	}

	/* jcmd expect some reply */
	if (jcmd->in_len > 0) {
		/* read collected data */
		ret = libusb_bulk_transfer(dev, USB_EP1_IN, jcmd->in_buf, jcmd->in_len, &transferred, 2000);
		if (ret < 0 || transferred != jcmd->in_len) {
			printf("%s: failed to read reply. ret=%d transferred=%d expected %d\n",
					__func__, ret, transferred, jcmd->in_len );
			pthread_mutex_unlock(mux);
			return -EIO;
		}
	}
	pthread_mutex_unlock(mux);

	return 0;
}

int joker_send_ts_loop(struct joker_t * joker, unsigned char *buf, int len) {
	struct libusb_device_handle *dev = NULL;
	int ret = 0, transferred = 0;

	if (!joker)
		return -EINVAL;

	dev = (struct libusb_device_handle *)joker->libusb_opaque;

	ret = libusb_bulk_transfer(dev, USB_EP4_OUT, buf, len, &transferred, 0);
	if (ret < 0 || transferred != len) {
		jdebug("%s: USB bulk transaction failed. ret=%d transferred=%d \n", __func__, ret, transferred);
		return -EIO;
	}

	return 0;
}



int joker_cmd(struct joker_t * joker, unsigned char *data, int len, unsigned char * in_buf, int in_len) {
	int ret = 0;
	struct jcmd_t jcmd;
	int i = 0;

	if (!joker)
		return -EINVAL;

	jcmd.buf = data;
	jcmd.len = len;
	jcmd.in_buf = in_buf;
	jcmd.in_len = in_len;

	if ((ret = joker_io(joker, &jcmd)))
		return ret;

	return 0;
}
