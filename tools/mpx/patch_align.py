#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Align mpx_error_buf to 4 (pi32v2 has no unaligned word access)."""
import io

P = "D:/fw-mini-sdk/app/mpx/mpx_host.c"
s = io.open(P, encoding="utf-8").read()
old = "static char mpx_error_buf[256];"
new = ("/* pi32v2 has no unaligned word access: WAMR set_error_buf memsets this\n"
       " * buffer word-wide, so it must be 4-byte aligned. */\n"
       "static char mpx_error_buf[256] __attribute__((aligned(4)));")
assert old in s, "anchor"
s = s.replace(old, new, 1)
io.open(P, "w", encoding="utf-8").write(s)
print("aligned")
