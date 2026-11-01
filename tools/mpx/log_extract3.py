#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Dump full exception blocks: all cycles, from the tail of the log."""
import re

data = open("D:/fw-mini-sdk/uart19.log", "rb").read().decode("utf-8", "replace")
marks = [m.start() for m in re.finditer(r"exception error", data)]
print("exception banners:", len(marks))
# last full block
i = marks[-1]
block = data[i:i + 2200]
print(block.replace("\r", ""))
