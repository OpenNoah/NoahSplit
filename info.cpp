#include <algorithm>
#include <iostream>
#include <iomanip>
#include <string>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cstdint>
#include <cstring>

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

void codec(void *p, unsigned long size)
{
	if (size % 8)
		throw std::runtime_error("Unexpected codec block size " + std::to_string(size));
	uint64_t *pv = static_cast<uint64_t *>(p);
	while (size) {
		uint64_t v = *pv;
		// Swap every 2 bits
		*pv++ = ((v & 0xaaaaaaaaaaaaaaaa) >> 1) | ((v & 0x5555555555555555) << 1);
		size -= 8;
	}
}

std::string fstype(uint32_t v)
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
	return "unknown" + std::to_string(v);
}

void info(const std::string &in)
{
	std::ifstream sin(in, std::ios::binary);
	if (!sin.is_open())
		throw std::runtime_error("Could not open input file " + in);

	// Read header of size 2k bytes
	uint8_t header[2048];
	if (!sin.read(reinterpret_cast<char *>(header), sizeof(header)))
		throw std::runtime_error("Unexpected EOF from " + in);
	codec(static_cast<void *>(header), sizeof(header));

	// Write segment configuration
	header_t &h(*reinterpret_cast<header_t *>(header));
	std::cout << std::string(h.tag, sizeof(h.tag)).c_str()
		<< "\t0x" << std::hex << std::setfill('0') << std::setw(8) << h.ver << std::endl;

	auto *s = h.pkg;
	for (unsigned long i = 1; i < sizeof(header)/sizeof(s->_blk); i++, s++) {
		if (!s->size)
			continue;

		std::cout << i << "\t0x" << std::hex << std::setfill('0') << std::setw(8) << s->ver
			<< "\t" << std::string(s->dev, sizeof(s->dev)).c_str()
			<< "\t" << fstype(s->fstype)
			<< "\t0x" << std::hex << std::setfill('0') << std::setw(8) << s->offset
			<< "\t0x" << std::hex << std::setfill('0') << std::setw(8) << s->size
			<< "\t0x" << std::hex << std::setfill('0') << std::setw(8) << s->crc << std::endl;
	}

	sin.close();
}

int main(int argc, char *argv[])
{
	std::string in;
	bool help = false;

	char **parg = &argv[1];
	for (int i = argc - 1; i--; parg++) {
		std::string arg(*parg);
		if (arg.compare(0, 2, "--") != 0) {
			if (in.empty()) {
				in = arg;
			} else {
				std::cerr << "Extra argument: " << arg << std::endl;
				help = true;
			}
		} else if (arg.compare("--help") == 0) {
			help = true;
		} else {
			std::cerr << "Unknown argument: " << arg << std::endl;
			help = true;
		}
	}
	if (in.empty())
		help = true;

	if (help) {
		std::cout << "Usage:" << std::endl;
		std::cout << "    " << argv[0] << " input.bin" << std::endl;
		return 1;
	}

	try {
		info(in);
	} catch (std::exception &e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return 1;
	}
	return 0;
}
