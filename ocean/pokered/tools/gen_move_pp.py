#!/usr/bin/env python3
"""Generate the PKRED_MOVE_PP_H block of ocean/pokered/data/ram_map.h (base PP of every move) from
the disassembly; replace that block with the output.

  python3 ocean/pokered/tools/gen_move_pp.py [vendor/pokered]
"""
import re, sys

root = sys.argv[1] if len(sys.argv) > 1 else "vendor/pokered"
pp = [0]
for line in open(f"{root}/data/moves/moves.asm"):
    m = re.match(r"\s*move\s+(\w+),.*,\s*(\d+)\s*$", line)
    if m:
        pp.append(int(m.group(2)))
assert len(pp) == 166, f"expected 165 moves, got {len(pp) - 1}"
print("#ifndef PKRED_MOVE_PP_H\n#define PKRED_MOVE_PP_H\n\n#include <stdint.h>\n")
print("#define PKRED_NUM_MOVES 165\n")
print("static const uint8_t PKRED_MOVE_BASE_PP[PKRED_NUM_MOVES + 1] = {")
for i in range(0, len(pp), 16):
    print("    " + ", ".join(str(v) for v in pp[i:i + 16]) + ",")
print("};\n\n#endif")
