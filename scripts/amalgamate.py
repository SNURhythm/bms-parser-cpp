# C++ amalgamator
# Copyright (C) 2024 VioletXF
# This script is used to generate a single C++ file from a set of C++ files.
# This implementation doesn't follow the standard C++ include resolution, it
# simply concatenates the files in the order they are given, to preserve the
# copyright notice.

import os
import re

def amalgamate(paths: [str], out: str):
    visited = set()
    content = "#pragma once\n"
    files = []

    def dfs(path):
        nonlocal content
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
            files.append(path)

    for path in paths:
        dfs(path)
    for path in files:
        with open(path, "r") as f:
            for line in f:
                if line.strip().startswith("#pragma once"):
                    continue
                m = re.match(r'#include\s+<(.*)>', line)
                if not line.strip().startswith("#include") or m:
                    content += line

    with open(out, "w") as f:
        f.write(content)


if __name__ == "__main__":
    # glob to get all files
    import sys
    if len(sys.argv) < 3:
        print("Usage: python amalgamate.py <out> <in>...")
        sys.exit(1)
    out = sys.argv[1]
    ins = sys.argv[2:]
    print("Amalgamating", ins, "into", out)
    amalgamate(ins, out)

