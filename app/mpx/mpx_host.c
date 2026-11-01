/*
 * app/mpx/mpx_host.c - MicroPixel 兼容宿主(WL82)
 *
 * 两条验证链路:
 *   1) mpx_smoke_test(): 内嵌 hello.wasm 走 env.jl_log(解释器点亮,M1)
 *   2) mpx_app_run():    内嵌 App Bundle(WASM 段)→ 加载/实例化/调
 *      __micropixel_start,注册 micropixel 模块的 7 个 ABI natives(M2)
 *
 * 签名串与 micropixel firmware runtime/abi/native_symbols.c 完全一致。
 * MINI_WASM_SUPPORT=1 时参与编译。
 */
#include "app_config.h"

#if (defined MINI_WASM_SUPPORT) && MINI_WASM_SUPPORT

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "os/os_api.h"
#include "update/net_update.h"
#include "wasm_export.h"

#include "mpx_compat.h"
#include "mpx_bundle.h"
#include "mpx_services.h"
#include "mpx_store.h"

#include "hello_wasm.h"
#include "hello_nocall.h"

#define MPX_TAG "[mpx] "

/* 网页 /mpx 可读。printf 若把 CPU1 打挂，状态仍留在这块内存里。 */
char mpx_status_line[160] = "wasm task not created";
volatile unsigned mpx_step_count;

void mpx_note(const char *msg)
{
    unsigned i = 0;
    if (msg == NULL) {
        msg = "";
    }
    while (msg[i] != '\0' && i + 1 < sizeof(mpx_status_line)) {
        mpx_status_line[i] = msg[i];
        i++;
    }
    mpx_status_line[i] = '\0';
}

/* 跨复位步骤刻痕: UPDATA 保留区(0x1c7fd80..0x1c7fe00), soft reset 不清零 */
#define MPX_SCRATCH ((volatile unsigned int *)0x1c7fd90)
static void mpx_mark(unsigned int step) { MPX_SCRATCH[0] = step; }

/* WAMR 运行时内存池: JieLi malloc 出一块, 交给 WAMR(POOL 模式)。
 * SYSTEM 分配器路径(杰理 os_none.c 的 os_malloc)在 pi32v2 上实测
 * memory_mode 静态量写读不一致, POOL 模式绕开并顺带固化预算 */
#define MPX_WAMR_POOL_SIZE (64 * 1024) /* 仅占位, 分配器已切 JieLi malloc */
static uint8_t *mpx_wamr_pool;

static void mpx_wamr_pool_init(void)
{
    if (mpx_wamr_pool == NULL) {
        mpx_wamr_pool = (uint8_t *)malloc(MPX_WAMR_POOL_SIZE);
        printf(MPX_TAG "wamr pool %u bytes @%p\n",
               (unsigned)MPX_WAMR_POOL_SIZE, (void *)mpx_wamr_pool);
    }
}

static void mpx_fill_init_args(RuntimeInitArgs *init_args)
{
    memset(init_args, 0, sizeof(*init_args));
    /* 直接用 JieLi malloc/realloc/free(8 字节对齐保证), 彻底绕开 WAMR 的
     * EMS/TLSF 自定义分配器 —— 导入名 memmove 写异常的排除项 */
    init_args->mem_alloc_type = Alloc_With_Allocator;
    init_args->mem_alloc_option.allocator.malloc_func = (void *)malloc;
    init_args->mem_alloc_option.allocator.realloc_func = (void *)realloc;
    init_args->mem_alloc_option.allocator.free_func = (void *)free;
    init_args->max_thread_num = 1;
}

/* ---------------- 应用时钟(与 mpx_services 共用) ---------------- */

/* ---------------- micropixel 模块 natives(语义对齐 guest_abi.cpp) ---------------- */

static uint32_t host_abi_version(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return (uint32_t)MICROPIXEL_ABI_VERSION;
}

static int32_t host_log_write(wasm_exec_env_t exec_env, uint32_t level, const uint8_t *bytes,
                              uint32_t length)
{
    (void)exec_env;
    if ((bytes == NULL && length != 0) || length > MICROPIXEL_ABI_MAX_LOG_BYTES) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
    if (level != MICROPIXEL_LOG_DEBUG && level != MICROPIXEL_LOG_INFO &&
        level != MICROPIXEL_LOG_WARNING && level != MICROPIXEL_LOG_ERROR) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
    printf(MPX_TAG "guest[%u]: %.*s\n", (unsigned)level, (int)length, (const char *)bytes);
    if (bytes != NULL && length > 0) {
        char tmp[80];
        uint32_t n = length < sizeof(tmp) - 1 ? length : (uint32_t)sizeof(tmp) - 1;
        uint32_t i;
        for (i = 0; i < n; i++) {
            tmp[i] = (char)bytes[i];
        }
        tmp[n] = '\0';
        mpx_note(tmp);
    }
    return MICROPIXEL_STATUS_OK;
}

static int32_t host_event_wait(wasm_exec_env_t exec_env, micropixel_event_t *event_out,
                               uint32_t event_capacity, uint64_t timeout_us)
{
    (void)exec_env;
    if (event_out == NULL || event_capacity < sizeof(micropixel_event_t)) {
        return MICROPIXEL_STATUS_BUFFER_TOO_SMALL;
    }
    return mpx_event_wait(event_out, timeout_us);
}

static uint64_t host_clock_now(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return mpx_app_time_now();
}

/* 最近一次 ABI 调用的实参和返回值，/mpx 用来看 guest 卡在哪一步。 */
volatile unsigned mpx_nat_seq, mpx_nat_cells, mpx_nat_lp0;

static char mpx_abi_op[8] = "none";
static volatile uint32_t mpx_abi_a, mpx_abi_b, mpx_abi_c;
static volatile int32_t mpx_abi_st = -1;

static void mpx_abi_rec(const char *op, uint32_t a, uint32_t b, uint32_t c, int32_t st)
{
    unsigned i = 0;
    while (op[i] && i < sizeof(mpx_abi_op) - 1) {
        mpx_abi_op[i] = op[i];
        i++;
    }
    mpx_abi_op[i] = '\0';
    mpx_abi_a = a;
    mpx_abi_b = b;
    mpx_abi_c = c;
    mpx_abi_st = st;
}

static int32_t host_service_open(wasm_exec_env_t exec_env, uint32_t service_id,
                                 uint32_t required_interface_version,
                                 micropixel_service_info_t *info_out, uint32_t info_capacity)
{
    int32_t st;
    (void)exec_env;
    if (info_out == NULL || info_capacity < sizeof(*info_out)) {
        st = MICROPIXEL_STATUS_BUFFER_TOO_SMALL;
    } else {
        st = mpx_service_open(service_id, required_interface_version, info_out, info_capacity);
    }
    mpx_abi_rec("open", service_id, required_interface_version, info_capacity, st);
    return st;
}

static int32_t host_service_call(wasm_exec_env_t exec_env, uint32_t service_handle,
                                 uint32_t method_id, const uint8_t *request, uint32_t request_size,
                                 uint8_t *response, uint32_t response_capacity,
                                 uint32_t *response_size_out)
{
    int32_t st;
    (void)exec_env;
    if (response_size_out == NULL || (request == NULL && request_size != 0) ||
        (response == NULL && response_capacity != 0)) {
        st = MICROPIXEL_STATUS_INVALID_ARGUMENT;
    } else {
        st = mpx_service_call(service_handle, method_id, request, request_size, response,
                              response_capacity, response_size_out);
    }
    mpx_abi_rec("call", service_handle, method_id, request_size, st);
    return st;
}

static int32_t host_service_submit(wasm_exec_env_t exec_env, uint32_t service_handle,
                                   uint32_t channel_id, const uint8_t *bytes, uint32_t length)
{
    int32_t st;
    (void)exec_env;
    if (bytes == NULL || length == 0) {
        st = MICROPIXEL_STATUS_INVALID_ARGUMENT;
    } else {
        st = mpx_service_submit(service_handle, channel_id, bytes, length);
    }
    mpx_abi_rec("sub", service_handle, channel_id, length, st);
    return st;
}

static NativeSymbol mpx_symbols[] = {
    { "abi_version", host_abi_version, "()i", NULL },
    { "log_write", host_log_write, "(i*~)i", NULL },
    { "event_wait", host_event_wait, "(*~I)i", NULL },
    { "clock_now", host_clock_now, "()I", NULL },
    { "service_open", host_service_open, "(ii*~)i", NULL },
    { "service_call", host_service_call, "(ii*~*~*)i", NULL },
    { "service_submit", host_service_submit, "(ii*~)i", NULL },
};

/* env.jl_log - M1 冒烟模块的导入 */
static void jl_log_native(wasm_exec_env_t exec_env, int32_t value)
{
    (void)exec_env;
    printf(MPX_TAG "guest log: %d\n", value);
}

static NativeSymbol mpx_env_symbols[] = {
    { "jl_log", jl_log_native, "(i)", NULL },
};

/* pi32v2 has no unaligned word access: WAMR set_error_buf memsets this
 * buffer word-wide, so it must be 4-byte aligned. */
static char mpx_error_buf[256] __attribute__((aligned(4)));

/* ---------------- M1:解释器冒烟(内嵌 hello.wasm) ---------------- */

static void mpx_smoke_test(void)
{
    RuntimeInitArgs init_args;
    wasm_module_t module = NULL;
    wasm_module_inst_t inst = NULL;
    wasm_exec_env_t exec_env = NULL;
    wasm_function_inst_t start_func = NULL;
    const char *exception = NULL;

    mpx_wamr_pool_init();
    mpx_fill_init_args(&init_args);

    if (!wasm_runtime_full_init(&init_args)) {
        printf(MPX_TAG "smoke: runtime init FAILED\n");
        return;
    }
    printf(MPX_TAG "smoke: runtime init ok (WAMR interpreter)\n"); os_time_dly(3); mpx_mark(2);

    if (!wasm_runtime_register_natives("env", mpx_env_symbols,
                                       sizeof(mpx_env_symbols) / sizeof(mpx_env_symbols[0]))) {
    printf(MPX_TAG "S: registered natives\n"); os_time_dly(3);
        printf(MPX_TAG "smoke: register natives FAILED\n"); os_time_dly(3);
        goto out;
    }

    mpx_mark(3); /* register_natives survived */
    printf(MPX_TAG "S: loading module\n"); os_time_dly(3);
    {
        /* 拷到对齐堆缓冲再加载: 消除 rodata 内非对齐偏移的一切隐患 */
        static uint8_t *s_wasm_copy;
        if (s_wasm_copy == NULL) {
            s_wasm_copy = (uint8_t *)malloc(HELLO_WASM_SIZE + 16);
        }
        memcpy(s_wasm_copy, k_hello_nocall, HELLO_NOCALL_SIZE);
        printf(MPX_TAG "S: wasm copy @%p\n", (void *)s_wasm_copy);
        os_time_dly(3);
        module = wasm_runtime_load(s_wasm_copy, (uint32_t)HELLO_NOCALL_SIZE, mpx_error_buf,
                                   sizeof(mpx_error_buf));
    }
    if (!module) {
        printf(MPX_TAG "smoke: load failed: %s\n", mpx_error_buf); os_time_dly(3);
    os_time_dly(1);
        goto out;
    }

    mpx_mark(4); /* load survived */
    printf(MPX_TAG "S: module loaded\n"); os_time_dly(3);
    inst = wasm_runtime_instantiate(module, 8 * 1024, 0, mpx_error_buf, sizeof(mpx_error_buf));
    if (!inst) {
        printf(MPX_TAG "smoke: instantiate failed: %s\n", mpx_error_buf); os_time_dly(3);
        goto out;
    }

    exec_env = wasm_exec_env_create(inst, 8 * 1024);
    if (!exec_env) {
        printf(MPX_TAG "smoke: exec env create FAILED\n");
        goto out;
    }

    mpx_mark(5); /* instantiate survived */
    printf(MPX_TAG "S: instantiated\n"); os_time_dly(3);
    start_func = wasm_runtime_lookup_function(inst, "_start");
    if (!start_func) {
        printf(MPX_TAG "smoke: lookup _start FAILED\n");
        goto out;
    }

    printf(MPX_TAG "smoke: calling nocall _start()...\n"); os_time_dly(3); mpx_mark(6);
    mpx_note("calling wasm");
    if (!wasm_runtime_call_wasm(exec_env, start_func, 0, NULL)) {
        exception = wasm_runtime_get_exception(inst);
        printf(MPX_TAG "smoke: nocall call failed: %s\n", exception ? exception : "?");
        os_time_dly(3);
        goto out;
    }
    printf(MPX_TAG "smoke: NOCALL PASS\n"); os_time_dly(3);
    printf(MPX_TAG "smoke: calling _start() (expect guest log: 120)...\n"); mpx_mark(6);
    if (!wasm_runtime_call_wasm(exec_env, start_func, 0, NULL)) {
        exception = wasm_runtime_get_exception(inst);
        printf(MPX_TAG "smoke: call failed: %s\n", exception ? exception : "?"); os_time_dly(3);
        goto out;
    }
    exception = wasm_runtime_get_exception(inst);
    if (exception) {
        printf(MPX_TAG "smoke: exception: %s\n", exception);
    } else {
        printf(MPX_TAG "smoke SMOKE PASS\n"); os_time_dly(3);
    }

out:
    if (exec_env)
        wasm_exec_env_destroy(exec_env);
    if (inst)
        wasm_runtime_deinstantiate(inst);
    if (module)
        wasm_runtime_unload(module);
    wasm_runtime_destroy();
}

/* ---------------- M2:App Bundle 流程 ---------------- */

static void mpx_app_run(uint8_t *wasm_buf, uint32_t wasm_size)
{
    RuntimeInitArgs init_args;
    wasm_module_t module = NULL;
    wasm_module_inst_t inst = NULL;
    wasm_exec_env_t exec_env = NULL;
    wasm_function_inst_t entry = NULL;
    const char *exception = NULL;
    int32_t status;

    mpx_note("app init");
    mpx_services_init();
    status = mpx_display_init();
    if (status != MICROPIXEL_STATUS_OK) {
        printf(MPX_TAG "display init FAILED: %d\n", status);
        mpx_note("display init failed");
        free(wasm_buf);
        mpx_services_deinit();
        return;
    }

    mpx_wamr_pool_init();
    mpx_fill_init_args(&init_args);

    if (!wasm_runtime_full_init(&init_args)) {
        printf(MPX_TAG "app: runtime init FAILED\n");
        mpx_note("runtime init failed");
        free(wasm_buf);
        mpx_services_deinit();
        return;
    }
    if (!wasm_runtime_register_natives("micropixel", mpx_symbols,
                                       sizeof(mpx_symbols) / sizeof(mpx_symbols[0]))) {
        printf(MPX_TAG "app: register ABI natives FAILED\n");
        mpx_note("register abi failed");
        goto out;
    }

    if (wasm_buf == NULL || wasm_size == 0) {
        mpx_note("wasm missing");
        goto out;
    }
    printf(MPX_TAG "app: bundle wasm=%u bytes\n", (unsigned)wasm_size);
    mpx_note("app load");
    module = wasm_runtime_load(wasm_buf, wasm_size, mpx_error_buf, sizeof(mpx_error_buf));
    if (!module) {
        printf(MPX_TAG "app: wasm load failed: %s\n", mpx_error_buf);
        mpx_note("wasm load failed");
        goto out;
    }

    /* exec 栈给解释器操作数; app 堆给 libc-builtin。线性内存另从系统堆增长。 */
    inst = wasm_runtime_instantiate(module, 16 * 1024, 256 * 1024, mpx_error_buf,
                                    sizeof(mpx_error_buf));
    if (!inst) {
        printf(MPX_TAG "app: instantiate failed: %s\n", mpx_error_buf);
        mpx_note("instantiate failed");
        goto out;
    }

    mpx_services_set_guest_inst(inst);

    exec_env = wasm_exec_env_create(inst, 16 * 1024);
    if (!exec_env) {
        printf(MPX_TAG "app: exec env create FAILED\n");
        mpx_note("exec env failed");
        goto out;
    }

    entry = wasm_runtime_lookup_function(inst, "__micropixel_start");
    if (!entry) {
        entry = wasm_runtime_lookup_function(inst, "_start");
    }
    if (!entry) {
        printf(MPX_TAG "app: no entry export (__micropixel_start/_start)\n");
        mpx_note("no entry");
        goto out;
    }

    /* 先停 LVGL, 留出正在刷的那一帧, 再让 guest 画。 */
    {
        extern volatile unsigned char mpx_display_hold;
        mpx_display_hold = 1;
        os_time_dly(30);
    }
    printf(MPX_TAG "app: starting\n");
    mpx_nat_seq = 0;
    mpx_note("app call");
    mpx_store_set_busy(1);
    if (!wasm_runtime_call_wasm(exec_env, entry, 0, NULL)) {
        exception = wasm_runtime_get_exception(inst);
        printf(MPX_TAG "app: entry failed: %s\n", exception ? exception : "?");
        mpx_note(exception ? exception : "entry failed");
        goto out;
    }
    exception = wasm_runtime_get_exception(inst);
    printf(MPX_TAG "app: %s\n", exception ? exception : "entry returned cleanly");
    if (exception) {
        mpx_note(exception);
    } else if (strcmp(mpx_status_line, "present") != 0) {
        char line[96];
        snprintf(line, sizeof(line), "v2 c=%u lp=%u %s %u st=%d",
                 mpx_nat_cells, mpx_nat_lp0, mpx_abi_op, (unsigned)mpx_abi_a,
                 (int)mpx_abi_st);
        mpx_note(line);
    }

out:
    mpx_store_set_busy(0);
    mpx_display_deinit();
    {
        extern volatile unsigned char mpx_display_hold;
        if (strcmp(mpx_status_line, "present") != 0) {
            mpx_display_hold = 0;
        }
    }
    if (wasm_buf) {
        free(wasm_buf);
    }
    mpx_services_set_guest_inst(NULL);
    if (exec_env)
        wasm_exec_env_destroy(exec_env);
    if (inst)
        wasm_runtime_deinstantiate(inst);
    if (module)
        wasm_runtime_unload(module);
    wasm_runtime_destroy();
    mpx_services_deinit();
}

/* ---------------- 任务入口 ---------------- */

void mpx_task(void *p)
{
    (void)p;
    mpx_note("task alive, waiting 20s");
    /* 开机 20s 后再跑测试: 1) 给网页 OTA 留升级窗口(崩溃也能重刷);
     * 2) 避开 ~3.1s 处 USB CDC 重建窗口与 printf 的竞争(此前该窗口内
     *    打印触发 CPU1 写总线异常导致崩溃环) */
    os_time_dly(2000);
    mpx_note("delay done");
    /* 20s 节点若 OTA 升级正在进行: 等它结束再测试, 避免测试代码
     * (CPU1 崩溃→软复位)打断烧写留下半截 bank */
    while (net_update_request()) {
        static uint8_t wait_rounds;
        printf(MPX_TAG "OTA in progress, test deferred (%u)\n", ++wait_rounds);
        os_time_dly(200); /* 2s 后复查 */
    }
    printf(MPX_TAG "task start [nolto-O0-nods-256K], prev step=%u\n", MPX_SCRATCH[0]);
    mpx_note("smoke enter");
    mpx_mark(1);
    mpx_smoke_test();
    mpx_note("ready");
    for (;;) {
        char id[MPX_STORE_ID_MAX + 1];
        if (mpx_store_take_launch(id, sizeof(id))) {
            uint8_t *wasm = NULL;
            uint32_t wasm_size = 0;
            int st = mpx_store_load_wasm(id, &wasm, &wasm_size);
            if (st != 0) {
                char line[48];
                snprintf(line, sizeof(line), "load %d", st);
                mpx_note(line);
            } else {
                mpx_note(id);
                mpx_app_run(wasm, wasm_size);
            }
        }
        while (net_update_request()) {
            os_time_dly(100);
        }
        os_time_dly(20);
    }
}

#endif /* MINI_WASM_SUPPORT */
