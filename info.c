#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#pragma pack(push, 1)
struct header_t {
	union {
		struct {
			char tag[8];
			uint32_t ver;
		};
		uint8_t _blk[64];
	};
	union pkg_t {
		struct {
			uint32_t size;
			uint32_t offset;
			uint32_t ver;
			uint32_t fstype;
			uint32_t crc;
			char dev[64 - 4 * 5];
		};
		uint8_t _blk[64];
	} pkg[31];
};
#pragma pack(pop)

static char strbuf[64];

void codec(void *p, unsigned long size)
{
	if (size % 8) {
		fprintf(stderr, "Unexpected codec block size %d\n", size);
		exit(1);
	}
	uint64_t *pv = (uint64_t *)p;
	while (size) {
		uint64_t v = *pv;
		// Swap every 2 bits
		*pv++ = ((v & 0xaaaaaaaaaaaaaaaa) >> 1) | ((v & 0x5555555555555555) << 1);
		size -= 8;
	}
}

const char *fstype(uint32_t v)
{
	static const char *pfstype[] = {
		"none",
		"unknown1",
		"unknown2",
		"unknown3",
		"unknown4",
		"unknown5",
		"raw",
		"nor",
		"ubifs",
	};
	switch (v) {
	case 0:
	case 6:
	case 7:
	case 8:
		return pfstype[v];
	}
	sprintf(strbuf, "unknown%d", v);
	return strbuf;
}

void info(const char *in)
{
	int fin = open(in, O_RDONLY);
	if (fin < 0) {
		fprintf(stderr, "Could not open input file %s: %d\n", in, errno);
		exit(1);
	}

	// Read header of size 2k bytes
	struct header_t h;
	if (read(fin, &h, sizeof(h)) !=  sizeof(h)) {
		fprintf(stderr, "Error reading from file %s: %d\n", in, errno);
		exit(1);
	}
	codec(&h, sizeof(h));

	// Write segment configuration
	bzero(strbuf, sizeof(strbuf));
	strncpy(strbuf, h.tag, sizeof(h.tag));
	printf("%s\t0x%08x\n", strbuf, h.ver);

	union pkg_t *s = h.pkg;
	for (unsigned long i = 1; i < sizeof(h)/sizeof(s->_blk); i++, s++) {
		if (!s->size)
			continue;

		bzero(strbuf, sizeof(strbuf));
		strncpy(strbuf, s->dev, sizeof(s->dev));
		printf("%d" "\t0x%08x" "\t%s", i, s->ver, strbuf);
		printf("\t%s" "\t0x%08x" "\t0x%08x" "\t0x%08x\n",
			fstype(s->fstype), s->offset, s->size, s->crc);
	}

	close(fin);
}

int main(int argc, char *argv[])
{
	const char *in = 0;
	int help = 0;

	char **parg = &argv[1];
	for (int i = argc - 1; i--; parg++) {
		const char *arg = *parg;
		if (strncmp(arg, "--", 2) != 0) {
			if (!in) {
				in = arg;
			} else {
				fprintf(stderr, "Extra argument: %s\n", arg);
				help = 1;
			}
		} else if (strcmp(arg, "--help") == 0) {
			help = 1;
		} else {
			fprintf(stderr, "Unknown argument: %s\n", arg);
			help = 1;
		}
	}
	if (!in)
		help = 1;

	if (help) {
		printf("Usage:\n    %s input.bin\n", argv[0]);
		return 1;
	}

	info(in);
	return 0;
}
