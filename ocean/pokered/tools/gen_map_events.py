#!/usr/bin/env python3
"""Generate ocean/pokered/data/map_events.h (the map id each wEventFlags bit is earned on, 0xFF when it
has no single map) from the disassembly.

  python3 ocean/pokered/tools/gen_map_events.py [vendor/pokered]
"""
import os
import re
import sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.abspath(__file__))
SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "..", "..", "..", "vendor", "pokered")
OUT = os.path.join(ROOT, "..", "data", "map_events.h")
FLAG_BITS = 320 * 8
SCAN_DIRS = ["scripts", "text", "engine", "home", "data"]
SKIP_DIRS = ["engine/debug"]
# set from engine code, not a map script
FILE_MAPS = {
    "engine/events/hidden_events/vermilion_gym_trash.asm": "VERMILION_GYM",
    "engine/events/hidden_events/bills_house_pc.asm": "BILLS_HOUSE",
    "engine/events/cinnabar_lab.asm": "CINNABAR_LAB_FOSSIL_ROOM",
}
EVENT_MAPS = {
    "EVENT_FIGHT_ROUTE12_SNORLAX": "ROUTE_12",
    "EVENT_FIGHT_ROUTE16_SNORLAX": "ROUTE_16",
}
# reached only as bit offsets from GATE0 (EventFlagBit)
EVENT_MAPS.update({"EVENT_CINNABAR_GYM_GATE%d_UNLOCKED" % i: "CINNABAR_GYM" for i in range(7)})
SETTERS = r"(?:trainer|SetEvent|SetEventReuseHL|SetEventAfterBranchReuseHL|SetEventForceReuseHL|CheckAndSetEvent)"


def read(rel):
    with open(os.path.join(SRC, rel)) as f:
        return f.read()


def strip(text):
    return "\n".join(line.split(";")[0] for line in text.splitlines())


events, n = {}, 0
for line in strip(read("constants/event_constants.asm")).splitlines():
    parts = line.split(None, 1)
    if not parts:
        continue
    op, arg = parts[0], parts[1].strip() if len(parts) > 1 else ""
    if op == "const_def":
        n = int(arg or 0)
    elif op == "const":
        events[arg] = n
        n += 1
    elif op == "const_skip":
        n += int(arg) if arg else 1
    elif op == "const_next":
        n = eval(arg.replace("$", "0x"))

map_ids = {name: int(h, 16) for name, h in
           re.findall(r"^\s*map_const (\w+),.*;\s*\$([0-9A-Fa-f]+)", read("constants/map_constants.asm"), re.M)}
file_map = {}
for f in os.listdir(os.path.join(SRC, "data/maps/headers")):
    m = re.search(r"map_header (\w+), (\w+)", read("data/maps/headers/" + f))
    if m:
        file_map[m.group(1)] = map_ids[m.group(2)]

setters, refs = defaultdict(set), defaultdict(set)
for d in SCAN_DIRS:
    for root, _, files in os.walk(os.path.join(SRC, d)):
        for f in sorted(files):
            rel = os.path.relpath(os.path.join(root, f), SRC)
            if not f.endswith(".asm") or any(rel.startswith(s) for s in SKIP_DIRS):
                continue
            text = strip(read(rel))
            where = file_map.get(f[:-4], map_ids.get(FILE_MAPS.get(rel)))
            hits = re.findall(SETTERS + r"\s+(EVENT_\w+)", text)
            for a, b in re.findall(r"SetEventRange\s+(EVENT_\w+),\s*(EVENT_\w+)", text):
                hits += [e for e, v in events.items() if events[a] <= v <= events[b]]
            for names in re.findall(r"SetEvents\s+([^\n]+)", text):
                hits += [x.strip() for x in names.split(",")]
            for e in hits:
                setters[e].add(where)
            if where is not None:
                for e in re.findall(r"\b(EVENT_\w+)", text):
                    refs[e].add(where)

flag_map = [0xFF] * FLAG_BITS
for e, bit in events.items():
    if e in EVENT_MAPS:
        flag_map[bit] = map_ids[EVENT_MAPS[e]]
        continue
    found = setters.get(e) or refs.get(e, set())
    if len(found) == 1 and None not in found:
        flag_map[bit] = next(iter(found))

out = ["#ifndef POKERED_MAP_EVENTS_H", "#define POKERED_MAP_EVENTS_H", "", "#include <stdint.h>", "",
       "static const uint8_t PKRED_EVENT_FLAG_MAP[%d] = {" % FLAG_BITS]
for i in range(0, FLAG_BITS, 16):
    out.append("    " + " ".join("0x%02X," % v for v in flag_map[i:i + 16]))
out += ["};", "", "#endif", ""]
with open(OUT, "w") as f:
    f.write("\n".join(out))
