#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Show the tail of the last OTA session (burn/result)."""
d = open("D:/fw-mini-sdk/uart19.log", "rb").read().decode("utf-8", "replace")
i = d.rfind("[ota-http] upload start")
seg = d[i:i + 12000]
# 跳过进度行, 找关键事件
import re
keep = []
for line in seg.split("\n"):
    l = line.replace("\r", "")
    if ("recv" in l and "(9" not in l and "(8" not in l and "(7" not in l) or \
       "burn" in l.lower() or "crc" in l.lower() or "OK" in l or "ERR" in l or \
       "cache" in l or "B1" in l or "switch" in l.lower() or "reboot" in l.lower() or \
       "reset" in l.lower() or "verify" in l.lower():
        keep.append(l[:150])
print("\n".join(keep[-25:]))
