#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Switch WAMR memory to Alloc_With_Allocator over JieLi malloc (no EMS pool)."""
import io

P = "D:/fw-mini-sdk/app/mpx/mpx_host.c"
s = io.open(P, encoding="utf-8").read()

old = """static void mpx_fill_init_args(RuntimeInitArgs *init_args)
{
    memset(init_args, 0, sizeof(*init_args));
    init_args->mem_alloc_type = Alloc_With_Pool;
    init_args->mem_alloc_option.pool.heap_buf = mpx_wamr_pool;
    init_args->mem_alloc_option.pool.heap_size = MPX_WAMR_POOL_SIZE;
    init_args->max_thread_num = 1;
}"""
new = """static void mpx_fill_init_args(RuntimeInitArgs *init_args)
{
    memset(init_args, 0, sizeof(*init_args));
    /* 直接用 JieLi malloc/realloc/free(8 字节对齐保证), 彻底绕开 WAMR 的
     * EMS/TLSF 自定义分配器 —— 导入名 memmove 写异常的排除项 */
    init_args->mem_alloc_type = Alloc_With_Allocator;
    init_args->mem_alloc_option.allocator.malloc_func = (void *)malloc;
    init_args->mem_alloc_option.allocator.realloc_func = (void *)realloc;
    init_args->mem_alloc_option.allocator.free_func = (void *)free;
    init_args->max_thread_num = 1;
}"""
assert old in s, "fill anchor"
s = s.replace(old, new, 1)

# pool init can stay (harmless) but stop wasting 6MB: shrink to 0
old2 = "#define MPX_WAMR_POOL_SIZE (6 * 1024 * 1024)"
new2 = "#define MPX_WAMR_POOL_SIZE (64 * 1024) /* 仅占位, 分配器已切 JieLi malloc */"
assert old2 in s, "pool size"
s = s.replace(old2, new2, 1)

io.open(P, "w", encoding="utf-8").write(s)
print("allocator switched to JieLi malloc")
