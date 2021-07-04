#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

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

void conv()
{
	uint8_t buf[2048];
	ssize_t s;
	while ((s = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
		if (s < sizeof(buf))
			memset(&buf[s], 0, sizeof(buf) - s);
		codec(buf, sizeof(buf));
		write(STDOUT_FILENO, buf, s);
	}
}

int main(int argc, char *argv[])
{
	conv();
	return 0;
}
