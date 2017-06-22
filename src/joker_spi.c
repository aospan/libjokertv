/* 
 * Access to Joker TV SPI bus
 * m25p80 128M SPI flash used (M25P128-VME6GB)
 * 
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
#include <unistd.h>
#include <sys/types.h>
#include <joker_tv.h>
#include <joker_spi.h>
#include <joker_fpga.h>

/* check SPI flash id
 * return 0 if OK
 */
int joker_flash_checkid(struct joker_t * joker)
{
	int ret = 0;
	unsigned char buf[JCMD_BUF_LEN];
	unsigned char in_buf[JCMD_BUF_LEN];

	memset(in_buf, 0x00, JCMD_BUF_LEN);
	memset(buf, 0x0, JCMD_BUF_LEN);

	// check SPI flash ID (should be 0x20 20 18)
	buf[0] = J_CMD_SPI;
	buf[1] = 0x9F; // read ID
	if ((ret = joker_cmd(joker, buf, 5, in_buf, 5)))
		return ret;
	jdebug("SPI flash id=0x%x %x %x \n", in_buf[2], in_buf[3], in_buf[4]);
	if (in_buf[2] != 0x20 && in_buf[3] != 0x20 && in_buf[4] != 0x18)
		return(-1);

	return 0;
}

/* erase sector at off on SPI flash 
 * return 0 if OK
 */
int joker_flash_erase_sector(struct joker_t * joker, int off)
{
	int ret = 0;
	unsigned char buf[JCMD_BUF_LEN];
	unsigned char in_buf[JCMD_BUF_LEN];

	memset(in_buf, 0x00, JCMD_BUF_LEN);
	memset(buf, 0x0, JCMD_BUF_LEN);


	buf[0] = J_CMD_SPI;
	buf[1] = 0x06;  // Write enable
	if ((ret = joker_cmd(joker, buf, 2, in_buf, 2)))
		return ret;
	jdebug("WE set done \n");

	buf[0] = J_CMD_SPI;
	buf[1] = 0xD8;  // sector erase
	buf[2] = ((off >> 16) & 0xFF);
	buf[3] = ((off >> 8) & 0xFF);
	buf[4] = (off & 0xFF);
	if ((ret = joker_cmd(joker, buf, 5, in_buf, 5)))
		return ret;
	printf("Sector %d erase started\n", off);

	while(1) {
		buf[0] = J_CMD_SPI;
		buf[1] = 0x05; // read status
		if ((ret = joker_cmd(joker, buf, 3, in_buf, 3)))
			return ret;
		jdebug("	SPI status=0x%x\n", in_buf[2]);
		if(in_buf[2] == 0x0)
			break;
		usleep(100*1000);
	}
	printf("Sector erase done\n");

	return 0;
}

int joker_flash_write_page(struct joker_t * joker, unsigned char * data, int off, int to_write)
{
	int ret = 0;
	unsigned char buf[JCMD_BUF_LEN];
	unsigned char in_buf[JCMD_BUF_LEN];

	memset(in_buf, 0x00, JCMD_BUF_LEN);
	memset(buf, 0x0, JCMD_BUF_LEN);


	buf[0] = J_CMD_SPI;
	buf[1] = 0x06;  // Write enable
	if ((ret = joker_cmd(joker, buf, 2, in_buf, 2)))
		return ret;
	jdebug("WE set done \n");

	// page program
	memset(buf, 0x0, JCMD_BUF_LEN);
	memset(in_buf, 0x0, JCMD_BUF_LEN);
	buf[0] = J_CMD_SPI;
	buf[1] = 0x02;  // page program
	buf[2] = ((off >> 16) & 0xFF);
	buf[3] = ((off >> 8) & 0xFF);
	buf[4] = (off & 0xFF);
	memcpy(buf+5, data, to_write);

	if ((ret = joker_cmd(joker, buf, to_write+5, in_buf, to_write+5)))
		return ret;
	jdebug("PP started\n");

	while(1) {
		buf[0] = J_CMD_SPI;
		buf[1] = 0x05; // read status
		if ((ret = joker_cmd(joker, buf, 3, in_buf, 3)))
			return ret;
		jdebug("	SPI status=0x%x\n", in_buf[2]);
		if(in_buf[2] == 0x0)
			break;
		usleep(1*1000);
	}
	jdebug("PP done\n");

	return 0;
}

/* write file to SPI flash
 * return 0 if OK
 */
int joker_flash_write(struct joker_t * joker, char * filename)
{
	FILE *fw = NULL;
	long size = 0;
	unsigned char * buf = NULL;
	int next_sector = 0, prev_sector = -1, to_write = 0, off = 0;

	fw = fopen((char*)filename, "rb");
	if (!fw){
		printf("Can't open fw file '%s' \n", filename);
		perror("");
		return(-1);
	} else {
		printf("FW file:%s opened\n", filename);
	}

	/* get file size */
	fseek(fw, 0L, SEEK_END);
	size = ftell(fw);
	fseek(fw, 0L, SEEK_SET);
	if (size <= 0 || size > 16*1024*1024) {
		printf("Invalid size(%d) for fw file '%s'\n", (int)size, filename);
		return(-1);
	}

	buf = malloc(size);
	if (!buf)
		return -1;

	if (fread(buf, 1, size, fw) != size) {
		printf("Can't read fw file '%s'\n", filename);
		return(-1);
	}

	/* now we ready to write fw into SPI flash 
	 * sector is 2 Mbit = 2097152 bit = 262144 bytes = 256 Kbytes
	 * page is 256 bytes
	 */
	for(off = 0; off < size; ){
		to_write = ((size - off) > J_SPI_PAGE_SIZE) ? J_SPI_PAGE_SIZE : (size - off);
		next_sector = (off + to_write) / J_SPI_SECTOR_SIZE;
		/* should we clean sector first */
		if(next_sector > prev_sector) {
			printf("cleaning sector %d at addr %d\n", next_sector, next_sector * J_SPI_SECTOR_SIZE);
			fflush(stdout);
			if(joker_flash_erase_sector(joker, next_sector * J_SPI_SECTOR_SIZE)) {
				printf("Can't erase sector at off=%d \n", next_sector * J_SPI_SECTOR_SIZE);
				return(-1);
			}
			prev_sector = next_sector;
		}
		jdebug("writing page at off %d to_write=%d \n", off, to_write);

		if(joker_flash_write_page(joker, buf + off, off, to_write)){
			printf("Can't write %d bytes at off=%d \n", to_write, off);
			return(-1);
		}
		if(!off)
			printf("Starting page programming ...\n");
		fflush(stdout);

		off += to_write;
	}

	return 0;
}
