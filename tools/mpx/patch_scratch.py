#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Cross-reset step tracking via the UPDATA scratch area (survives soft reset)."""
import io

P = "D:/fw-mini-sdk/app/mpx/mpx_host.c"
s = io.open(P, encoding="utf-8").read()
n = 0

old = '#define MPX_TAG "[mpx] "\n'
new = ('#define MPX_TAG "[mpx] "\n\n'
       '/* 跨复位步骤刻痕: UPDATA 保留区(0x1c7fd80..0x1c7fe00), soft reset 不清零 */\n'
       '#define MPX_SCRATCH ((volatile unsigned int *)0x1c7fd90)\n'
       'static void mpx_mark(unsigned int step) { MPX_SCRATCH[0] = step; }\n')
assert old in s, "tag"
s = s.replace(old, new, 1); n += 1

old = 'printf(MPX_TAG "task start [nolto-O0-nods]\\n");'
new = ('printf(MPX_TAG "task start [nolto-O0-nods], prev step=%u\\n", MPX_SCRATCH[0]);\n'
       '    mpx_mark(1);')
assert old in s, "taskstart"
s = s.replace(old, new, 1); n += 1

old = 'printf(MPX_TAG "smoke: runtime init ok (WAMR interpreter)\\n");'
new = ('printf(MPX_TAG "smoke: runtime init ok (WAMR interpreter)\\n"); mpx_mark(2);')
assert old in s, "initok"
s = s.replace(old, new, 1); n += 1

old = '''    module = wasm_runtime_load(k_hello_wasm, (uint32_t)HELLO_WASM_SIZE, mpx_error_buf,
                               sizeof(mpx_error_buf));'''
new = ('''    mpx_mark(3); /* register_natives survived */
    module = wasm_runtime_load(k_hello_wasm, (uint32_t)HELLO_WASM_SIZE, mpx_error_buf,
                               sizeof(mpx_error_buf));''')
assert old in s, "load"
s = s.replace(old, new, 1); n += 1

old = '''    inst = wasm_runtime_instantiate(module, 8 * 1024, 0, mpx_error_buf, sizeof(mpx_error_buf));'''
new = ('''    mpx_mark(4); /* load survived */
    inst = wasm_runtime_instantiate(module, 8 * 1024, 0, mpx_error_buf, sizeof(mpx_error_buf));''')
assert old in s, "instantiate"
s = s.replace(old, new, 1); n += 1

old = '''    start_func = wasm_runtime_lookup_function(inst, "_start");'''
new = ('''    mpx_mark(5); /* instantiate survived */
    start_func = wasm_runtime_lookup_function(inst, "_start");''')
assert old in s, "lookup"
s = s.replace(old, new, 1); n += 1

old = 'printf(MPX_TAG "smoke: calling _start() (expect guest log: 120)...\\n");'
new = 'printf(MPX_TAG "smoke: calling _start() (expect guest log: 120)...\\n"); mpx_mark(6);'
assert old in s, "call"
s = s.replace(old, new, 1); n += 1

io.open(P, "w", encoding="utf-8").write(s)
print("scratch marks:", n)
