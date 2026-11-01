#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Big task stack + load wasm from an aligned heap copy."""
import io

P = "D:/fw-mini-sdk/app/mpx/mpx_host.c"
s = io.open(P, encoding="utf-8").read()
n = 0

# 1) 64KB task stack (app_main.c) — already applied?
Q = "D:/fw-mini-sdk/app/app_main.c"
t = io.open(Q, encoding="utf-8").read()
if "16384, 0, \"mpx_task\"" in t:
    print("task stack already 16384")
else:
    old = 'os_task_create(mpx_task, NULL, 10, 4096, 0, "mpx_task");'
    assert old in t, "task stack"
    t = t.replace(old, 'os_task_create(mpx_task, NULL, 10, 16384, 0, "mpx_task");', 1)
    io.open(Q, "w", encoding="utf-8").write(t)
    print("task stack bumped")
n = 0

# 2) aligned heap copy before load (smoke)
old = """    printf(MPX_TAG "S: loading module\\n"); os_time_dly(3);
    module = wasm_runtime_load(k_hello_wasm, (uint32_t)HELLO_WASM_SIZE, mpx_error_buf,
                               sizeof(mpx_error_buf));"""
new = """    printf(MPX_TAG "S: loading module\\n"); os_time_dly(3);
    {
        /* 拷到对齐堆缓冲再加载: 消除 rodata 内非对齐偏移的一切隐患 */
        static uint8_t *s_wasm_copy;
        if (s_wasm_copy == NULL) {
            s_wasm_copy = (uint8_t *)malloc(HELLO_WASM_SIZE + 16);
        }
        memcpy(s_wasm_copy, k_hello_wasm, HELLO_WASM_SIZE);
        printf(MPX_TAG "S: wasm copy @%p\\n", (void *)s_wasm_copy);
        os_time_dly(3);
        module = wasm_runtime_load(s_wasm_copy, (uint32_t)HELLO_WASM_SIZE, mpx_error_buf,
                                   sizeof(mpx_error_buf));
    }"""
assert old in s, "load copy"
s = s.replace(old, new, 1)
n += 1

io.open(P, "w", encoding="utf-8").write(s)
print("stack+copy:", n)
