import re

def read_ini(fpath):
    data = {}
    with open(fpath, "r", encoding="utf8") as f:
        tag_data = None
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue

            if line.startswith("["):
                tag = re.match(r"\[(.+)\]", line)[1]
                if tag not in data:
                    data[tag] = []
                tag_data = {}
                data[tag].append(tag_data)
                continue

            k,v = line.split("=", 1)
            tag_data[k] = v

    return data
