#include <algorithm>
#include <iostream>
#include <iomanip>
#include <string>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cstdint>
#include <cstring>

void extract_890(const std::string &in, const std::string &out, bool ext);

void create_1000(const std::string &in, const std::string &out);
void extract_1000(const std::string &in, const std::string &out, bool ext);

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

void copy(std::ofstream &out, std::ifstream &in, unsigned long size, unsigned long align)
{
	static const unsigned long block = 4 * 1024 * 1024;	// Block size 4MiB
	unsigned long padding = (align - (size % align)) % align;
	uint8_t buf[block];
	while (size) {
		unsigned long s = std::min(block, size);
		in.read(reinterpret_cast<char *>(buf), s);
		out.write(reinterpret_cast<char *>(buf), s);
		size -= s;
	}
	if (padding) {
		bzero(buf, padding);
		out.write(reinterpret_cast<char *>(buf), padding);
	}
}

int main(int argc, char *argv[])
{
	std::string in, out, dir(".");
	enum {OpCreate, OpExtract, OpInfo} op = OpCreate;
	enum {Type1000, Type890} type = Type1000;
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
		} else if (arg.compare("--type=np890") == 0) {
			type = Type890;
		} else if (arg.compare("--type=np1000") == 0) {
			type = Type1000;
		} else if (arg.compare("--create") == 0) {
			op = OpCreate;
		} else if (arg.compare("--extract") == 0) {
			op = OpExtract;
		} else if (arg.compare("--info") == 0) {
			op = OpInfo;
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
		std::cout << "    " << argv[0] << " [--type=np1000] [--create] input.pkg output.bin" << std::endl;
		std::cout << "    " << argv[0] << " [--type=np1000] [--info|--extract] input.bin output.pkg" << std::endl;
		std::cout << "Available types: np890, np1000" << std::endl;
		return 1;
	}

	try {
		if (type == Type1000) {
			if (op == OpCreate)
				create_1000(in, out);
			else
				extract_1000(in, out, op == OpExtract);
		} else if (type == Type890) {
			if (op == OpCreate)
				throw std::runtime_error("Unsupported operation");
			else
				extract_890(in, out, op == OpExtract);
		} else {
			throw std::runtime_error("Unknown type " + type);
		}
	} catch (std::exception &e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return 1;
	}
	return 0;
}
