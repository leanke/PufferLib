#!/usr/bin/env python3
"""Writes vendor/redcore/src/host/rc_item_names.c (item id -> name) from the disassembly's
data/items/names.asm, for drawing the battle bag. TMs/HMs are numbered by id."""
import re, sys
src = sys.argv[1] if len(sys.argv) > 1 else "vendor/pokered/data/items/names.asm"
out = sys.argv[2] if len(sys.argv) > 2 else "vendor/redcore/src/host/rc_item_names.c"
names = [re.sub(r"[^\x20-\x7e]", "e", m) for m in re.findall(r'li "([^"]*)"', open(src, encoding="utf-8").read())]
lines = ['#include "rc_item_names.h"', "", "#include <stdio.h>", "",
         "const char *const RC_ITEM_NAMES[] = {", '    "",']
lines += [f'    "{n}",' for n in names]
lines += ["};", "",
          "void rc_item_name(unsigned id, char *out, unsigned cap) {",
          "    if (id > 0 && id < RC_ITEM_NAME_COUNT) snprintf(out, cap, \"%s\", RC_ITEM_NAMES[id]);",
          "    else if (id >= 0xC4 && id <= 0xC8) snprintf(out, cap, \"HM%02u\", id - 0xC3);",
          "    else if (id >= 0xC9 && id <= 0xFA) snprintf(out, cap, \"TM%02u\", id - 0xC8);",
          "    else snprintf(out, cap, \"ITEM %u\", id);", "}", ""]
hdr = "vendor/redcore/src/host/rc_item_names.h"
if f"#define RC_ITEM_NAME_COUNT {len(names) + 1}\n" not in open(hdr).read():
    print(f"WARNING: update RC_ITEM_NAME_COUNT in {hdr} to {len(names) + 1}")
open(out, "w").write("\n".join(lines))
print(f"wrote {out}: {len(names)} names")
