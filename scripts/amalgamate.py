# C++ amalgamator
# Copyright (C) 2024 VioletXF
# This script is used to generate a single C++ file from a set of C++ files.
# This implementation doesn't follow the standard C++ include resolution, it
# simply concatenates the files in the order they are given, to preserve the
# copyright notice.

import os
import re


def amalgamate(paths: [str], out_header: str, out_source: str):
    visited = set()
    header_content = "#pragma once\n"
    source_content = f'#include "{os.path.basename(out_header)}"\n'
    header_files = []

    def dfs(path, is_source=False):
        if path in visited:
            return
        visited.add(path)
        with open(path, "r") as f:
            for line in f:
                if line.strip().startswith("#include"):
                    m = re.match(r'#include\s+"(.*)"', line)
                    if m:
                        include = m.group(1)
                        include_path = os.path.join(os.path.dirname(path), include)
                        dfs(include_path)
            if not is_source:
                header_files.append(path)

    for path in paths:
        dfs(path, is_source=True)
        with open(path, "r") as f:
            for line in f:
                if line.strip().startswith("#pragma once"):
                    continue
                m = re.match(r"#include\s+<(.*)>", line)
                if not line.strip().startswith("#include") or m:
                    source_content += line
            source_content += "\n"
    for path in header_files:
        with open(path, "r") as f:
            for line in f:
                if line.strip().startswith("#pragma once"):
                    continue
                m = re.match(r"#include\s+<(.*)>", line)
                if not line.strip().startswith("#include") or m:
                    header_content += line
            header_content += "\n"

    with open(out_header, "w") as f:
        f.write(header_content)
    with open(out_source, "w") as f:
        f.write(source_content)


if __name__ == "__main__":
    # glob to get all files
    import sys

    if len(sys.argv) < 4:
        print("Usage: python amalgamate.py <out_header> <out_source> <in>...")
        sys.exit(1)
    out_header = sys.argv[1]
    out_source = sys.argv[2]
    ins = sys.argv[3:]
    print("Amalgamating", ins, "into", out_header, out_source)
    amalgamate(ins, out_header, out_source)
