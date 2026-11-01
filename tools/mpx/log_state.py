#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""State check: boot count, mpx timing, crash pattern from uart19.log."""
import re

data = open("D:/fw-mini-sdk/uart19.log", "rb").read().decode("utf-8", "replace")
boots = len(re.findall(r"system reset reason:", data))
mpx_starts = re.findall(r"\[00:00:(\d+\.\d+)\]\[mpx\] task start", data)
print("boot count:", boots)
print("mpx task start times (first 10):", mpx_starts[:10])
print("mpx task start times (last 10):", mpx_starts[-10:])
print("SMOKE PASS count:", len(re.findall(r"SMOKE PASS", data)))
print("step1 count:", len(re.findall(r"step1 registering", data)))
print("OTA deferred count:", len(re.findall(r"OTA in progress", data)))
print("ota-http lines:", len(re.findall(r"ota-http", data)))
tail = [l for l in data.split("\n") if l.strip()][-12:]
print("=== tail ===")
for l in tail:
    print(l.replace("\r", "")[:150])
