#!/usr/bin/env python3
import os
import yaml
import argparse
from pprint import pprint

def bin_to_str(ba):
    return [ba.split(b'\0', 1)[0].decode("GBK"), ba]

def main():
    parser = argparse.ArgumentParser(prog='dump_mop_image',
                                     description='Convert a (NP7000) mop upgrade package to a full disk image')
    parser.add_argument('-s', '--size', type=int, default=0, help="Image size")
    parser.add_argument('idx_file', help="upd.idx")
    parser.add_argument('out_dir', help="output/")
    args = parser.parse_args()

    f_name, f_ext = os.path.splitext(args.idx_file)

    os.makedirs(args.out_dir, exist_ok=True)
    data = {}
    with open(args.idx_file, "rb") as f_idx:
        buf = f_idx.read(0x200)
        header = {}
        data["header"] = header
        header["format"] = bin_to_str(buf[0:128])[0]
        header["soc"]    = bin_to_str(buf[128:160])[0]
        header["board"]  = bin_to_str(buf[160:192])[0]
        header["num_sections"] = int.from_bytes(buf[0xcc:0xd0], 'little')
        header["block_len"] = int.from_bytes(buf[0xd0:0xd4], 'little')
        header["section_block_len"] = int.from_bytes(buf[0xd4:0xd8], 'little')
        header["num_files"] = int.from_bytes(buf[0xfc:0x0100], 'little')

        file_lens = []
        for i in range(header["num_files"]):
            file_lens.append(int.from_bytes(buf[0x0100+i*4:0x0104+i*4], 'little'))

        data["sections"] = []
        for i in range(header["num_sections"]):
            buf = f_idx.read(0x200)
            section = {}
            data["sections"].append(section)
            section["name"] = bin_to_str(buf[0:0x100])[0]
            section["start"] = int.from_bytes(buf[0x104:0x108], 'little')
            section["end"] = int.from_bytes(buf[0x108:0x10c], 'little')
            section["size"] = int.from_bytes(buf[0x110:0x114], 'little')
            section["idx"] = int.from_bytes(buf[0x118:0x11c], 'little')

        with open(os.path.join(args.out_dir, "image.bin"), "wb") as f_img:
            if (args.size):
                f_img.truncate(args.size)

            data["files"] = []
            for i_file in range(header["num_files"]):
                file = {}
                data["files"].append(file)
                file["size"] = file_lens[i_file]
                file["name"] = f"{f_name}.mo{"p" if i_file == 0 else i_file}"
                with open(file["name"], "rb") as f_data:
                    for i_block in range(file_lens[i_file] // header["block_len"]):
                        buf = f_idx.read(0x10)
                        block = {
                            "section": int.from_bytes(buf[0x00:0x04], 'little'),
                            "section_checksum": int.from_bytes(buf[0x04:0x08], 'little'),
                            "block_offset": int.from_bytes(buf[0x08:0x0c], 'little'),
                            "block_offset_checksum": int.from_bytes(buf[0x0c:0x10], 'little'),
                        }
                        f_img.seek(data["sections"][block["section"]]["start"] * header["section_block_len"] +
                                   block["block_offset"] * header["block_len"])
                        f_img.write(f_data.read(header["block_len"]))

    with open(os.path.join(args.out_dir, "index.yaml"), "w", encoding="UTF-8") as f_out:
        yaml.dump(data, f_out, allow_unicode=True)

if __name__ == '__main__':
    main()
