#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Add flush delays after key prints so they survive the crash point."""
import io

P = "D:/fw-mini-sdk/app/mpx/mpx_host.c"
s = io.open(P, encoding="utf-8").read()
n = 0

pairs = [
    ('printf(MPX_TAG "smoke: runtime init ok (WAMR interpreter)\\n");',
     'printf(MPX_TAG "smoke: runtime init ok (WAMR interpreter)\\n"); os_time_dly(3);'),
    ('printf(MPX_TAG "smoke: register natives FAILED\\n");',
     'printf(MPX_TAG "smoke: register natives FAILED\\n"); os_time_dly(3);'),
    ('printf(MPX_TAG "smoke: load failed: %s\\n", mpx_error_buf);',
     'printf(MPX_TAG "smoke: load failed: %s\\n", mpx_error_buf); os_time_dly(3);'),
    ('printf(MPX_TAG "smoke: instantiate failed: %s\\n", mpx_error_buf);',
     'printf(MPX_TAG "smoke: instantiate failed: %s\\n", mpx_error_buf); os_time_dly(3);'),
    ('printf(MPX_TAG "smoke: call failed: %s\\n", exception ? exception : "?");',
     'printf(MPX_TAG "smoke: call failed: %s\\n", exception ? exception : "?"); os_time_dly(3);'),
    ('printf(MPX_TAG "smoke SMOKE PASS\\n");',
     'printf(MPX_TAG "smoke SMOKE PASS\\n"); os_time_dly(3);'),
]
for old, new in pairs:
    if old in s and "os_time_dly(3)" not in s.split(old)[1][:20]:
        s = s.replace(old, new, 1)
        n += 1
    elif old in s:
        n += 1  # already has delay
io.open(P, "w", encoding="utf-8").write(s)
print("flush delays:", n)
