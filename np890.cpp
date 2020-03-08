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
#include <zlib.h>

void codec_xor(void *p, unsigned long size, const void *pattern, const unsigned long psize);

#pragma pack(push, 1)
union setup_t {
	uint32_t raw[35];
	struct {
		char version[32];
		char date[32];
		char model[32];
		char hostname[32];
		uint32_t autorun;
		uint32_t keeplogs;
		uint32_t dumpnand;
		uint32_t valid;
	};
	union {
		uint32_t raw[18];
		struct {
			char date[32];
			uint32_t autorun;
			uint32_t quiet;
			uint32_t _reserved0;
			uint32_t _reserved1;
			uint32_t keeplogs;
			uint32_t dumpnand;
			uint32_t _reserved2;
			uint32_t _reserved3;
			uint32_t _reserved4;
			uint32_t _reserved5;
		};
	} menu;
};

union device_t {
	uint32_t raw[7];
	struct {
		uint32_t type, dest, size, rawsize, compressed, pattern, cksum;
	};
};

union system_t {
	uint32_t raw[5];
	struct {
		uint32_t index, size;
		uint32_t rawsize;
		uint32_t compressed;
	};
};
#pragma pack(pop)

// XOR pattern extracted from NP890 update.bin
static const uint8_t pattern[] = {
	0x38, 0x20, 0x08, 0x31, 0x19, 0x01, 0x2a, 0x12,  0x3b, 0x23, 0x2e, 0x16, 0x3d, 0x25, 0x0d, 0x34,
	0x1c, 0x04, 0x0b, 0x10, 0x00, 0x1b, 0x28, 0x10,  0x39, 0x21, 0x09, 0x32, 0x1a, 0x02, 0x2b, 0x36,
	0x1e, 0x06, 0x2d, 0x15, 0x3c, 0x24, 0x0c, 0x13,  0x0d, 0x17, 0x02, 0x30, 0x18, 0x00, 0x29, 0x11,
	0x3a, 0x22, 0x0a, 0x33, 0x3e, 0x26, 0x0e, 0x35,  0x1d, 0x05, 0x2c, 0x14, 0x1b, 0x03, 0x0a, 0x04,
};

static std::string destination(uint32_t v)
{
	static const char *pdest[] = {
		"/dev/_nand0",
                "/dev/_nand1",
                "/dev/_nand2",
                "/dev/_nand3",
                "/dev/_nand4",
                "/dev/_nand5",
                "/dev/_nand6",
                "/dev/_nand7",
                "/tmp/sysdata.img",
	};
	if (v >= sizeof(pdest) / sizeof(pdest[0]))
		return "unknown" + std::to_string(v) + ".bin";
	return pdest[v];
}

static void zlib_inflate(void *zbuf, uint32_t zsize, void *ubuf, uint32_t usize)
{
	z_stream strm;
	strm.next_in   = reinterpret_cast<z_const Bytef *>(zbuf);
	strm.avail_in  = zsize;
	strm.total_in  = 0;
	strm.next_out  = reinterpret_cast<Bytef *>(ubuf);
	strm.avail_out = usize;
	strm.total_out = 0;
	strm.zalloc    = Z_NULL;
	strm.zfree     = Z_NULL;
	strm.opaque    = Z_NULL;

	// +32: Detect gzip or zlib
	int err = inflateInit2(&strm, 32 + MAX_WBITS);
	if (err != Z_OK) {
		inflateEnd(&strm);
		throw std::runtime_error("zlib inflate init error: " + std::string(zError(err)));
	}
	// Single step inflate
	err = inflate(&strm, Z_FINISH);
	inflateEnd(&strm);
	if (err != Z_STREAM_END)
		throw std::runtime_error("zlib inflate error: " + std::string(zError(err)));
	if (strm.total_in != zsize)
		throw std::runtime_error("zlib compressed size mismatch: " + std::to_string(strm.total_in) +
				" should be " + std::to_string(zsize));
	if (strm.total_out != usize)
		throw std::runtime_error("zlib uncompressed size mismatch: " + std::to_string(strm.total_out) +
				" should be " + std::to_string(usize));
}

static void copy(std::ifstream &sin, const std::string &out, const std::string &file,
		long offset, unsigned long size, unsigned long align, int codec, bool ext, bool inflate = false)
{
	if (offset >= 0 && !sin.seekg(offset))
		throw std::runtime_error("Could not seek to offset " + std::to_string(offset));

	boost::filesystem::path p(out);
	std::string filename = (p.parent_path() / file).native();
	std::ofstream sout(filename, std::ios::binary);
	if (ext && !sout.is_open())
		throw std::runtime_error("Could not open output file " + filename);

	// Select XOR pattern
	uint64_t xpattern;
	const uint64_t *px = 0;
	uint32_t xsize = 0;
	if (codec < 0) {
		px = reinterpret_cast<const uint64_t *>(pattern);
		xsize = sizeof(pattern);
	} else {
		memset(&xpattern, codec, sizeof(xpattern));
		px = &xpattern;
		xsize = sizeof(xpattern);
	}

	void *ubuf = 0, *zbuf = 0;
	while (inflate) {
		uint32_t usize, zsize;
		if (!sin.read(reinterpret_cast<char *>(&usize), sizeof(usize)))
			throw std::runtime_error("Could not read uncompressed size");
		if (!sin.read(reinterpret_cast<char *>(&zsize), sizeof(zsize)))
			throw std::runtime_error("Could not read compressed size");
		if (usize == 0) {
			free(ubuf);
			free(zbuf);
			return;		// No padding applied
		}
		ubuf = realloc(ubuf, usize);
		if (ubuf == nullptr)
			throw std::runtime_error("Could not allocate inflate buffer");
		uint32_t asize = (zsize + 7) & ~7;	// Align to 8-byte boundary
		zbuf = realloc(zbuf, asize);
		if (asize && zbuf == nullptr)
			throw std::runtime_error("Could not allocate deflate buffer");
		if (!sin.read(reinterpret_cast<char *>(zbuf), zsize))
			throw std::runtime_error("Could not read zlib data");
		if (px)
			codec_xor(zbuf, asize, px, xsize);
		zlib_inflate(zbuf, zsize, ubuf, usize);
		if (ext)
			sout.write(reinterpret_cast<char *>(ubuf), usize);
	}

	unsigned long bsize = 4 * 1024 * 1024;	// Block size 4MiB
	uint8_t buf[bsize];
	unsigned long read = 0, rsize = size;
	bsize = rsize ? std::min(rsize, bsize) : bsize;
	while (sin.read(reinterpret_cast<char *>(buf), bsize), (read = sin.gcount()) != 0) {
		if (px) {
			unsigned long bsize = (read + 7) / 8 * 8;
			codec_xor(buf, bsize, px, xsize);
		}
		if (ext)
			sout.write(reinterpret_cast<char *>(buf), read);
		if (rsize) {
			rsize -= read;
			if (rsize == 0)
				break;
			bsize = std::min(rsize, bsize);
		}
	}
	if (rsize)
		throw std::runtime_error("Unexpected end of file");
	sin.clear();

	unsigned long padding = (align - (size % align)) % align;
	if (ext && padding) {
		bzero(buf, padding);
		sout.write(reinterpret_cast<char *>(buf), padding);
	}
}

void extract_890(const std::string &in, const std::string &out, bool ext)
{
	std::ifstream sin(in, std::ios::binary);
	if (!sin.is_open())
		throw std::runtime_error("Could not open input file " + in);

	// Write segment configuration
	std::ofstream sout(out);
	if (!sout.is_open())
		throw std::runtime_error("Could not open output file " + out);

	// Sections at constant offsets
	static const struct {
		std::string file, name;
		unsigned long offset, size;
		int pattern;
	} sections[] = {
		{"ploader",     "Primary loader",         0,  0x8000, -1},
		{"sloader",     "Secondary loader",  0x8000, 0x10000, -1},
		{"updtool",     "Update tool",      0x18000, 0x18000, -1},
	};
	sout << "Fixed offset encrypted sections" << std::endl;
	for (auto &ps: sections) {
		copy(sin, out, ps.file, ps.offset, ps.size, 1, ps.pattern, ext);
		sout << ps.file << "\t" << "offset\t0x" << std::hex << ps.offset;
		sout << "\tsize\t0x" << ps.size << "\t" << ps.name << std::endl;
	}

	// Setup information
	long offset = 0x30000;
	if (!sin.seekg(offset))
		throw std::runtime_error("Could not seek to offset " + std::to_string(offset));
	setup_t setup;
	// Try early version first
	if (!sin.read(reinterpret_cast<char *>(&setup.menu), sizeof(setup.menu.raw)))
		throw std::runtime_error("Could not read menu information");
	setup.valid = setup.model[0] == 'n';
	if (!setup.valid) {
		// Confirmed early version
		sout << std::endl << "Menu Information" << std::dec << std::endl;
		sout << "    Date:        " << setup.menu.date << std::endl;
		sout << "    Auto run:    " << setup.menu.autorun << std::endl;
		sout << "    Quiet:       " << setup.menu.quiet << std::endl;
		sout << "    Reserved[0]: " << setup.menu._reserved0 << std::endl;
		sout << "    Reserved[1]: " << setup.menu._reserved1 << std::endl;
		sout << "    Keep logs:   " << setup.menu.keeplogs << std::endl;
		sout << "    Dump NAND:   " << setup.menu.dumpnand << std::endl;
		sout << "    Reserved[2]: " << setup.menu._reserved2 << std::endl;
		sout << "    Reserved[3]: " << setup.menu._reserved3 << std::endl;
		sout << "    Reserved[4]: " << setup.menu._reserved4 << std::endl;
		sout << "    Reserved[5]: " << setup.menu._reserved5 << std::endl;
	} else {
		// No, this should be the newer version
		if (!sin.seekg(offset))
			throw std::runtime_error("Could not seek to offset " + std::to_string(offset));
		if (!sin.read(reinterpret_cast<char *>(&setup), sizeof(setup.raw)))
			throw std::runtime_error("Could not read setup information");
		sout << std::endl << "Setup Information" << std::dec << std::endl;
		sout << "    Version:   " << setup.version << std::endl;
		sout << "    Date:      " << setup.date << std::endl;
		sout << "    Model:     " << setup.model << std::endl;
		sout << "    Hostname:  " << setup.hostname << std::endl;
		sout << "    Auto run:  " << setup.autorun << std::endl;
		sout << "    Keep logs: " << setup.keeplogs << std::endl;
		sout << "    Dump NAND: " << setup.dumpnand << std::endl;
	}

	// Device information
	uint32_t ndev;
	if (!sin.read(reinterpret_cast<char *>(&ndev), sizeof(ndev)))
		throw std::runtime_error("Could not read number of devices");
	device_t devs[ndev];
	for (uint32_t i = 0; i < ndev; i++) {
		device_t &dev = devs[i];
		if (!sin.read(reinterpret_cast<char *>(&dev), sizeof(dev.raw)))
			throw std::runtime_error("Could not read device information " + std::to_string(i));
	}

	// System data section
	uint32_t nsys;
	if (!sin.read(reinterpret_cast<char *>(&nsys), sizeof(nsys)))
		throw std::runtime_error("Could not read number of system data sections");
	sout << std::endl << "System file sections" << std::endl;
	for (uint32_t i = 0; i < nsys; i++) {
		system_t sys;
		if (!sin.read(reinterpret_cast<char *>(&sys), sizeof(sys.raw)))
			throw std::runtime_error("Could not read system data section " + std::to_string(i));
		std::string filename = "sys" + std::to_string(i) + (sys.compressed ? ".gz" : ".bin");
		sout << "    sys" << i << std::endl;
		sout << "        Index:             " << sys.index << std::endl;
		sout << "        Data size:         " << sys.size << std::endl;
		sout << "        Uncompressed size: " << sys.rawsize << std::endl;
		sout << "        Compressed:        " << sys.compressed << std::endl;
		sout << "        Dumped file:       " << filename << std::endl;
		copy(sin, out, filename, -1, sys.size, 1, 0, ext);
	}

	// File offset table
	uint32_t fpos[10];
	if (!sin.read(reinterpret_cast<char *>(&fpos[0]), sizeof(fpos)))
		throw std::runtime_error("Could not read file offset table");

	// Dump device data section
	sout << std::endl << "Device Information" << std::endl;
	for (uint32_t i = 0; i < ndev; i++) {
		device_t &dev = devs[i];
		std::string filename = destination(dev.dest);
		offset = fpos[i];
		sout << "    dev" << i << std::endl;
		sout << "        Type:              " << dev.type << std::endl;
		sout << "        Destination:       " << filename << std::endl;
		sout << "        Data size:         " << dev.size << std::endl;
		sout << "        Uncompressed size: " << dev.rawsize << std::endl;
		sout << "        Compressed:        " << dev.compressed << std::endl;
		sout << "        XOR pattern:       " << dev.pattern << std::endl;
		sout << "        Checksum:          " << dev.cksum << std::endl;
		sout << "        Offset:            " << offset << std::endl;
		filename = std::string(basename(filename.c_str()));
		filename += !setup.valid && dev.compressed ? ".gz" :
				filename.find('.') == std::string::npos ? ".bin" : "";
		sout << "        Dumped file:       " << filename << std::endl;
		copy(sin, out, filename, offset, dev.size, 1, dev.pattern, ext, setup.valid && dev.compressed);
	}
}
