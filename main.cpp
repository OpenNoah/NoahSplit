#include <algorithm>
#include <iostream>
#include <iomanip>
#include <string>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cstdint>
#include <cstring>
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

uint32_t fstype(const std::string &str)
{
	if (str == "none")
		return 0;
	else if (str == "raw")
		return 6;
	else if (str == "nor")
		return 7;
	else if (str == "ubifs")
		return 8;
	else if (str.compare(0, 7, "unknown") == 0)
		return strtoul(str.data() + 7, nullptr, 0);
	else
		throw std::runtime_error("Unrecognised filesystem type: " + str);
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

void copy(std::ofstream &out, std::ifstream &in, uint32_t size, uint32_t align)
{
	static const uint32_t block = 4 * 1024 * 1024;	// Block size 4MiB
	uint32_t padding = (align - (size % align)) % align;
	uint8_t buf[block];
	while (size) {
		uint32_t s = std::min(block, size);
		in.read(reinterpret_cast<char *>(buf), s);
		out.write(reinterpret_cast<char *>(buf), s);
		size -= s;
	}
	if (padding) {
		bzero(buf, padding);
		out.write(reinterpret_cast<char *>(buf), padding);
	}
}

void append(header_t::pkg_t &s, std::ofstream &sout, const std::string &out,
		const boost::filesystem::path &parent, const std::string &file)
{
	std::string filename((parent / file).native());
	std::ifstream sbin(filename);
	if (!sbin.is_open())
		throw std::runtime_error("Could not open input file " + filename);

	// Get file size
	sbin.seekg(0, std::ios::end);
	s.size = sbin.tellg();
	sbin.seekg(0);

	std::clog << "if=" << filename << " of=" << out << " seek=" << sout.tellp() << " size=" << s.size << std::endl;
	copy(sout, sbin, s.size, 512);	// Align to 512-byte boundary for mount
	s.crc = 0;
}

void create(const std::string &in, const std::string &out)
{
	std::ifstream sin(in);
	if (!sin.is_open())
		throw std::runtime_error("Could not open input file " + in);

	std::ofstream sout(out);
	if (!sout.is_open())
		throw std::runtime_error("Could not open output file " + out);

	// Create header of size 2k bytes
	uint8_t header[2048] = {0};
	header_t &h(*reinterpret_cast<header_t *>(header));
	sout.write(reinterpret_cast<char *>(header), sizeof(header));

	boost::filesystem::path parent(boost::filesystem::path(in).parent_path());
	struct {
		uint32_t idx, include = 0;
		uint32_t ver, fstype, crc;
		std::string file, dev;
		bool crcovw = false;		// CRC overwrite
	} pkg;
	auto fappend = [&] {
		if (pkg.include) {
			auto &s = h.pkg[pkg.idx - 1];
			s.offset = sout.tellp();
			s.ver = pkg.ver;
			s.fstype = pkg.fstype;
			strncpy(s.dev, pkg.dev.c_str(), sizeof(s.dev));
			append(s, sout, out, parent, pkg.file);
			if (pkg.crcovw)
				s.crc = pkg.crc;
			// Reset
			pkg.include = 0;
			pkg.crcovw = false;
		}
	};

	enum {OpHeader, OpPkg} op = OpHeader;
	std::string line;
	uint32_t lnum = 0;
	while (std::getline(sin, line)) {
		lnum++;
		if (line.empty() || line[0] == '#')
			continue;
		if (line.compare("[header]") == 0) {
			op = OpHeader;
		} else if (line.compare("[pkg]") == 0) {
			fappend();
			op = OpPkg;
		} else if (op == OpHeader) {
			if (line.compare(0, 4, "tag=") == 0)
				strncpy(h.tag, line.data() + 4, sizeof(h.tag));
			else if (line.compare(0, 4, "ver=") == 0)
				h.ver = strtoul(line.data() + 4, nullptr, 0);
			else
				throw std::runtime_error("Unrecognised header configuration at " +
					in + ":" + std::to_string(lnum) + ": " + line);
		} else {
			if (line.compare(0, 5, "name=") == 0)
				;
			else if (line.compare(0, 4, "idx=") == 0)
				pkg.idx = strtoul(line.data() + 4, nullptr, 0);
			else if (line.compare(0, 8, "include=") == 0)
				pkg.include = strtoul(line.data() + 8, nullptr, 0);
			else if (line.compare(0, 5, "file=") == 0)
				pkg.file = line.substr(5);
			else if (line.compare(0, 4, "ver=") == 0)
				pkg.ver = strtoul(line.data() + 4, nullptr, 0);
			else if (line.compare(0, 4, "dev=") == 0)
				pkg.dev = line.substr(4);
			else if (line.compare(0, 7, "fstype=") == 0)
				pkg.fstype = fstype(line.substr(7));
			else if (line.compare(0, 4, "crc=") == 0) {
				pkg.crc = strtoul(line.data() + 4, nullptr, 0);
				pkg.crcovw = true;
			} else
				throw std::runtime_error("Unrecognised package configuration at " +
					in + ":" + std::to_string(lnum) + ": " + line);
		}
	}
	fappend();

	// Encrypt and update header
	codec(static_cast<void *>(header), sizeof(header));
	sout.seekp(0);
	sout.write(reinterpret_cast<char *>(header), sizeof(header));

	sin.close();
	sout.close();
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
	sout << "[header]" << std::endl;
	sout << "tag=" << std::string(h.tag, sizeof(h.tag)).c_str() << std::endl;
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
		sout << "dev=" << std::string(s->dev, sizeof(s->dev)).c_str() << std::endl;
		sout << "fstype=" << fstype(s->fstype) << std::endl;
		sout << "# crc=0x" << std::hex << std::setfill('0') << std::setw(8) << s->crc << std::endl;

		// Extract segment to file
		if (!ext)
			continue;
		boost::filesystem::path p(out);
		filename = (p.parent_path() / filename).native();
		std::ofstream sbin(filename, std::ios::binary);
		if (!sbin.is_open())
			throw std::runtime_error("Could not open output file " + filename);
		std::clog << "if=" << in << " of=" << filename << " skip=" << s->offset << " size=" << s->size << std::endl;
		if (!sin.seekg(s->offset))
			throw std::runtime_error("Unexpected EOF at " + in + " offset " + std::to_string(s->offset));
		copy(sbin, sin, s->size, 1);
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
		} else if (arg.compare("--help") == 0) {
			help = true;
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
