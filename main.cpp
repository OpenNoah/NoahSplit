#include <iostream>
#include <iomanip>
#include <string>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cstdint>
#include <boost/filesystem.hpp>

#pragma pack(push, 1)
struct header_t {
	union {
		struct {
			char tag[8];
			uint32_t ver;
		};
		uint8_t _blk[64];
	};
	union {
		struct {
			uint32_t size;
			uint32_t offset;
			uint32_t ver;
			uint32_t fstype;
			uint32_t crc;
			char dev[];
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
	static const char *pfstype[10] = {
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
	switch (v) {
	case 0:
	case 6:
	case 7:
	case 8:
		return pfstype[v];
	}
	return "unknown" + std::to_string(v);
}

void create(const std::string &in, const std::string &out)
{
	throw std::runtime_error("Unimplemented");
}

void dump(std::ifstream &in, std::ofstream &out, uint32_t offset, uint32_t size)
{
	if (!in.seekg(offset))
		throw std::runtime_error("Unexpected EOF at offset " + std::to_string(offset));
	static const uint32_t block = 4 * 1024 * 1024;		// Block size 4MiB
	uint8_t buf[block];
	while (size) {
		uint32_t s = std::min(block, size);
		in.read(reinterpret_cast<char *>(buf), s);
		out.write(reinterpret_cast<char *>(buf), s);
		size -= s;
	}
}

void extract(const std::string &in, const std::string &out, bool ext)
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
	std::ofstream sout(out);
	if (!sout.is_open())
		throw std::runtime_error("Could not open output file " + out);

	header_t &h(*reinterpret_cast<header_t *>(header));
	sout << "[Header]" << std::endl;
	sout << "tag=" << h.tag << std::endl;
	sout << "ver=0x" << std::hex << std::setfill('0') << std::setw(8) << h.ver << std::endl;

	auto *s = h.pkg;
	for (unsigned long i = 1; i < sizeof(header)/sizeof(s->_blk); i++, s++) {
		if (!s->size)
			continue;
		std::ostringstream sfilename;
		sfilename << "segment" << std::dec << std::setfill('0') << std::setw(2) << i << ".bin";
		std::string filename(sfilename.str());

		sout << std::endl << "[pkg]" << std::endl;
		sout << "name=sgmnt" << std::dec << std::setfill('0') << std::setw(2) << i << std::endl;
		sout << "idx=" << i << std::endl;
		sout << "include=1" << std::endl;
		sout << "file=" << filename << std::endl;
		sout << "ver=0x" << std::hex << std::setfill('0') << std::setw(8) << s->ver << std::endl;
		sout << "dev=" << s->dev << std::endl;
		sout << "fstype=" << fstype(s->fstype) << std::endl;

		// Extract segment to file
		if (!ext)
			continue;
		boost::filesystem::path p(out);
		filename = (p.parent_path() / filename).native();
		std::ofstream sbin(filename, std::ios::binary);
		if (!sbin.is_open())
			throw std::runtime_error("Could not open output file " + filename);
		std::cerr << "if=" << in << " of=" << filename
			<< " skip=" << s->offset << " size=" << s->size << std::endl;
		dump(sin, sbin, s->offset, s->size);
		sbin.close();
	}

	sin.close();
}

int main(int argc, char *argv[])
{
	std::string in, out, dir(".");
	enum {OpCreate, OpExtract, OpSplit} op = OpCreate;
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
		} else if (arg.compare("--create") == 0) {
			op = OpCreate;
		} else if (arg.compare("--extract") == 0) {
			op = OpExtract;
		} else if (arg.compare("--split") == 0) {
			op = OpSplit;
		} else {
			std::cerr << "Unknown argument: " << arg << std::endl;
			help = true;
		}
	}
	if (out.empty())
		help = true;

	if (help) {
		std::cout << "Usage:" << std::endl;
		std::cout << "    " << argv[0] << " [--create] input.pkg output.bin" << std::endl;
		std::cout << "    " << argv[0] << " [--split|--extract] input.bin output.pkg" << std::endl;
		return 1;
	}

	try {
		if (op == OpCreate)
			create(in, out);
		else
			extract(in, out, op == OpExtract);
	} catch (std::exception &e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return 1;
	}
	return 0;
}
