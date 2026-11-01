#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Tail uart19.log: last 40 lines + reboot count over time."""
import re
import time

path = "D:/fw-mini-sdk/uart19.log"
data = open(path, "rb").read().decode("utf-8", "replace")
lines = [l for l in data.split("\n") if l.strip()]
print("total lines:", len(lines))
print("=== last 25 lines ===")
for l in lines[-25:]:
    print(l.replace("\r", "")[:160])
boots = re.findall(r"system reset reason: (\w+)", data)
print("=== reset reasons ===", boots)
mpx = re.findall(r"\[mpx\][^\r\n]*", data)
print("=== mpx lines (all %d) ===" % len(mpx))
for m in mpx:
    print(m[:160])
