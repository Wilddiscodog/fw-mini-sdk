#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Switch WAMR memory to Alloc_With_Pool (4MB JieLi-malloc'd pool)."""
import io

P = "D:/fw-mini-sdk/app/mpx/mpx_host.c"
s = io.open(P, encoding="utf-8").read()

# 1) pool storage + helper
old = """#define MPX_TAG "[mpx] "
"""
new = """#define MPX_TAG "[mpx] "

/* WAMR 运行时内存池: JieLi malloc 出一块, 交给 WAMR(POOL 模式)。
 * SYSTEM 分配器路径(杰理 os_none.c 的 os_malloc)在 pi32v2 上实测
 * memory_mode 静态量写读不一致, POOL 模式绕开并顺带固化预算 */
#define MPX_WAMR_POOL_SIZE (6 * 1024 * 1024)
static uint8_t *mpx_wamr_pool;

static void mpx_wamr_pool_init(void)
{
    if (mpx_wamr_pool == NULL) {
        mpx_wamr_pool = (uint8_t *)malloc(MPX_WAMR_POOL_SIZE);
        printf(MPX_TAG "wamr pool %u bytes @%p\\n",
               (unsigned)MPX_WAMR_POOL_SIZE, (void *)mpx_wamr_pool);
    }
}

static void mpx_fill_init_args(RuntimeInitArgs *init_args)
{
    memset(init_args, 0, sizeof(*init_args));
    init_args->mem_alloc_type = Alloc_With_Pool;
    init_args->mem_alloc_option.pool.heap_buf = mpx_wamr_pool;
    init_args->mem_alloc_option.pool.heap_size = MPX_WAMR_POOL_SIZE;
    init_args->max_thread_num = 1;
}
"""
assert old in s, "tag anchor"
s = s.replace(old, new, 1)

# 2) smoke uses the pool
old = """    memset(&init_args, 0, sizeof(init_args));
    init_args.mem_alloc_type = Alloc_With_System_Allocator;
    init_args.max_thread_num = 1;

    if (!wasm_runtime_full_init(&init_args)) {
        printf(MPX_TAG "smoke: runtime init FAILED\\n");
        return;
    }"""
new = """    mpx_wamr_pool_init();
    mpx_fill_init_args(&init_args);

    if (!wasm_runtime_full_init(&init_args)) {
        printf(MPX_TAG "smoke: runtime init FAILED\\n");
        return;
    }"""
assert old in s, "smoke anchor"
s = s.replace(old, new, 1)

# 3) app_run uses the pool
old = """    memset(&init_args, 0, sizeof(init_args));
    init_args.mem_alloc_type = Alloc_With_System_Allocator;
    init_args.max_thread_num = 1;

    if (!wasm_runtime_full_init(&init_args)) {
        printf(MPX_TAG "app: runtime init FAILED\\n");
        return;
    }"""
new = """    mpx_wamr_pool_init();
    mpx_fill_init_args(&init_args);

    if (!wasm_runtime_full_init(&init_args)) {
        printf(MPX_TAG "app: runtime init FAILED\\n");
        return;
    }"""
assert old in s, "app anchor"
s = s.replace(old, new, 1)

io.open(P, "w", encoding="utf-8").write(s)
print("pool mode wired")
