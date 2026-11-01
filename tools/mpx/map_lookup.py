#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Find which symbols own given addresses in sdk.map (call-chain lookup)."""
import re
import sys

MAP = "D:/fw-mini-sdk/cpu/wl82/tools/sdk.map"
addrs = [int(a, 16) for a in sys.argv[1:]]
rows = []
for line in open(MAP, encoding="utf-8", errors="replace"):
    m = re.match(r"\s*(0x[0-9a-fA-F]+)\s+0x[0-9a-fA-F]+\s+(.*)", line)
    if m:
        rows.append((int(m.group(1), 16), m.group(2).strip()))
rows.sort()
for a in addrs:
    best = None
    for start, name in rows:
        if start <= a:
            best = (start, name)
        else:
            break
    if best:
        print(f"0x{a:08x} -> {best[1][:120]} (+0x{a - best[0]:x})")
    else:
        print(f"0x{a:08x} -> ?")
