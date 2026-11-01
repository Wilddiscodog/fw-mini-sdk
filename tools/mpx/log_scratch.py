#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Read cross-reset step markers from uart19.log."""
import re

d = open("D:/fw-mini-sdk/uart19.log", "rb").read().decode("utf-8", "replace")
marks = re.findall(r"prev step=(\d+)", d)
print("prev step sequence (last 10):", marks[-10:])
j = d.rfind("[mpx] task start")
print(d[j:j + 900].replace("\r", ""))
