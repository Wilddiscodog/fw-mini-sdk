#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Add build marker to mpx task start print."""
import io

P = "D:/fw-mini-sdk/app/mpx/mpx_host.c"
s = io.open(P, encoding="utf-8").read()
old = 'printf(MPX_TAG "task start\\n");'
new = 'printf(MPX_TAG "task start [nolto-O0]\\n");'
assert old in s, "anchor not found"
s = s.replace(old, new, 1)
io.open(P, "w", encoding="utf-8").write(s)
print("marker added")
