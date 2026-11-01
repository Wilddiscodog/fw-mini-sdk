#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Extract boot cycles and crash context from uart19.log."""
import re
import sys

data = open("D:/fw-mini-sdk/uart19.log", "rb").read().decode("utf-8", "replace")
idx = [m.start() for m in re.finditer(r"00:00:00\.141", data)]
print("boot markers:", len(idx))
if len(idx) >= 2:
    seg = data[idx[0]:idx[1]]
    print("=== boot cycle 0 tail (before next boot) ===")
    print(seg[-2500:])
# 全局搜崩溃特征
for pat in ["exception", "Exception", "reboot", "Reboot", "WDT", "wdt", "fault",
            "Fault", "crash", "assert", "Assert", "reset", "Reset", "BOOT_RE",
            "CPU0", "cpu0", "illegally", "misalign"]:
    hits = [m.start() for m in re.finditer(pat, data)]
    if hits:
        print(f"--- pattern {pat!r}: {len(hits)} hits; first context ---")
        s = max(0, hits[0] - 200)
        print(data[s:hits[0] + 300].replace("\r", ""))
        break
