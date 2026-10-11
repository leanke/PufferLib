#!/usr/bin/env python3
"""Generate ocean/pokered/data/species_items.h (TM/HM compatibility and stone evolutions per internal
species id) from the disassembly.

  python3 ocean/pokered/tools/gen_species_items.py [vendor/pokered]
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "..", "..", "..", "vendor", "pokered")
OUT = os.path.join(ROOT, "..", "data", "species_items.h")
STONES = ["MOON_STONE", "FIRE_STONE", "THUNDER_STONE", "WATER_STONE", "LEAF_STONE"]


def read(rel):
    with open(os.path.join(SRC, rel)) as f:
        return f.read()


def strip(text):
    return "\n".join(line.split(";")[0] for line in text.splitlines())


internal = {}
for name, val in re.findall(r"^\s*const (\w+)\s*;\s*\$([0-9A-Fa-f]+)", read("constants/pokemon_constants.asm"), re.M):
    internal[name] = int(val, 16)

items = read("constants/item_constants.asm")
tmhm_bit = {}
for i, name in enumerate(re.findall(r"^\s*add_tm (\w+)", items, re.M)):
    tmhm_bit[name] = i
for i, name in enumerate(re.findall(r"^\s*add_hm (\w+)", items, re.M)):
    tmhm_bit[name] = 50 + i
assert len(tmhm_bit) == 55, len(tmhm_bit)

tmhm = [0] * 256
files = [os.path.join("data/pokemon/base_stats", f) for f in os.listdir(os.path.join(SRC, "data/pokemon/base_stats"))]
for rel in files + ["data/pokemon/mew.asm"]:
    text = strip(read(rel)).replace("\\\n", " ")
    dex = re.search(r"db DEX_(\w+)", text)
    moves = re.search(r"tmhm ([^\n]*)", text)
    if not dex:
        continue
    mask = 0
    for move in (moves.group(1).split(",") if moves else []):
        if move.strip() and move.strip() != "UNUSED":
            mask |= 1 << tmhm_bit[move.strip()]
    tmhm[internal[dex.group(1)]] = mask

stones = [0] * 256
evos = strip(read("data/pokemon/evos_moves.asm"))
labels = re.findall(r"^\s*dw (\w+EvosMoves)", evos, re.M)
for idx, label in enumerate(labels, start=1):
    body = evos.split(label + ":", 1)[1].split("db 0", 1)[0]
    for stone in re.findall(r"EVOLVE_ITEM, (\w+)", body):
        stones[idx] |= 1 << STONES.index(stone)

out = ["#ifndef POKERED_SPECIES_ITEMS_H", "#define POKERED_SPECIES_ITEMS_H", "", "#include <stdint.h>", "",
       "static const uint64_t PKRED_SPECIES_TMHM[256] = {"]
for i in range(0, 256, 4):
    out.append("    " + " ".join("0x%014xull," % v for v in tmhm[i:i + 4]))
out += ["};", "", "static const uint8_t PKRED_SPECIES_STONES[256] = {"]
for i in range(0, 256, 16):
    out.append("    " + " ".join("%d," % v for v in stones[i:i + 16]))
out += ["};", "", "#endif", ""]
with open(OUT, "w") as f:
    f.write("\n".join(out))
