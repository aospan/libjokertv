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
#include <errno.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <joker_i2c.h>
#include <joker_fpga.h>

#define CHECK_ACK 1
#define DO_NOT_CHECK_ACK 0


/* helper funcs
 * prepare jcmd to exchange with FPGA
 */
int joker_i2c_read_cmd(struct joker_t * joker, int offset, unsigned char * data) {
	int ret = 0;
	struct jcmd_t jcmd;
	jcmd.buf[0] = J_CMD_I2C_READ;
	jcmd.buf[1] = offset;
	jcmd.len = 2;
	jcmd.in_len = 2;

	if ((ret != joker_io(joker, &jcmd)))
		return ret;

	*data = jcmd.in_buf[1];
	return 0;
}

int joker_i2c_write_cmd(struct joker_t * joker, int offset, unsigned char data) {
	int ret = 0;
	struct jcmd_t jcmd;
	jcmd.buf[0] = J_CMD_I2C_WRITE;
	jcmd.buf[1] = offset;
	jcmd.buf[2] = data;
	jcmd.len = 3;
	jcmd.in_len = 0;

	if ((ret != joker_io(joker, &jcmd)))
		return ret;

	return 0;
}

/* helper func
 * i2c read cycle with error control
 * return 0 if read success
 * return error code if fail
 * */
int joker_i2c_read_cycle(struct joker_t *joker, unsigned char * buf, int check_ack)
{
	int cnt = 1000; /* can't wait more */
	int ret = 0;

	while (cnt-- > 0) {
		if ((ret = joker_i2c_read_cmd(joker, OC_I2C_SR, buf)))
			return ret;

		/* TIP - transaction in progress */
		if (!(buf[0] & OC_I2C_TIP))
			break;
		
		jdebug("TIP. do one more cycle \n");
		cnt--;
		usleep(1000);
	}

	/* no ACK received */
	if (check_ack && (buf[0] & OC_I2C_ACK)) {
		jdebug("no ack\n");
		return ENODEV;
	}

	if (buf[0] & OC_I2C_AL) {
		jdebug("arbitration lost\n");
		return EIO; 
	}

	if (cnt <= 0) {
		jdebug("timeout\n");
		return EIO; 
	}

	return 0;
}

/* write bytes to i2c chip
 * chip - chip address (7 bit notation)
 * data, size - actual data to write
 * return 0 if success
 * return error code if fail
 *
 * resulting actual transaction on i2c bus:
 *	Start/chip - data[0] - data[1] ... data[n]/Stop
 */
int joker_i2c_write(struct joker_t *joker, uint8_t chip, unsigned char * data, int size)
{
	int i = 0, ret = 0;
	unsigned char buf[BUF_LEN];
	unsigned char cmd;

	if (!joker)
		return EINVAL;

	chip = chip << 1; /* convert i2c addr to 8 bit notation */

	/* dump data (if debug enabled) */
	jdebug("i2c:0x%x:W ", chip );
	for(i = 0; i < size; i++) {
		jdebug("0x%x ", data[i]);
	}
	jdebug("\n");

	/* write device address first */
	if ((ret = joker_i2c_write_cmd(joker, OC_I2C_TXR, chip)))
		return ret;

	/* actual bus transfer */
	cmd = OC_I2C_START | OC_I2C_WRITE;
	if (size == 0)
		cmd |= OC_I2C_STOP;
	if ((ret = joker_i2c_write_cmd(joker, OC_I2C_CR, cmd)))
		return ret;

	if ((ret = joker_i2c_read_cycle(joker, &buf[0], CHECK_ACK))) {
		jdebug("i2c_write: can't set chip address to the bus. ret=%d \n", ret);
		return ret;
	}

	for (i = 0; i < size; i++) {
		/* set data to bus */
		if ((ret = joker_i2c_write_cmd(joker, OC_I2C_TXR, data[i])))
			return ret;

		/* actual bus transfer */
		cmd = OC_I2C_WRITE;
		if ( (i+1) == size ) /* last byte */
			cmd |= OC_I2C_STOP;

		if ((ret = joker_i2c_write_cmd(joker, OC_I2C_CR, cmd)))
			return ret;

		if((ret = joker_i2c_read_cycle(joker, &buf[0], DO_NOT_CHECK_ACK)))
			return ret;
	}

	return 0;
}

/* read bytes from i2c chip
 * chip - chip address (7 bit notation)
 * return 0 if success
 * return error code if fail
 *
 * resulting actual transaction on i2c bus:
 *	Start/chip - data[0] - data[1] ... data[n]/Stop
 */
int joker_i2c_read(struct joker_t *joker, uint8_t chip, unsigned char * data, int size) {
	int i = 0, ret = 0;
	unsigned char buf[BUF_LEN];
	unsigned char cmd;

	if (!joker)
		return ENODEV;

	chip = ((chip << 1) | 0x01); /* convert i2c addr to 8 bit notation and add Read bit */

	/* write device address first */
	if ((ret = joker_i2c_write_cmd(joker, OC_I2C_TXR, chip)))
		return ret;

	/* actual bus transfer */
	cmd = OC_I2C_START | OC_I2C_WRITE;
	if (size == 0)
		cmd |= OC_I2C_STOP;
	if ((ret = joker_i2c_write_cmd(joker, OC_I2C_CR, cmd)))
		return ret;

	if ((ret = joker_i2c_read_cycle(joker, &buf[0], CHECK_ACK))) {
		jdebug("i2c_read: can't set chip address to the bus. ret=%d \n", ret);
		return ret;
	}

	for (i = 0; i < size; i++) {
		/* actual bus transfer */
		cmd = OC_I2C_READ;
		if ( (i+1) == size ) /* last byte */
			cmd |= OC_I2C_STOP | OC_I2C_NACK; /* TODO: check NACK option behaviour */
		// cmd |= OC_I2C_STOP;

		if ((ret = joker_i2c_write_cmd(joker, OC_I2C_CR, cmd)))
			return ret;

		if ((ret = joker_i2c_read_cycle(joker, &buf[0], DO_NOT_CHECK_ACK)))
			return ret;

		/* read saved byte */
		if((ret = joker_i2c_read_cmd(joker, OC_I2C_RXR, &data[i])))
			return ret;
	}

	/* dump data (if debug enabled) */
	jdebug("i2c:0x%x:R ", chip );
	for(i = 0; i < size; i++) {
		jdebug("0x%x ", data[i]);
	}
	jdebug("\n");

	return 0;
}

/* "ping" i2c address.
 * return 0 (success) if ACKed
 * return negative error code if no ACK received
 */
int joker_i2c_ping(struct joker_t *joker, uint8_t chip)
{
	int i = 0;
	unsigned char buf[BUF_LEN];
	unsigned char cmd;
	struct joker_i2c_t *i2c = NULL;
	int ret = 0;

	if (!joker)
		return EINVAL;

	chip = (chip << 1); /* convert i2c addr to 8 bit notation */

	/* write device address first */
	if ((ret = joker_i2c_write_cmd(joker, OC_I2C_TXR, chip)))
		return ret;

	/* actual bus transfer */
	cmd = OC_I2C_START | OC_I2C_WRITE | OC_I2C_STOP;
	if ((ret = joker_i2c_write_cmd(joker, OC_I2C_CR, cmd)))
		return ret;

	if ((ret = joker_i2c_read_cycle(joker, &buf[0], CHECK_ACK)))
		return ret;

	return 0;
}

/* Open joker I2C
 * return 0 if success
 * return negative error code if fail 
 * */
int joker_i2c_init(struct joker_t *joker)
{
	struct joker_i2c_t *i2c = NULL;
	int ret = 0;

	if (!joker)
		return ENODEV;

	i2c = (struct joker_i2c_t*)malloc(sizeof(struct joker_i2c_t));
	if (!i2c)
		return ENOMEM;

	joker->i2c_opaque = i2c;

	/* set i2c bus to 400kHz */
	if ((ret = joker_i2c_write_cmd(joker, OC_I2C_PRELO, OC_I2C_400K)))
		goto cleanup;

	if ((ret = joker_i2c_write_cmd(joker, OC_I2C_PREHI, 0x00)))
		goto cleanup;

	/* enable core */
	if ( (ret = joker_i2c_write_cmd(joker, OC_I2C_CTR, OC_I2C_CORE_ENABLE | OC_I2C_IRQ_ENABLE)) )
		goto cleanup;

	return 0;

cleanup:
	free(i2c);
	joker->i2c_opaque = NULL;
	return ret;
}

/* release i2c resources */
int joker_i2c_close(struct joker_t *joker) {
	struct joker_i2c_t *i2c = NULL;
	int ret = 0;

	if (!joker || !joker->i2c_opaque)
		return ENODEV;

	i2c = (struct joker_i2c_t*)joker->i2c_opaque;

	free(i2c);
	joker->i2c_opaque = NULL;

	return 0;
}
