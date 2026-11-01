#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Align embedded wasm arrays + flush step prints (20ms delay each)."""
import io

P = "D:/fw-mini-sdk/app/mpx/mpx_host.c"
s = io.open(P, encoding="utf-8").read()
n = 0

# 1) align the embedded arrays (loader reads + %.*s sources)
pairs = [
    ('#include "hello_wasm.h"',
     '#include "hello_wasm.h"\n\n'
     '/* pi32v2 无非对齐字访问: loader/vsnprintf 会按字读模块缓冲 */\n'
     '__attribute__((aligned(4)))\nstatic const unsigned char k_hello_wasm_holder_marker;'),
]
# hello_wasm.h 的数组在外部头里, 给头内数组加属性:
h = "D:/fw-mini-sdk/app/mpx/hello_wasm.h"
t = io.open(h, encoding="utf-8").read()
if "aligned(4)" not in t:
    t = t.replace("static const unsigned char k_hello_wasm[] = {",
                  "static const unsigned char k_hello_wasm[] __attribute__((aligned(4))) = {")
    io.open(h, "w", encoding="utf-8").write(t)
    print("hello_wasm.h aligned")
b = "D:/fw-mini-sdk/app/mpx/app_wasm_bundle.h"
t = io.open(b, encoding="utf-8").read()
if "aligned(4)" not in t:
    t = t.replace("static const unsigned char k_mpx_app_bundle[] = {",
                  "static const unsigned char k_mpx_app_bundle[] __attribute__((aligned(4))) = {")
    io.open(b, "w", encoding="utf-8").write(t)
    print("app_wasm_bundle.h aligned")

# 2) step prints with flush delay
for step in ("step1 registering natives", "step2 natives ok, loading module",
             "step3 module loaded, instantiating", "step4 instantiated, creating exec env"):
    old = 'printf(MPX_TAG "smoke: %s\\n");' % step
    new = 'printf(MPX_TAG "smoke: %s\\n"); os_time_dly(2);' % step
    if old in s:
        s = s.replace(old, new, 1)
        n += 1
io.open(P, "w", encoding="utf-8").write(s)
print("flush delays added:", n)
