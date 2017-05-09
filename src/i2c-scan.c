#include <stdio.h>
#include <oc_i2c.h>

static struct libusb_device_handle *dev = NULL;

int i2c_start()
{
	if((dev = i2c_init()))
	{
		return 0;
	} else {
		printf("FAIL open I2C\n");
		return -1;
	}
}

int i2c_stop()
{
	i2c_close(dev);
	return 0;
}

int main() {
	int i = 0;

	if (i2c_start())
		return -1;

	for(i = 0; i < 0x7F; i++) {
		if ( !oc_i2c_ping(dev, i) ) {
			printf("0x%x address ACKed on i2c bus\n", i );
		}
	}

	i2c_stop();
}
