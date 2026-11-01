#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Bind guest instance into mpx_host.c app flow (M3)."""
import io

P = "D:/fw-mini-sdk/app/mpx/mpx_host.c"
s = io.open(P, encoding="utf-8").read()

old = """    exec_env = wasm_exec_env_create(inst, 16 * 1024);
    if (!exec_env) {
        printf(MPX_TAG "app: exec env create FAILED\\n");
        goto out;
    }"""
new = """    mpx_services_set_guest_inst(inst);

    exec_env = wasm_exec_env_create(inst, 16 * 1024);
    if (!exec_env) {
        printf(MPX_TAG "app: exec env create FAILED\\n");
        goto out;
    }"""
assert old in s, "create anchor"
s = s.replace(old, new, 1)

old = """out:
    if (exec_env)
        wasm_exec_env_destroy(exec_env);
    if (inst)
        wasm_runtime_deinstantiate(inst);
    if (module)
        wasm_runtime_unload(module);
    wasm_runtime_destroy();
    mpx_services_deinit();
}"""
new = """out:
    mpx_services_set_guest_inst(NULL);
    if (exec_env)
        wasm_exec_env_destroy(exec_env);
    if (inst)
        wasm_runtime_deinstantiate(inst);
    if (module)
        wasm_runtime_unload(module);
    wasm_runtime_destroy();
    mpx_services_deinit();
}"""
assert old in s, "out anchor"
s = s.replace(old, new, 1)

io.open(P, "w", encoding="utf-8").write(s)
print("host wired")
