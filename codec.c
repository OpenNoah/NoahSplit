#include <malloc.h>
#include "codec.h"

static const char code[0x10] = {\
	0x00, 0x02, 0x01, 0x03, 0x08, 0x0A, 0x09, 0x0B, \
	0x04, 0x06, 0x05, 0x07, 0x0C, 0x0E, 0x0D, 0x0F};

unsigned char *codec(const int num, const unsigned char *input)
{
	unsigned char *output = malloc(num * sizeof(char));
	int i;
	for (i = 0; i < num; i++) {
		*(output + i) = code[*(input + i) / 0x10] * 0x10 + \
				code[*(input + i) % 0x10];
#ifdef DEBUG
		printf("Input: %x, Output: %x\n", *(input + i), *(output + i));
#endif
	}
	return output;
}

unsigned char charcodec(const unsigned char input)
{
	return code[input / 0x10] * 0x10 + code[input % 0x10];
}
