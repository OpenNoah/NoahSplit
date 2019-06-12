#include <malloc.h>
#include "codec.h"

unsigned char *codec(const int num, const unsigned char *input)
{
	unsigned char *output = malloc(num * sizeof(char));
	const unsigned char *pi = input;
	unsigned char *po = output;
	int i = num;
	while (i--)
		*po++ = charcodec(*pi++);
	return output;
}

unsigned char charcodec(const unsigned char v)
{
	// Swap every 2 bits
	return ((v & 0xaa) >> 1) | ((v & 0x55) << 1);
}
