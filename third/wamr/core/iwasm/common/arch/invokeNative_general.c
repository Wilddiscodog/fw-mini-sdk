/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 * pi32v2: 无原型函数指针调用不会按被调函数的原型把参数放进寄存器，
 * native 读到的是调用前残留的寄存器。这里按 argc 转成有原型的调用。
 * 返回值留在返回寄存器里，供调用方按 int32/int64 读取。
 */

#include "../wasm_runtime_common.h"
#include "../wasm_exec_env.h"

uint32
invokeNative(void (*native_code)(void), uint32 argv[], uint32 argc)
{
    bh_assert(argc >= sizeof(WASMExecEnv *) / sizeof(uint32));

    switch (argc) {
        case 1:
            return ((uint32 (*)(uint32))native_code)(argv[0]);
        case 2:
            return ((uint32 (*)(uint32, uint32))native_code)(argv[0], argv[1]);
        case 3:
            return ((uint32 (*)(uint32, uint32, uint32))native_code)(
                argv[0], argv[1], argv[2]);
        case 4:
            return ((uint32 (*)(uint32, uint32, uint32, uint32))native_code)(
                argv[0], argv[1], argv[2], argv[3]);
        case 5:
            return ((uint32 (*)(uint32, uint32, uint32, uint32, uint32))native_code)(
                argv[0], argv[1], argv[2], argv[3], argv[4]);
        case 6:
            return ((uint32 (*)(uint32, uint32, uint32, uint32, uint32, uint32))
                        native_code)(argv[0], argv[1], argv[2], argv[3], argv[4],
                                     argv[5]);
        case 7:
            return ((uint32 (*)(uint32, uint32, uint32, uint32, uint32, uint32,
                                uint32))native_code)(argv[0], argv[1], argv[2],
                                                     argv[3], argv[4], argv[5],
                                                     argv[6]);
        case 8:
            return ((uint32 (*)(uint32, uint32, uint32, uint32, uint32, uint32,
                                uint32, uint32))native_code)(
                argv[0], argv[1], argv[2], argv[3], argv[4], argv[5], argv[6],
                argv[7]);
        case 9:
            return ((uint32 (*)(uint32, uint32, uint32, uint32, uint32, uint32,
                                uint32, uint32, uint32))native_code)(
                argv[0], argv[1], argv[2], argv[3], argv[4], argv[5], argv[6],
                argv[7], argv[8]);
        case 10:
            return ((uint32 (*)(uint32, uint32, uint32, uint32, uint32, uint32,
                                uint32, uint32, uint32, uint32))native_code)(
                argv[0], argv[1], argv[2], argv[3], argv[4], argv[5], argv[6],
                argv[7], argv[8], argv[9]);
        default: {
            WASMExecEnv *exec_env = *(WASMExecEnv **)argv;
            WASMModuleInstanceCommon *module_inst = exec_env->module_inst;
            wasm_runtime_set_exception(
                module_inst,
                "the argument number of native function exceeds maximum");
            return 0;
        }
    }
}
