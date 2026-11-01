#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Wire the no-call isolation test into the smoke flow."""
import io

P = "D:/fw-mini-sdk/app/mpx/mpx_host.c"
s = io.open(P, encoding="utf-8").read()

old = """#include "hello_wasm.h"
"""
new = """#include "hello_wasm.h"
#include "hello_nocall.h"
"""
assert old in s, "include"
s = s.replace(old, new, 1)

old = """        memcpy(s_wasm_copy, k_hello_wasm, HELLO_WASM_SIZE);
        printf(MPX_TAG "S: wasm copy @%p\\n", (void *)s_wasm_copy);
        os_time_dly(3);
        module = wasm_runtime_load(s_wasm_copy, (uint32_t)HELLO_WASM_SIZE, mpx_error_buf,
                                   sizeof(mpx_error_buf));"""
new = """        memcpy(s_wasm_copy, k_hello_nocall, HELLO_NOCALL_SIZE);
        printf(MPX_TAG "S: wasm copy @%p\\n", (void *)s_wasm_copy);
        os_time_dly(3);
        module = wasm_runtime_load(s_wasm_copy, (uint32_t)HELLO_NOCALL_SIZE, mpx_error_buf,
                                   sizeof(mpx_error_buf));"""
assert old in s, "load swap"
s = s.replace(old, new, 1)

old = """    printf(MPX_TAG "smoke: calling _start() (expect guest log: 120)...\\n"); mpx_mark(6);"""
new = """    printf(MPX_TAG "smoke: calling nocall _start()...\\n"); os_time_dly(3); mpx_mark(6);
    if (!wasm_runtime_call_wasm(exec_env, start_func, 0, NULL)) {
        exception = wasm_runtime_get_exception(inst);
        printf(MPX_TAG "smoke: nocall call failed: %s\\n", exception ? exception : "?");
        os_time_dly(3);
        goto out;
    }
    printf(MPX_TAG "smoke: NOCALL PASS\\n"); os_time_dly(3);
    printf(MPX_TAG "smoke: calling _start() (expect guest log: 120)...\\n"); mpx_mark(6);"""
assert old in s, "call site"
s = s.replace(old, new, 1)

io.open(P, "w", encoding="utf-8").write(s)
print("nocall test wired")
