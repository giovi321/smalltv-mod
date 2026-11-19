#!/usr/bin/env python3
"""Copy whole member functions of MDNSResponder out of LEAmDNS_Control.cpp.

The library copy cannot be built on the host (it needs lwIP and the Arduino
core), so tests/mdns/test_known_answers.cpp compiles the REAL text of a few
functions against a small fake of the class. This script cuts that text out of
the source, so the test always runs what is in the file.

  extract_functions.py <source.cpp> <out.inc> <function name>...

A function starts at the line that holds "MDNSResponder::<name>(" (plus the
line above it when the return type is on its own line) and ends at the first
line that is exactly four spaces and a closing brace. Exit 1 when a name is
missing or when its end is not found.
"""
import re
import sys


def cut(lines, name):
    pat = re.compile(r"^\s+(?:\S.*\s)?MDNSResponder::%s\(" % re.escape(name))
    for i, line in enumerate(lines):
        if not pat.match(line):
            continue
        start = i
        # Return type on the line above ("inline bool __attribute__(...)").
        if line.strip().startswith("MDNSResponder::"):
            start = i - 1
        for j in range(i, len(lines)):
            if lines[j].rstrip("\r\n") == "    }":
                return lines[start:j + 1]
        return None
    return None


def main(argv):
    if len(argv) < 4:
        print(__doc__)
        return 2
    with open(argv[1]) as f:
        lines = f.read().replace("\r\n", "\n").splitlines(True)
    out = []
    bad = 0
    for name in argv[3:]:
        body = cut(lines, name)
        if body is None:
            print("extract_functions: cannot find %s in %s" % (name, argv[1]))
            bad += 1
            continue
        out.append("// ---- %s ----\n" % name)
        out.extend(body)
        out.append("\n")
    if bad:
        return 1
    with open(argv[2], "w") as f:
        f.writelines(out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
