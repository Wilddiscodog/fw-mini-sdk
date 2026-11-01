/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 * JieLi AC79xx (pi32v2) platform core: memory, printf, mmap-by-malloc,
 * time and file stubs. Threads live in jl82_thread.c.
 */

#include "platform_api_vmcore.h"
#include "platform_api_extension.h"

int
os_thread_sys_init();
void
os_thread_sys_destroy(void);

int
bh_platform_init()
{
    return os_thread_sys_init();
}

void
bh_platform_destroy()
{
    os_thread_sys_destroy();
}

/* os_malloc/os_realloc/os_free are NOT defined here: the JieLi closed libs
 * (os_none.c) already export them over the SDK heap, and platform_common.h
 * defaults BH_MALLOC/BH_FREE to those symbols. */

int
os_dumps_proc_mem_info(char *out, unsigned int size)
{
    (void)out;
    (void)size;
    return -1;
}

int
os_printf(const char *format, ...)
{
    int ret = 0;
    va_list ap;

    va_start(ap, format);
    ret += vprintf(format, ap);
    va_end(ap);

    return ret;
}

int
os_vprintf(const char *format, va_list ap)
{
    /* The JieLi libc has no vprintf; spool through a bounded buffer.
       Called from bh_log timestamps and wasm libc-builtin printf paths. */
    char buf[256];
    int n = vsnprintf(buf, sizeof(buf), format, ap);
    if (n > 0)
        puts(buf);
    return n > 0 ? n : 0;
}

/* ---- mmap-by-malloc (zephyr-style; no MMU on pi32v2) ---- */

void *
os_mmap(void *hint, size_t size, int prot, int flags, os_file_handle file)
{
    void *addr;

    (void)hint;
    (void)prot;
    (void)flags;
    (void)file;

    if ((uint64)size >= UINT32_MAX)
        return NULL;

    addr = BH_MALLOC((uint32)size);
    if (addr)
        memset(addr, 0, (uint32)size);
    return addr;
}

void *
os_mremap(void *old_addr, size_t old_size, size_t new_size)
{
    return os_mremap_slow(old_addr, old_size, new_size);
}

void
os_munmap(void *addr, size_t size)
{
    (void)size;
    BH_FREE(addr);
}

int
os_mprotect(void *addr, size_t size, int prot)
{
    (void)addr;
    (void)size;
    (void)prot;
    return 0;
}

int
os_mem_commit(void *addr, size_t size, int flags)
{
    (void)addr;
    (void)size;
    (void)flags;
    return 0;
}

int
os_mem_decommit(void *addr, size_t size)
{
    (void)addr;
    (void)size;
    return 0;
}

os_raw_file_handle
os_invalid_raw_handle(void)
{
    return -1;
}

/* ---- time (10 ms system tick; refined clock for guests lives in the
   MicroPixel host, not here) ---- */

uint64
os_time_get_boot_us()
{
    return (uint64)os_time_get() * 10000;
}

uint64
os_time_get_boot_microsecond()
{
    return os_time_get_boot_us() / 1000;
}

uint64
os_time_thread_cputime_us(void)
{
    /* Single-threaded runtime: report wall clock. */
    return os_time_get_boot_us();
}

int
os_usleep(uint32 usec)
{
    uint64 deadline = os_time_get_boot_us() + usec;

    if (usec >= 10000) {
        os_time_dly((int)(usec / 10000));
        return 0;
    }
    while (os_time_get_boot_us() < deadline)
        ;
    return 0;
}

/* ---- file stubs (no wasi/libc-wasi in this build) ---- */

char *
os_realpath(const char *path, char *resolved_path)
{
    (void)path;
    (void)resolved_path;
    return NULL;
}

__wasi_errno_t
os_open_preopendir(const char *path, os_file_handle *out)
{
    (void)path;
    (void)out;
    return (__wasi_errno_t)-1;
}

os_file_handle
os_convert_stdin_handle(os_raw_file_handle raw_stdin)
{
    (void)raw_stdin;
    return -1;
}

os_file_handle
os_convert_stdout_handle(os_raw_file_handle raw_stdout)
{
    (void)raw_stdout;
    return -1;
}

os_file_handle
os_convert_stderr_handle(os_raw_file_handle raw_stderr)
{
    (void)raw_stderr;
    return -1;
}
