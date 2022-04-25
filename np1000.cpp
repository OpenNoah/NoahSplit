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

enum {
	FsNone,
	FsMsdos,
	FsUnknown2,
	FsYaffs,
	FsRawNand,
	FsUnknown5,
	FsRaw,
	FsNor,
	FsUbifs,
} fs_type_t;

void copy(std::ofstream &out, std::ifstream &in, unsigned long size, unsigned long align);
uint32_t crc32(uint32_t crc, const void *buf, size_t size);

static unsigned long tag_ubifs_leb_size(const char *tag)
{
	if (strcmp(tag, "np1300") == 0)
		return 252 * 1024;
	else if (strcmp(tag, "np1500") == 0)
		return 252 * 1024;
	else if (strcmp(tag, "np1501") == 0)
		return 504 * 1024;
	else if (strcmp(tag, "np1380") == 0)
		return 504 * 1024;
	else if (strcmp(tag, "np2150") == 0)
		return 504 * 1024;
	//throw std::runtime_error("Unknown ubifs LEB size for " + std::string(tag));
	return 252 * 1024;
}

static void tag_nand_size(const char *tag, unsigned long &page, unsigned long &oob)
{
	if (strcmp(tag, "np1100") == 0) {
		page = 2048;
		oob = 64;
		return;
	}
	throw std::runtime_error("Unknown NAND size for " + std::string(tag));
}

static uint32_t fstype(const std::string &str)
{
	if (str == "none")
		return FsNone;
	else if (str == "msdos")
		return FsMsdos;
	else if (str == "raw")
		return FsRaw;
	else if (str == "nor")
		return FsNor;
	else if (str == "yaffs")
		return FsYaffs;
	else if (str == "nand")
		return FsRawNand;
	else if (str == "ubifs")
		return FsUbifs;
	else if (str.compare(0, 7, "unknown") == 0)
		return strtoul(str.data() + 7, nullptr, 0);
	else
		throw std::runtime_error("Unrecognised filesystem type: " + str);
}

static std::string fstype(uint32_t v)
{
	static const char *pfstype[] = {
		"none",
		"msdos",
		"unknown2",
		"yaffs",
		"nand",
		"unknown5",
		"raw",
		"nor",
		"ubifs",
	};
	if (v < 9)
		return pfstype[v];
	return "unknown" + std::to_string(v);
}

static void codec(void *p, unsigned long size)
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

uint32_t np_crc32(uint32_t crc, const void *buf, size_t size)
{
	return 0xffffffff ^ crc32(crc ^ 0xffffffff, buf, size);
}

uint32_t np_crc32(std::ifstream &in, unsigned long size)
{
	static const unsigned long block = 4 * 1024 * 1024;	// Block size 4MiB
	uint8_t buf[block];
	uint32_t crc = 0;
	while (size) {
		unsigned long s = std::min(block, size);
		in.read(reinterpret_cast<char *>(buf), s);
		crc = np_crc32(crc, buf, s);
		size -= s;
	}
	return crc;
}

int is_unmap_block(uint8_t *buf, uint32_t size)
{
	for (uint32_t i = 0; i < size; i++)
		if (buf[i] != 0xff)
			return 0;
	return 1;
}

uint32_t np_crc32_ubifs(std::ifstream &in, unsigned long size, unsigned long leb_size)
{
	const unsigned long block = leb_size + 4;
	uint8_t buf[block];
	uint32_t crc = 0;
	while (size >= 4) {
		unsigned long s = std::min(block, size);
		in.read(reinterpret_cast<char *>(buf), s);
		// ubirefimg: First u32 means number of skipped unmapped LEBs
		// is_unmap_block should never return 1 for ubirefimg images
		if (is_unmap_block(buf + 4, s - 4) == 0)
			crc = np_crc32(crc, buf + 4, s - 4);
		size -= s;
	}
	return crc;
}

uint32_t np_crc32_nand(std::ifstream &in, unsigned long size, unsigned long page, unsigned long oob)
{
	const unsigned long block = page + oob;
	uint8_t buf[block];
	uint32_t crc = 0;
	while (size >= 4) {
		unsigned long s = std::min(block, size);
		in.read(reinterpret_cast<char *>(buf), s);
		crc = np_crc32(crc, buf, s < page ? s : page);
		size -= s;
	}
	return crc;
}

static void verify_crc32(std::string path, const header_t::pkg_t &s, const char *tag)
{
	std::ifstream sbin(path);
	uint32_t crc = 0;
	if (s.fstype == FsUbifs) {
		unsigned long leb_size = tag_ubifs_leb_size(tag);
		crc = np_crc32_ubifs(sbin, s.size, leb_size);
	} else if (s.fstype == FsRawNand) {
		unsigned long page, oob;
		tag_nand_size(tag, page, oob);
		crc = np_crc32_nand(sbin, s.size, page, oob);
	} else {
		crc = np_crc32(sbin, s.size);
	}
	if (crc != s.crc)
		throw std::runtime_error("Checksum mismatch!");
}

static void append(const char *tag, header_t::pkg_t &s,
		std::ofstream &sout, const std::string &out,
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

	std::clog << "if=" << filename << " of=" << out << " seek=" << sout.tellp() << " size=" << s.size;
	copy(sout, sbin, s.size, 512);	// Align to 512-byte boundary for mount

	sbin.seekg(0);
	if (s.fstype == FsUbifs) {
		unsigned long leb_size = tag_ubifs_leb_size(tag);
		s.crc = np_crc32_ubifs(sbin, s.size, leb_size);
	} else if (s.fstype == FsRawNand) {
		unsigned long page, oob;
		tag_nand_size(tag, page, oob);
		s.crc = np_crc32_nand(sbin, s.size, page, oob);
	} else {
		s.crc = np_crc32(sbin, s.size);
	}
	std::clog << " crc=0x" << std::hex << std::setfill('0') << std::setw(8) << s.crc << std::endl;
}

void create_1000(const std::string &in, const std::string &out)
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
			append(h.tag, s, sout, out, parent, pkg.file);
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

void extract_1000(const std::string &in, const std::string &out, bool ext)
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
		std::clog << "if=" << in << " of=" << filename << " skip=" << s->offset << " size=" << s->size
			  << " crc=0x" << std::hex << std::setfill('0') << std::setw(8) << s->crc << std::endl;
		if (!sin.seekg(s->offset))
			throw std::runtime_error("Unexpected EOF at " + in + " offset " + std::to_string(s->offset));
		copy(sbin, sin, s->size, 1);
		sbin.close();
		verify_crc32(filename, *s, h.tag);
	}
}
