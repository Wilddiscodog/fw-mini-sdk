/*
 * Tiny call gate for wasm_interp_call_wasm.
 *
 * pi32v2 clang 4.0.1 miscompiles the large interpreter: the incoming
 * argument registers are already wrong on entry. This file stays small
 * so the stores below are reliable; the large functions reload the slot.
 *
 * 默认关闭调试打印: 每次 WASM 入口 printf/mpx_note 会严重拖慢帧率。
 * 需要时: -DMPX_INTERP_DEBUG=1
 */
#include "wasm_interp.h"

#if defined(MPX_INTERP_DEBUG) && MPX_INTERP_DEBUG
#include <stdio.h>
extern void mpx_note(const char *msg);
#endif

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

#if defined(MPX_INTERP_DEBUG) && MPX_INTERP_DEBUG
    printf("[mpx] interp gate inst=%p exec=%p func=%p argc=%u\n",
           (void *)module_inst, (void *)exec_env, (void *)function, argc);
    {
        char note[96];
        snprintf(note, sizeof(note), "gate inst=%p func=%p argc=%u",
                 (void *)module_inst, (void *)function, argc);
        mpx_note(note);
    }
#endif

    wasm_interp_call_wasm_impl(module_inst, exec_env, function, argc, argv);
}
