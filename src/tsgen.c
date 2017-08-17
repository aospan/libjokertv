#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <stdbool.h>
#include <dvbpsi.h>
#include <psi.h>
#include <pat.h>
#include <descriptor.h>
#include <pmt.h>

#define TS_SIZE 188
// about 54MB of data
#define TS_LIMIT 300000

#define PROGRAM_NUMBER 13105
#define PMT_PID 1305
#define PCR_PID 0x200
#define ES_TYPE 6 // ISO/IEC 13818-1 Private PES data packets

// usec step for generated PCR
#define USEC_STEP 20000
static uint64_t pcr_usec = 0;

void help() {
	printf("tsgen utility for TS stream generation (pattern)\n");
	printf("Usage:\n");
	printf("	-f filename	TS stream output file\n");
	printf("	-s size		TS file size in bytes (default 54MB)\n");
}

static void writePSI(uint8_t* p_packet, dvbpsi_psi_section_t* p_section)
{
	p_packet[0] = 0x47;

	while(p_section)
	{
		size_t i_bytes_written = 0;
		uint8_t* p_pos_in_ts;
		uint8_t* p_byte = p_section->p_data;
		uint8_t* p_end =   p_section->p_payload_end
			+ (p_section->b_syntax_indicator ? 4 : 0);

		p_packet[1] |= 0x40;
		p_packet[3] = (p_packet[3] & 0x0f) | 0x10;

		p_packet[4] = 0x00; /* pointer_field */
		p_pos_in_ts = p_packet + 5;

		while((p_pos_in_ts < p_packet + 188) && (p_byte < p_end))
			*(p_pos_in_ts++) = *(p_byte++);
		while(p_pos_in_ts < p_packet + 188)
			*(p_pos_in_ts++) = 0xff;
		// i_bytes_written = fwrite(p_packet, 1, 188, stdout);
		if(i_bytes_written == 0)
		{ 
			fprintf(stderr,"eof detected ... aborting\n");
			return;
		}
		p_packet[3] = (p_packet[3] + 1) & 0x0f;

		while(p_byte < p_end)
		{ 
			p_packet[1] &= 0xbf;
			p_packet[3] = (p_packet[3] & 0x0f) | 0x10;

			p_pos_in_ts = p_packet + 4;

			while((p_pos_in_ts < p_packet + 188) && (p_byte < p_end))
				*(p_pos_in_ts++) = *(p_byte++);
			while(p_pos_in_ts < p_packet + 188)
				*(p_pos_in_ts++) = 0xff;
			// i_bytes_written = fwrite(p_packet, 1, 188, stdout);
			if(i_bytes_written == 0)
			{
				fprintf(stderr,"eof detected ... aborting\n");
				return;
			}
			p_packet[3] = (p_packet[3] + 1) & 0x0f;
		}

		p_section = p_section->p_next;
	}
}

static void message(dvbpsi_t *handle, const dvbpsi_msg_level_t level, const char* msg)
{
	switch(level)
	{   
		case DVBPSI_MSG_ERROR: fprintf(stderr, "Error: "); break;
		case DVBPSI_MSG_WARN:  fprintf(stderr, "Warning: "); break;
		case DVBPSI_MSG_DEBUG: fprintf(stderr, "Debug: "); break;
		default: /* do nothing */
				       return;
	}
	fprintf(stderr, "message: %s\n", msg);
}

/* generate packet with PCR */
int generate_pcr(uint8_t * packet)
{
	int64_t pcr = 0;
	int64_t pcr_27m = 0;
	int64_t pcr_90k = 0;

	packet[0] = 0x47;
	packet[1] = (PCR_PID >> 8)&0x1F;
	packet[2] = PCR_PID&0xFF;
	packet[3] = 0x20; // b'10 – adaptation field only, no payload,
	packet[4] = 0x07; // Number of bytes in the adaptation field immediately following this byte
	packet[5] = 0x10; // PCR flag set
	
	// 48 bit long (6 bytes)
	// Program clock reference, stored as 33 bits base, 6 bits reserved, 9
	// bits extension.
	// The value is calculated as base * 300 + extension.
	//
	// from so:
	// the PCR contains 33(PCR_Base)+6(PCR_const)+9(PCR_Ext) number of bits
	// and also it states that the first 33 bits are based on a 90 kHz clock
	// while the last 9 are based on a 27 MHz clock.PCR_const = 0x3F
	// PCR_Ext=0 PCR_Base=pts/dts

	pcr_27m = pcr_usec*27; //pcr in 27MHz base
	pcr_90k = pcr_27m/300; //pcr in 90Khz base
	pcr = pcr_90k << 9; // we lose here 27Mhz ext but it's not critical

	packet[ 6] = (pcr >> 34) & 0xff;
	packet[ 7] = (pcr >> 26) & 0xff;
	packet[ 8] = (pcr >> 18) & 0xff;
	packet[ 9] = (pcr >> 10) & 0xff;
	packet[10] = 0x7e | ((pcr & (1 << 9)) >> 2) | ((pcr & (1 << 8)) >> 8);
	packet[11] = pcr & 0xff;

	pcr_usec += USEC_STEP;

	return 0;
}

/* generate PAT */
int generate_pat(uint8_t * packet)
{
	dvbpsi_t *p_dvbpsi = NULL;
	dvbpsi_pat_t pat;
	dvbpsi_psi_section_t* p_section1;

	p_dvbpsi = dvbpsi_new(&message, DVBPSI_MSG_DEBUG);
	if (p_dvbpsi == NULL)
		return -ENOMEM;

	dvbpsi_pat_init(&pat, 1, 0, 0);
	pat.b_current_next = 1;
	dvbpsi_pat_program_add(&pat, PROGRAM_NUMBER, PMT_PID);
	p_section1 = dvbpsi_pat_sections_generate(p_dvbpsi, &pat, 4);

	packet[0] = 0x47;
	packet[1] = packet[2] = 0x00;
	packet[3] = 0x00;
	writePSI(packet, p_section1);

	dvbpsi_pat_empty(&pat);

	dvbpsi_delete(p_dvbpsi);

	return 0;
}

/* generate PMT */
int generate_pmt(uint8_t * packet)
{
	dvbpsi_t *p_dvbpsi = NULL;
	dvbpsi_psi_section_t* p_section1;
	dvbpsi_pmt_t pmt;
	dvbpsi_pmt_es_t* p_es;
	uint8_t data[] = "abcdefghijklmnopqrstuvwxyz";

	p_dvbpsi = dvbpsi_new(&message, DVBPSI_MSG_DEBUG);
	if (p_dvbpsi == NULL)
		return -ENOMEM;

	dvbpsi_pmt_init(&pmt, PROGRAM_NUMBER, 0, 0, PCR_PID);
	p_es = dvbpsi_pmt_es_add(&pmt, ES_TYPE, 0x177);
	p_es = dvbpsi_pmt_es_add(&pmt, 0x02 /* fake video streams */, 0x178);
	p_es = dvbpsi_pmt_es_add(&pmt, 0x04 /* fake audio streams */, 0x179);
	p_section1 = dvbpsi_pmt_sections_generate(p_dvbpsi, &pmt);

	/* TS packets generation */
	packet[0] = 0x47;
	packet[1] = (PMT_PID >> 8)&0x0f;
	packet[2] = PMT_PID&0xff;
	packet[3] = 0x00;

	writePSI(packet, p_section1);

	dvbpsi_DeletePSISections(p_section1);
	dvbpsi_pmt_empty(&pmt);
	dvbpsi_delete(p_dvbpsi);

	return 0;
}

int main(int argc, char **argv) {
	unsigned char pkt[TS_SIZE];
	int counter = 0;
	int pattern = 0;
	int i = 0, j = 0;
	FILE * ofd;
	int c;
	char *filename = "tsgen.ts";
	int size = TS_LIMIT;
	uint8_t pat_packet[TS_SIZE];
	uint8_t pmt_packet[TS_SIZE];
	uint8_t pcr_packet[TS_SIZE];

	memset(pat_packet, 0, TS_SIZE);
	memset(pmt_packet, 0, TS_SIZE);
	memset(pcr_packet, 0, TS_SIZE);

	while ((c = getopt (argc, argv, "f:s:")) != -1) {
		switch (c)
		{
			case 's':
				size = atoi(optarg)/TS_SIZE;
				break;
			case 'f':
				filename = optarg;
				break;
			default:
				help();
				return 0;
		}
	}

	ofd = fopen(filename, "w+");
	if (ofd < 0)
		return -1;

	printf("Generate PAT ... \n");
	if (generate_pat(&pat_packet[0]))
		return -1;

	printf("Generate PMT ... \n");
	if (generate_pmt(&pmt_packet[0]))
		return -1;
	printf("Generate PCR ... \n");
	if (generate_pcr(&pcr_packet[0]))
		return -1;

	fwrite(pcr_packet, TS_SIZE, 1, ofd);
	fwrite(pat_packet, TS_SIZE, 1, ofd);
	fwrite(pmt_packet, TS_SIZE, 1, ofd);

	/* pattern should be rolled as 0x00 -> 0xff */
	size = 256 * (size/256);
	printf("ts pkt count=%d \n", size );

	for (i = 0; i < size; i++) {
		pkt[0x00] = 0x47;
		pkt[0x01] = 0x01;
		pkt[0x02] = 0x77;
		pkt[0x03] = 0x10 | counter;

		for (j = 4; j < TS_SIZE; j++) {
			pkt[j] = pattern;
		}

		pattern++;
		if (pattern > 0xFF) {
			pattern = 0;
			// write PAT/PMT every 255 TS packets (about 47KB)
			fwrite(pat_packet, TS_SIZE, 1, ofd);
			fwrite(pmt_packet, TS_SIZE, 1, ofd);

			// insert new PCR
			if (generate_pcr(&pcr_packet[0]))
				return -1;

			fwrite(pcr_packet, TS_SIZE, 1, ofd);
		}

		counter++;
		if (counter > 0x0F)
			counter = 0;

		if (!fwrite(pkt, TS_SIZE, 1, ofd))
			return -1;
	}
	fclose(ofd);

	printf("TS stream generated. Please check %s file ... \n", filename);
}
