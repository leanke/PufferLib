#!/usr/bin/env python3
"""Generate ocean/pokered/data/species_names.h (display name per internal species id) from the disassembly.

  python3 ocean/pokered/tools/gen_species_names.py [vendor/pokered]
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "..", "..", "..", "vendor", "pokered")
OUT = os.path.join(ROOT, "..", "data", "species_names.h")

with open(os.path.join(SRC, "data/pokemon/names.asm")) as f:
    names = re.findall(r'^\s*dname "([^"]*)"', f.read(), re.M)
assert len(names) == 190, len(names)

table = ["?"] * 256
for i, name in enumerate(names, start=1):
    table[i] = name.replace("♂", " M").replace("♀", " F")

out = ["#ifndef POKERED_SPECIES_NAMES_H", "#define POKERED_SPECIES_NAMES_H", "",
       "static const char *const PKRED_SPECIES_NAMES[256] = {"]
for i in range(0, 256, 8):
    out.append("    " + " ".join('"%s",' % n for n in table[i:i + 8]))
out += ["};", "", "#endif", ""]
with open(OUT, "w") as f:
    f.write("\n".join(out))
