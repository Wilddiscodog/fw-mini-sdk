/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 * JieLi AC79xx (pi32v2, "WL82") platform layer for the MicroPixel mini host.
 * Modeled on the WAMR zephyr port; OS primitives come from the JieLi SDK
 * (os_api.h: os_mutex/os_sem/thread_fork) rather than stock FreeRTOS calls.
 */

#ifndef _PLATFORM_INTERNAL_H
#define _PLATFORM_INTERNAL_H

/* JieLi headers typedef bool/uint8_t themselves (os_api.h -> typedef.h ->
 * cpu.h), so os_api.h MUST expand before <stdbool.h> turns bool into a
 * macro. Keep this include first. */
#include "os_api.h"

/* JieLi's typedef.h forces its own offsetof(&((T*)0)->m), which is not a
 * constant expression; WAMR uses offsetof() inside _Static_assert. Restore
 * the compiler builtin for everything compiled against this header. */
#ifdef offsetof
#undef offsetof
#endif
#define offsetof(type, memb) __builtin_offsetof(type, memb)

#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef BH_PLATFORM_JL82
#define BH_PLATFORM_JL82
#endif

/* os_setjmp is a plain macro on every WAMR platform that has no signal-based
   trap handling; we run with software bounds checks (no HW guard pages). */
#define os_setjmp setjmp
#define os_longjmp longjmp
static inline void
os_sigreturn(void)
{
}
static inline void
os_signal_unmask(void)
{
}

/* Statics guarded by this macro in wasm_c_api.c compile out; our korp_mutex
   needs a runtime os_mutex_create() call, so no static initializer exists. */
/* #define OS_THREAD_MUTEX_INITIALIZER not defined on purpose */

#define BH_APPLET_PRESERVED_STACK_SIZE (2 * BH_KB)

/* JieLi task priorities run 0..30 (higher = more urgent); keep WAMR threads
   below the LVGL/USB tasks and above the WiFi RX worker. */
#define BH_THREAD_DEFAULT_PRIORITY 10

typedef struct os_thread_wait_node *os_thread_wait_list;

/* Thread identity: JieLi has no "current pid" getter, but os_current_task()
   returns a stable per-task name string, so its pointer is the tid. */
typedef const char *korp_tid;
typedef OS_MUTEX korp_mutex;
typedef OS_SEM korp_sem;

typedef struct korp_cond {
    korp_mutex wait_list_lock;
    os_thread_wait_list thread_wait_list;
} korp_cond;

/* Only used by lib-pthread builds, which we do not enable. */
typedef struct korp_rwlock {
    korp_mutex mtx;
    int read_count;
} korp_rwlock;

/* The below types are used in platform_api_extension.h,
   we just define them to make the compiler happy */
typedef int os_file_handle;
typedef int os_dir_stream;
typedef int os_raw_file_handle;
typedef int os_poll_file_handle;
typedef unsigned int os_nfds_t;
typedef struct timespec os_timespec;

#define OS_DIR_STREAM_INVALID 0

#ifndef PATH_MAX
#define PATH_MAX 256
#endif

#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif

#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif

#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

static inline int
os_getpagesize(void)
{
    return 4096;
}

static inline os_file_handle
os_get_invalid_handle(void)
{
    return -1;
}

static inline int
os_sched_yield(void)
{
    os_time_dly(0);
    return 0;
}

#endif /* end of _PLATFORM_INTERNAL_H */
