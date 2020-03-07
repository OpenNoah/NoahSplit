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
	};
};

union device_t {
	uint32_t raw[7];
	struct {
		uint32_t type, dest, size, devsize, compressed, pattern, cksum;
	};
};

union system_t {
	uint32_t raw[5];
	struct {
		uint32_t callback, size;
		uint32_t _reserved0[1];
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

static void copy(std::ifstream &sin, const std::string &out, const std::string &file,
		long offset, unsigned long size, unsigned long align, int codec, bool ext, bool append = false)
{
	if (offset >= 0 && !sin.seekg(offset))
		throw std::runtime_error("Could not seek to offset " + std::to_string(offset));

	boost::filesystem::path p(out);
	std::string filename = (p.parent_path() / file).native();
	std::ofstream sout(filename, append ? std::ios::binary | std::ios::app : std::ios::binary);
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
		//{"version.bin", "Version section",  0x30000,    0x80, false},
		//{"unknown.bin", "Unknown section",  0x30080,       0, false},
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
	sout << std::endl << "System data sections" << std::endl;
	for (uint32_t i = 0; i < nsys; i++) {
		system_t sys;
		if (!sin.read(reinterpret_cast<char *>(&sys), sizeof(sys.raw)))
			throw std::runtime_error("Could not read system data section " + std::to_string(i));
		std::string filename = "sys" + std::to_string(i) + (sys.compressed ? ".gz" : ".bin");
		sout << "    sys" << i << std::endl;
		sout << "        Callback:    " << sys.callback << std::endl;
		sout << "        Size:        " << sys.size << std::endl;
		sout << "        _Reserved:   " << sys._reserved0[0] << std::endl;
		sout << "        Compressed:  " << sys.compressed << std::endl;
		sout << "        Dumped file: " << filename << std::endl;
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
		sout << "        Destination:       " << dev.dest << "\t" << filename << std::endl;
		sout << "        Data size:         " << dev.size << std::endl;
		sout << "        Device size:       " << dev.devsize << std::endl;
		sout << "        Compressed:        " << dev.compressed << std::endl;
		sout << "        XOR pattern:       " << dev.pattern << std::endl;
		sout << "        Checksum:          " << dev.cksum << std::endl;
		sout << "        Offset:            " << offset << std::endl;

		filename = std::string(basename(filename.c_str()));
		filename += dev.compressed ? ".zlib" : filename.find('.') == std::string::npos ? ".bin" : "";

		if (!sin.seekg(offset))
			throw std::runtime_error("Could not seek to offset " + std::to_string(offset));
		if (dev.compressed) {
			uint32_t tusize, tzsize;
			bool append = false;
			for (;;) {
				uint32_t usize, zsize;
				if (!sin.read(reinterpret_cast<char *>(&usize), sizeof(usize)))
					throw std::runtime_error("Could not read uncompressed size");
				if (!sin.read(reinterpret_cast<char *>(&zsize), sizeof(zsize)))
					throw std::runtime_error("Could not read compressed size");
				if (usize == 0)
					break;
				copy(sin, out, filename, -1, zsize, 1, dev.pattern, ext, append);
				tusize += usize;
				tzsize += zsize;
				append = true;
			}
			sout << "        Uncompressed size: " << tusize << std::endl;
			sout << "        Compressed size:   " << tzsize << std::endl;
		} else {
			copy(sin, out, filename, offset, dev.size, 1, dev.pattern, ext);
		}
		sout << "        Dumped file:       " << filename << std::endl;
	}
}
