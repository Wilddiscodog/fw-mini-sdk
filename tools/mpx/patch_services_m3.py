#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Wire raster + touch bridge into mpx_services.c (M3)."""
import io

P = "D:/fw-mini-sdk/app/mpx/mpx_services.c"
s = io.open(P, encoding="utf-8").read()
n = 0

# 1) includes + touch hook decl
old = '''#include "os/os_api.h"
#include "timer.h"

#include "mpx_display.h"'''
new = '''#include "os/os_api.h"
#include "timer.h"
#include "wasm_export.h"

#include "mpx_display.h"

#if (defined MINI_LCD_SUPPORT) && MINI_LCD_SUPPORT
extern void cst816_register_event(void (*cb)(u8 status, u16 x, u16 y));
#endif'''
assert old in s, "includes anchor"
s = s.replace(old, new); n += 1

# 2) guest instance binding before the event queue section
old = "/* ---------------- 事件队列 ---------------- */"
new = '''/* ---------------- Guest 线性内存访问(上传请求携带 Guest 偏移) ---------------- */

static wasm_module_inst_t mpx_guest_inst;

void mpx_services_set_guest_inst(void *inst)
{
    mpx_guest_inst = (wasm_module_inst_t)inst;
}

static const void *mpx_guest_ptr(uint32_t offset, uint32_t size)
{
    if (mpx_guest_inst == NULL || size == 0) {
        return NULL;
    }
    if (!wasm_runtime_validate_app_addr(mpx_guest_inst, offset, size)) {
        return NULL;
    }
    return wasm_runtime_addr_app_to_native(mpx_guest_inst, offset);
}

/* ---------------- 事件队列 ---------------- */'''
assert old in s, "queue anchor"
s = s.replace(old, new, 1); n += 1

# 3) touch bridge before the Random section
old = "/* ---------------- Random(xorshift32) ---------------- */"
new = '''/* ---------------- 触摸桥(CST816 轮询任务回调 -> INPUT 事件) ---------------- */

#if (defined MINI_LCD_SUPPORT) && MINI_LCD_SUPPORT
static void mpx_touch_cb(u8 status, u16 x, u16 y)
{
    uint16_t phase;
    if (status == 1) {
        phase = MICROPIXEL_TOUCH_DOWN;
    } else if (status == 2) {
        phase = MICROPIXEL_TOUCH_MOVE;
    } else {
        phase = MICROPIXEL_TOUCH_UP;
    }
    micropixel_touch_event_payload_t payload;
    memset(&payload, 0, sizeof(payload));
    if (x >= mpx_display_width()) {
        x = (u16)(mpx_display_width() - 1);
    }
    if (y >= mpx_display_height()) {
        y = (u16)(mpx_display_height() - 1);
    }
    payload.x = (int32_t)x;
    payload.y = (int32_t)y;
    payload.pressure_per_mille = 0;
    payload.phase = phase;
    mpx_event_post(MICROPIXEL_INPUT_EVENT_TOUCH, MICROPIXEL_SERVICE_INPUT, 0,
                   &payload, sizeof(payload));
}
#endif

/* ---------------- Random(xorshift32) ---------------- */'''
assert old in s, "random anchor"
s = s.replace(old, new, 1); n += 1

# 4) raster uploads in mpx_graphics_call (insert before the SURFACE_DESTROY branch)
old = '''    if (method_id == MICROPIXEL_GRAPHICS_METHOD_SURFACE_DESTROY) {'''
new = '''    if (method_id == MICROPIXEL_GRAPHICS_METHOD_RASTER_TEXTURE_UPLOAD) {
        micropixel_raster_texture_upload_request_t wire;
        if (mpx_read_request(request, request_size, &wire, sizeof(wire)) != MICROPIXEL_STATUS_OK ||
            wire.size != sizeof(wire)) {
            return MICROPIXEL_STATUS_INVALID_ARGUMENT;
        }
        const void *texels = mpx_guest_ptr(wire.pixels, wire.length);
        if (texels == NULL) {
            return MICROPIXEL_STATUS_INVALID_ARGUMENT;
        }
        return mpx_raster_texture_upload(wire.texture_slot, wire.width, wire.height,
                                         wire.layout, (const uint8_t *)texels, wire.length);
    }
    if (method_id == MICROPIXEL_GRAPHICS_METHOD_RASTER_PALETTE_UPLOAD) {
        micropixel_raster_palette_upload_request_t wire;
        if (mpx_read_request(request, request_size, &wire, sizeof(wire)) != MICROPIXEL_STATUS_OK ||
            wire.size != sizeof(wire)) {
            return MICROPIXEL_STATUS_INVALID_ARGUMENT;
        }
        const void *entries = mpx_guest_ptr(wire.entries, wire.length);
        if (entries == NULL) {
            return MICROPIXEL_STATUS_INVALID_ARGUMENT;
        }
        return mpx_raster_palette_upload(wire.palette_slot, wire.light_levels,
                                         (const uint8_t *)entries, wire.length);
    }
    if (method_id == MICROPIXEL_GRAPHICS_METHOD_SURFACE_DESTROY) {'''
assert old in s, "surface destroy anchor"
s = s.replace(old, new, 1); n += 1

# 5) submit -> raster executor
old = '''    if (channel_id != MICROPIXEL_GRAPHICS_CHANNEL_RASTER) {
        return MICROPIXEL_STATUS_UNSUPPORTED;
    }
    /* M3: 'MPRS' 光栅命令执行器;v1 先拒绝未实现的光栅提交 */
    (void)bytes;
    (void)length;
    return MICROPIXEL_STATUS_UNSUPPORTED;'''
new = '''    if (channel_id != MICROPIXEL_GRAPHICS_CHANNEL_RASTER) {
        return MICROPIXEL_STATUS_UNSUPPORTED;
    }
    return mpx_raster_submit(bytes, length);'''
assert old in s, "submit anchor"
s = s.replace(old, new, 1); n += 1

# 6) init/deinit: raster + touch registration
old = '''    memset(mpx_timers, 0, sizeof(mpx_timers));
    memset(mpx_kv_table, 0, sizeof(mpx_kv_table));
    mpx_timer_sys_timer_id = sys_timer_add(NULL, mpx_timer_tick_cb, 10);
}'''
new = '''    memset(mpx_timers, 0, sizeof(mpx_timers));
    memset(mpx_kv_table, 0, sizeof(mpx_kv_table));
    mpx_timer_sys_timer_id = sys_timer_add(NULL, mpx_timer_tick_cb, 10);
    mpx_raster_init();
#if (defined MINI_LCD_SUPPORT) && MINI_LCD_SUPPORT
    cst816_register_event(mpx_touch_cb);
#endif
}'''
assert old in s, "init anchor"
s = s.replace(old, new, 1); n += 1

old = '''void mpx_services_deinit(void)
{
    if (mpx_timer_sys_timer_id) {'''
new = '''void mpx_services_deinit(void)
{
    mpx_raster_deinit();
    mpx_guest_inst = NULL;
    if (mpx_timer_sys_timer_id) {'''
assert old in s, "deinit anchor"
s = s.replace(old, new, 1); n += 1

io.open(P, "w", encoding="utf-8").write(s)
print("patched", n, "sites")
