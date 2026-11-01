#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Bracket the crash: print+flush at every smoke step."""
import io

P = "D:/fw-mini-sdk/app/mpx/mpx_host.c"
s = io.open(P, encoding="utf-8").read()
n = 0

def add_after(anchor, text):
    global s, n
    assert anchor in s, anchor[:40]
    assert text not in s, text[:40]
    s = s.replace(anchor, anchor + "\n    " + text, 1)
    n += 1

add_after(
    'if (!wasm_runtime_register_natives("env", mpx_env_symbols,\n'
    '                                       sizeof(mpx_env_symbols) / sizeof(mpx_env_symbols[0]))) {',
    'printf(MPX_TAG "S: registered natives\\n"); os_time_dly(3);')
add_after(
    'printf(MPX_TAG "smoke: load failed: %s\\n", mpx_error_buf); os_time_dly(3);',
    'os_time_dly(1);')
add_after(
    'mpx_mark(3); /* register_natives survived */',
    'printf(MPX_TAG "S: loading module\\n"); os_time_dly(3);')
add_after(
    'mpx_mark(4); /* load survived */',
    'printf(MPX_TAG "S: module loaded\\n"); os_time_dly(3);')
add_after(
    'mpx_mark(5); /* instantiate survived */',
    'printf(MPX_TAG "S: instantiated\\n"); os_time_dly(3);')

io.open(P, "w", encoding="utf-8").write(s)
print("brackets:", n)
