#include <algorithm>
#include <iostream>
#include <iomanip>
#include <string>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cstdint>
#include <cstring>

// XOR pattern extracted from NP890 update.bin
static const uint8_t pattern_890[] = {
	0x38, 0x20, 0x08, 0x31, 0x19, 0x01, 0x2a, 0x12,  0x3b, 0x23, 0x2e, 0x16, 0x3d, 0x25, 0x0d, 0x34,
	0x1c, 0x04, 0x0b, 0x10, 0x00, 0x1b, 0x28, 0x10,  0x39, 0x21, 0x09, 0x32, 0x1a, 0x02, 0x2b, 0x36,
	0x1e, 0x06, 0x2d, 0x15, 0x3c, 0x24, 0x0c, 0x13,  0x0d, 0x17, 0x02, 0x30, 0x18, 0x00, 0x29, 0x11,
	0x3a, 0x22, 0x0a, 0x33, 0x3e, 0x26, 0x0e, 0x35,  0x1d, 0x05, 0x2c, 0x14, 0x1b, 0x03, 0x0a, 0x04,
};

void codec_xor(void *p, unsigned long size, const void *pattern, const unsigned long psize)
{
	if (size % 8)
		throw std::runtime_error("Unexpected codec block size " + std::to_string(size));
	if (psize % 8)
		throw std::runtime_error("Unexpected pattern block size " + std::to_string(psize));

	uint64_t *pv = static_cast<uint64_t *>(p);
	const uint64_t *pp = static_cast<const uint64_t *>(pattern);
	unsigned long i = 0;
	unsigned long ps = psize / 8;
	while (size) {
		*pv++ ^= *(pp + i);
		i = (i + 1) % ps;
		size -= 8;
	}
}

void extract(const std::string &in, const std::string &out, const void *pp, const unsigned long psize, unsigned long offset, unsigned long size)
{
	std::ifstream sin(in, std::ios::binary);
	if (!sin.is_open())
		throw std::runtime_error("Could not open input file " + in);

	if (!sin.seekg(offset))
		throw std::runtime_error("Could not seek to offset " + std::to_string(offset));

	std::ofstream sout(out, std::ios::binary);
	if (!sout.is_open())
		throw std::runtime_error("Could not open output file " + out);

	// Read file in blocks of 4k bytes
	uint8_t block[4096];
	unsigned long read = 0;
	while (sin.read(reinterpret_cast<char *>(block), sizeof(block)), (read = sin.gcount()) != 0) {
		unsigned long wsize = size ? std::min(size, read) : read;
		unsigned long bsize = (wsize + 7) / 8 * 8;
		codec_xor(block, bsize, pp, psize);
		sout.write(reinterpret_cast<char *>(block), wsize);
		if (size) {
			size -= wsize;
			if (size == 0)
				break;
		}
	}

	sout.close();
	sin.close();
}

int main(int argc, char *argv[])
{
	std::string in, out;
	const void *pp = 0;
	unsigned long psize = 0;
	unsigned long offset = 0, size = 0;
	bool help = false;

	char **parg = &argv[1];
	for (int i = argc - 1; i--; parg++) {
		std::string arg(*parg);
		if (arg.compare(0, 2, "--") != 0) {
			if (in.empty()) {
				in = arg;
			} else if (out.empty()) {
				out = arg;
			} else {
				std::cerr << "Extra argument: " << arg << std::endl;
				help = true;
			}
		} else if (arg.compare("--pattern=np890") == 0) {
			pp = pattern_890;
			psize = sizeof(pattern_890);
		} else if (arg.compare(0, 9, "--offset=") == 0) {
			offset = std::stoul(arg.substr(9), 0, 0);
		} else if (arg.compare(0, 7, "--size=") == 0) {
			size = std::stoul(arg.substr(7), 0, 0);
		} else if (arg.compare("--help") == 0) {
			help = true;
		} else {
			std::cerr << "Unknown argument: " << arg << std::endl;
			help = true;
		}
	}
	if (!pp || out.empty())
		help = true;

	if (help) {
		std::cout << "Usage:" << std::endl;
		std::cout << "    " << argv[0] << " --pattern=np890 [--offset=8192] [--size=2048] input.bin output.bin" << std::endl;
		std::cout << "Available XOR patterns: np890" << std::endl;
		return 1;
	}

	try {
		extract(in, out, pp, psize, offset, size);
	} catch (std::exception &e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return 1;
	}
	return 0;
}
