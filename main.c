#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <malloc.h>
#include <string.h>
#include "codec.h"

#define FILE_OUTPUT "output.log"
#define FILE_PKGCFG "pkg.cfg"

#define PUT(str) fputs(str, output)
#define CFGPUT(str) fputs(str, pkgcfg)
#define NEWLINE() fputc('\n', output);
#define CFGNEWLINE() fputc('\n', pkgcfg);

const char *fstype[10] = {
	"none",
	"Unknown 01",
	"Unknown 02",
	"Unknown 03",
	"Unknown 04",
	"Unknown 05",
	"raw",
	"nor",
	"ubifs",
	"Unknown 09",
};

char *dir;
int count = 0;
FILE *input, *output, *pkgcfg;

struct header
{
	unsigned char data[64];
	unsigned char *tag;
	unsigned int ver;
};

struct sector
{
	unsigned char data[64];
	unsigned int size;
	unsigned int offset;
	unsigned int ver;
	unsigned int fstype;
	unsigned int crc;
	unsigned char *dev;
};

unsigned int getint(const unsigned char *ch)
{
	return (*ch + *(ch + 1) * 0x00000100 + \
	*(ch + 2) * 0x00010000 + *(ch + 3) * 0x01000000);

}

struct header *readheader(void)
{
	unsigned char i;
	struct header *h = (struct header *)malloc(sizeof(struct header));
	for (i = 0; i < 64; i++)
		h->data[i] = charcodec(fgetc(input));
	h->ver = getint(&h->data[8]);
	h->tag = &h->data[0];
	return h;
}


struct sector *readsector(void)
{
	unsigned char i;
	struct sector *s = (struct sector *)malloc(sizeof(struct sector));
	for (i = 0; i < 64; i++)
		s->data[i] = charcodec(fgetc(input));
	s->size = getint(&s->data[0]);
	s->offset = getint(&s->data[4]);
	s->ver = getint(&s->data[8]);
	s->fstype = getint(&s->data[12]);
	s->crc = getint(&s->data[16]);
	s->dev = &s->data[20];
	return s;
}
 
void fputdata(const unsigned char data[64])
{
	unsigned char i;
	PUT("Original data:\n");
	for (i = 0; i < 64; i++) {
		fprintf(output, "%02x", data[i]);
		fputc(i % 16 == 15 ? '\n' : ' ', output);
		if (i % 16 == 7)
			fputc(' ', output);
	}
}

void fputheader(const struct header *h)
{
	PUT("[Header]\n");
	CFGPUT("[Header]\n");
	fprintf(output, "tag=%s\n", h->tag);
	fprintf(pkgcfg, "tag=%s\n", h->tag);
	fprintf(output, "ver=0x%08x\n", h->ver);
	fprintf(pkgcfg, "ver=0x%08x\n", h->ver);
	NEWLINE();
	CFGNEWLINE();
	fputdata(h->data);
}

void fputsector(const struct sector *s)
{
	PUT("[pkg]\n");
	CFGPUT("[pkg]\n");
	fprintf(output, "name=SECT%02d\n", count);
	fprintf(pkgcfg, "name=SECT%02d\n", count);
	fprintf(output, "idx=%d\n", count + 1);
	fprintf(pkgcfg, "idx=%d\n", count + 1);
	PUT("include=1\n");
	CFGPUT("include=1\n");
	fprintf(output, "file=Sector%02d.bin\n", count);
	fprintf(pkgcfg, "file=Sector%02d.bin\n", count);
	fprintf(output, "ver=0x%08x\n", s->ver);
	fprintf(pkgcfg, "ver=0x%08x\n", s->ver);
	fprintf(output, "dev=%s\n", s->dev);
	fprintf(pkgcfg, "dev=%s\n", s->dev);
	fprintf(output, "fstype=%s\n", fstype[s->fstype]);
	fprintf(pkgcfg, "fstype=%s\n", fstype[s->fstype]);
	fprintf(output, "# offset=0x%08x\n", s->offset);
	fprintf(output, "# size=0x%08x\n", s->size);
	fprintf(output, "# crc=0x%08x\n", s->crc);
	NEWLINE();
	CFGNEWLINE();
	fputdata(s->data);
}

void split(struct sector *s)
{
	char *p;
	unsigned int pos = s->size;
	long posbak;
	FILE *fp;
	
	// Open Sector??.bin file for write
	p = (char *)malloc(sizeof(char) * (strlen(dir) + strlen("/Sector??.bin")) + 1);
	sprintf(p, "%s/Sector%02d.bin", dir, count);
	fp = fopen(p, "wb");
	free(p);
	if (fp == NULL) {
		fprintf(stderr, "Error: Cannot open Sector%02d.bin for write\n", count);
		exit(1);
	}
	rewind(fp);
	
	// Write
	posbak = ftell(input);
	fseek(input, s->offset, SEEK_SET);
	while(pos--)
		fputc(fgetc(input), fp);
	fseek(input, posbak, SEEK_SET);
	fclose(fp);
}

int main(int argc, char *argv[])
{
	char *p, nosplit = 0;
	struct header *h;
	struct sector *s;
	
	// Check arguments
	if (argc != 3 && argc != 4) {
		fputs("Arguments error!\n", stderr);
		fputs("Noahsplit input-file output-directory [no]\n", stderr);
		return 1;
	}
	if (argc == 4 && strcmp(argv[3], "no") == 0)
		nosplit++;
	
	// Open input file for read
	input = fopen(argv[1], "rb");
	if (input == NULL) {
		fputs("Error: Cannot open input file\n", stderr);
		return 1;
	}
	rewind(input);
	
	// Output directory
	dir = argv[2];
	
	// Open output.log file for write
	p = malloc(sizeof(char) * (strlen(dir) + strlen("/" FILE_OUTPUT)) + 1);
	strcpy(p, dir);
	strcat(p, "/" FILE_OUTPUT);
	output = fopen(p, "wb");
	free(p);
	if (output == NULL) {
		fputs("Error: Cannot open " FILE_OUTPUT " for write\n", stderr);
		return 1;
	}
	rewind(output);
	
	// Open pkg.cfg file for write
	p = malloc(sizeof(char) * (strlen(dir) + strlen("/" FILE_PKGCFG)) + 1);
	strcpy(p, dir);
	strcat(p, "/" FILE_PKGCFG);
	pkgcfg = fopen(p, "wb");
	free(p);
	if (pkgcfg == NULL) {
		fputs("Error: Cannot open " FILE_PKGCFG " for write\n", stderr);
		return 1;
	}
	rewind(pkgcfg);
	
	// Special first sector
	h = readheader();
	puts("Analysing header...");
	PUT("Header:\n");
	fputheader(h);
	free(h);
	
	// Main loop
	while (ftell(input) < 2048) {
		s = readsector();
		if (s->size == 0) {
			free(s);
			count++;
			continue;
		}
		printf("Analysing sector %d...", count);
		NEWLINE();
		fprintf(output, "Data (Sector %d):\n", count);
		fputsector(s);
		if (nosplit) {
			putchar('\n');
		} else {
			puts(" Spliting...");
			split(s);
		}
		free(s);
		count++;
	}

	// Finished, close files
	fclose(output);
	fclose(input);
	puts("All finished.");
	return 0;
}
