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
    drive = re.sub(r'[:/]', '', metadata["drive"])
    if metadata["path"].endswith("/"):
        if metadata["size"] != 0:
            raise RuntimeError(f"Size is not 0 for a directory path: {metadata}")
        os.makedirs(os.path.join(save_path, drive, metadata["path"]), exist_ok=True)
    else:
        f_path = os.path.join(save_path, drive, metadata["path"])
        print(f_path)
        os.makedirs(os.path.dirname(f_path), exist_ok=True)
        with open(f_path, "wb") as f_out:
            f_out.write(data)

def main():
    parser = argparse.ArgumentParser(prog='upg.py',
                                     description='Noah UPG format upgrade package (e.g. NP950) extraction tool')
    parser.add_argument('upg_file', help="NP950_prg.upg")
    parser.add_argument('save_path', help="output/")
    args = parser.parse_args()

    os.makedirs(args.save_path, exist_ok=True)

    upg_data = {"header": {}, "files": []}
    with open(args.upg_file, "rb") as f_in:
        upg_data["header"]["magic"] = f_in.read(8).hex()
        upg_data["header"]["type"] = bin_to_str(f_in.read(32))[0]
        upg_data["header"]["flags"] = f_in.read(4).hex()
        while True:
            f_meta = {}
            drive = f_in.read(4)
            if not drive:
                break
            f_meta["drive"] = bin_to_str(drive)[0]
            f_meta["path"] = bin_to_str(f_in.read(256))[0]
            f_meta["size"] = int.from_bytes(f_in.read(4), 'little')
            f_data = f_in.read(f_meta["size"])
            # print(f"{f_in.tell():#010x}")
            upg_data["files"].append(f_meta)
            save(args.save_path, f_meta, f_data)

    # pprint(upg_data)
    # with open(os.path.join(args.save_path, "index.json"), "w", encoding="UTF-8") as f_out:
    #     f_out.write(json.dumps(upg_data, indent=4))
    with open(os.path.join(args.save_path, "index.yaml"), "w", encoding="UTF-8") as f_out:
        yaml.dump(upg_data, f_out, allow_unicode=True)

if __name__ == '__main__':
    main()
