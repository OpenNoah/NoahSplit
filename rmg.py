#!/usr/bin/env python3
import os
import re
# import json
import yaml
import argparse
from pprint import pprint

def bin_to_str(ba):
    return [ba.split(b'\0', 1)[0].decode("GBK"), ba]

def save(save_path, metadata, data):
    if metadata["type"] == "dir":
        if metadata["ext"]:
            raise RuntimeError(f"Extension is not empty for a directory path: {metadata}")
        if metadata["size"] != 0:
            raise RuntimeError(f"Size is not 0 for a directory path: {metadata}")
        # TODO: How to decode the directory path?
        d_path = os.path.join(save_path, metadata['dir'], metadata['name'])
        os.makedirs(d_path, exist_ok=True)
    else:
        # TODO: How to decode the directory path?
        ext = '.' + metadata['ext'] if metadata['ext'] else ''
        f_path = os.path.join(save_path, metadata['dir'], f"{metadata['name']}{ext}")
        print(f_path)
        os.makedirs(os.path.dirname(f_path), exist_ok=True)
        with open(f_path, "wb") as f_out:
            f_out.write(data[metadata["offset"]:metadata["offset"]+metadata["size"]])

def main():
    parser = argparse.ArgumentParser(prog='upg.py',
                                     description='Noah RMG format upgrade package (e.g. NP980) extraction tool')
    parser.add_argument('pkg_file', help="NP980.rmg")
    parser.add_argument('save_path', help="output/")
    args = parser.parse_args()

    os.makedirs(args.save_path, exist_ok=True)

    pkg_data = {"header": {}, "files": []}
    with open(args.pkg_file, "rb") as f_in:
        header = f_in.read(32)
        pkg_data["header"] = header
        while entry := f_in.read(32):
            if entry[0] == 0:
                break
            f_meta = {}
            f_meta["name"] = bin_to_str(entry[0:8])[0].strip()
            f_meta["ext"] = bin_to_str(entry[8:11])[0].strip()
            f_meta["type"] = {0x11: "dir", 0x21: "file"}[entry[11]]
            f_meta["offset"] = int.from_bytes(entry[20:24], 'little')
            f_meta["size"] = int.from_bytes(entry[24:28], 'little')
            f_meta["dir"] = entry[28:32].hex()
            pkg_data["files"].append(f_meta)

        f_in.seek(0, os.SEEK_SET)
        f_data = f_in.read()
        for f_meta in pkg_data["files"]:
            save(args.save_path, f_meta, f_data)

    # pprint(pkg_data)
    with open(os.path.join(args.save_path, "index.yaml"), "w", encoding="UTF-8") as f_out:
        yaml.dump(pkg_data, f_out, allow_unicode=True)

if __name__ == '__main__':
    main()
