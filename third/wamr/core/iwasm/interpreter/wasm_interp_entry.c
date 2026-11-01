/*
 * Tiny call gate for wasm_interp_call_wasm.
 *
 * pi32v2 clang 4.0.1 miscompiles the large interpreter: the incoming
 * argument registers are already wrong on entry. This file stays small
 * so the stores below are reliable; the large functions reload the slot.
 */
#include "wasm_interp.h"

#include <stdio.h>

extern void mpx_note(const char *msg);

volatile MpxInterpCallSlot g_mpx_interp_slot;

void
wasm_interp_call_wasm(struct WASMModuleInstance *module_inst,
                      struct WASMExecEnv *exec_env,
                      struct WASMFunctionInstance *function, uint32 argc,
                      uint32 argv[])
{
    g_mpx_interp_slot.module_inst = module_inst;
    g_mpx_interp_slot.exec_env = exec_env;
    g_mpx_interp_slot.function = function;
    g_mpx_interp_slot.argc = argc;
    g_mpx_interp_slot.argv = argv;
    g_mpx_interp_slot.frame = NULL;

    printf("[mpx] interp gate inst=%p exec=%p func=%p argc=%u\n",
           (void *)module_inst, (void *)exec_env, (void *)function, argc);
    {
        char note[96];
        snprintf(note, sizeof(note), "gate inst=%p func=%p argc=%u",
                 (void *)module_inst, (void *)function, argc);
        mpx_note(note);
    }

    wasm_interp_call_wasm_impl(module_inst, exec_env, function, argc, argv);
}
