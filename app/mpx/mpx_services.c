/* app/mpx/mpx_services.c - MicroPixel ABI 服务层实现(见 mpx_services.h)
 *
 * v1 服务集:System / Timer / Storage(RAM KV) / Random / Devices / Graphics /
 * Input;Resource/Audio/Gpio/Haptics/Power/Sensors 按 ABI 语义返回 NOT_FOUND,
 * guest 侧 SDK 会拿到 UNSUPPORTED/NOT_FOUND 而不破坏加载。
 */
#include "mpx_services.h"

#include <string.h>
#include <stdlib.h>

#include "os/os_api.h"
#include "timer.h"
#include "wasm_export.h"

#include "mpx_display.h"

#if (defined MINI_LCD_SUPPORT) && MINI_LCD_SUPPORT
extern void cst816_register_event(void (*cb)(u8 status, u16 x, u16 y));
#endif

#define MPX_TAG "[mpx] "

/* ---------------- Guest 线性内存访问(上传请求携带 Guest 偏移) ---------------- */

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

/* ---------------- 事件队列 ---------------- */

#define MPX_EVENT_QUEUE_DEPTH 16

static OS_SEM mpx_event_sem;
static uint8_t mpx_event_sem_created;
static micropixel_event_t mpx_event_queue[MPX_EVENT_QUEUE_DEPTH];
static uint32_t mpx_event_head, mpx_event_count;
static uint32_t mpx_event_sequence;
static OS_MUTEX mpx_event_lock;
static uint8_t mpx_event_lock_created;

int32_t mpx_event_post(uint16_t event_id, uint32_t service_id, uint32_t source,
                       const void *payload, uint32_t payload_size)
{
    if (payload_size > sizeof(mpx_event_queue[0].payload)) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
    if (!mpx_event_lock_created) {
        return MICROPIXEL_STATUS_CLOSED;
    }
    os_mutex_pend(&mpx_event_lock, 0);
    if (mpx_event_count >= MPX_EVENT_QUEUE_DEPTH) {
        os_mutex_post(&mpx_event_lock);
        return MICROPIXEL_STATUS_RESOURCE_EXHAUSTED;
    }
    micropixel_event_t *slot = &mpx_event_queue[(mpx_event_head + mpx_event_count) % MPX_EVENT_QUEUE_DEPTH];
    memset(slot, 0, sizeof(*slot));
    slot->size = (uint16_t)sizeof(*slot);
    slot->event_id = event_id;
    slot->service_id = service_id;
    slot->source = source;
    slot->timestamp_us = mpx_app_time_now();
    slot->sequence = ++mpx_event_sequence;
    if (payload != NULL && payload_size != 0) {
        memcpy(slot->payload, payload, payload_size);
    }
    mpx_event_count++;
    os_mutex_post(&mpx_event_lock);
    if (mpx_event_sem_created) {
        os_sem_post(&mpx_event_sem);
    }
    return MICROPIXEL_STATUS_OK;
}

int32_t mpx_event_wait(micropixel_event_t *event_out, uint64_t timeout_us)
{
    if (!mpx_event_sem_created) {
        return MICROPIXEL_STATUS_CLOSED;
    }
    for (;;) {
        os_mutex_pend(&mpx_event_lock, 0);
        if (mpx_event_count != 0) {
            *event_out = mpx_event_queue[mpx_event_head];
            mpx_event_head = (mpx_event_head + 1) % MPX_EVENT_QUEUE_DEPTH;
            mpx_event_count--;
            os_mutex_post(&mpx_event_lock);
            return MICROPIXEL_STATUS_OK;
        }
        os_mutex_post(&mpx_event_lock);

        /* 10ms tick 粒度;timeout_us==0 表示无限等 */
        int ticks;
        if (timeout_us == 0) {
            ticks = 0; /* JieLi pend: 0 = 无限等待 */
        } else {
            ticks = (int)(timeout_us / 10000);
            if (ticks < 1) {
                ticks = 1;
            }
        }
        if (os_sem_pend(&mpx_event_sem, ticks) != 0) {
            return MICROPIXEL_STATUS_TIMEOUT;
        }
    }
}

/* ---------------- 应用时钟 ---------------- */

uint64_t mpx_app_time_now(void)
{
    /* 10ms tick;M4 换硬件 µs 定时器(动画需要更高精度) */
    return (uint64_t)os_time_get() * 10000;
}

/* ---------------- 服务注册表 ---------------- */

#define MPX_MAX_SERVICES 16

typedef struct mpx_service_desc {
    uint32_t service_id;
    uint16_t interface_major;
    uint16_t interface_minor;
    uint32_t flags;
    uint64_t capabilities;
    uint32_t max_request_bytes;
    uint32_t max_response_bytes;
    uint32_t max_submit_bytes;
} mpx_service_desc_t;

static const mpx_service_desc_t mpx_service_table[] = {
    /* Timer: CREATE(空请求)/START/CANCEL/DESTROY, 到期投递事件 */
    { MICROPIXEL_SERVICE_TIMER, 1, 0,
      MICROPIXEL_SERVICE_FLAG_CALL | MICROPIXEL_SERVICE_FLAG_EVENTS, 0,
      sizeof(micropixel_timer_start_request_t), sizeof(micropixel_handle_response_t), 0 },
    /* Storage: GET/SET/REMOVE */
    { MICROPIXEL_SERVICE_STORAGE, 1, 0,
      MICROPIXEL_SERVICE_FLAG_CALL, 0,
      sizeof(micropixel_storage_set_request_t) + MICROPIXEL_STORAGE_MAX_KEY_BYTES +
          MICROPIXEL_STORAGE_MAX_VALUE_BYTES,
      MICROPIXEL_STORAGE_MAX_VALUE_BYTES, 0 },
    /* Random: GET_U32 */
    { MICROPIXEL_SERVICE_RANDOM, 1, 0,
      MICROPIXEL_SERVICE_FLAG_CALL, 0, 0, sizeof(micropixel_random_u32_response_t), 0 },
    /* System: GET_LOCALE / GET_LAUNCH_ARGUMENTS */
    { MICROPIXEL_SERVICE_SYSTEM, 1, 0,
      MICROPIXEL_SERVICE_FLAG_CALL, 0, 0,
      sizeof(micropixel_system_launch_arguments_response_t), 0 },
    /* Devices: LIST/GET_INFO(本板 v1 只报触摸屏,见 mpx_devices_call) */
    { MICROPIXEL_SERVICE_DEVICES, 1, 0,
      MICROPIXEL_SERVICE_FLAG_CALL, 0,
      sizeof(micropixel_devices_list_request_t), sizeof(micropixel_devices_list_response_t), 0 },
    /* Graphics: Direct Surface + Raster 通道(M3 起全量) */
    { MICROPIXEL_SERVICE_GRAPHICS, MICROPIXEL_GRAPHICS_INTERFACE_MAJOR, MICROPIXEL_GRAPHICS_INTERFACE_MINOR,
      MICROPIXEL_SERVICE_FLAG_CALL | MICROPIXEL_SERVICE_FLAG_SUBMIT | MICROPIXEL_SERVICE_FLAG_EVENTS,
      MICROPIXEL_GRAPHICS_CAP_RASTER,
      sizeof(micropixel_surface_present_request_t), sizeof(micropixel_graphics_info_t),
      32 * 1024 },
    /* Input: GET_INFO + 触摸事件 */
    { MICROPIXEL_SERVICE_INPUT, 1, 0,
      MICROPIXEL_SERVICE_FLAG_CALL | MICROPIXEL_SERVICE_FLAG_EVENTS, 0, 0,
      sizeof(micropixel_input_info_t), 0 },
};

#define MPX_SERVICE_TABLE_COUNT (sizeof(mpx_service_table) / sizeof(mpx_service_table[0]))

/* ---------------- Timer(8 槽, 10ms sys_timer 驱动) ---------------- */

#define MPX_TIMER_COUNT 8

typedef struct mpx_timer {
    uint8_t used;
    uint8_t armed;
    uint64_t delay_left_us;
    uint64_t period_us;
} mpx_timer_t;

static mpx_timer_t mpx_timers[MPX_TIMER_COUNT];
static u16 mpx_timer_sys_timer_id = 0;

static void mpx_timer_tick_cb(void *priv)
{
    uint32_t i;
    (void)priv;
    for (i = 0; i < MPX_TIMER_COUNT; i++) {
        mpx_timer_t *t = &mpx_timers[i];
        if (!t->used || !t->armed) {
            continue;
        }
        if (t->delay_left_us > 10000) {
            t->delay_left_us -= 10000;
            continue;
        }
        micropixel_timer_event_payload_t payload;
        memset(&payload, 0, sizeof(payload));
        payload.elapsed_us = t->period_us;
        payload.missed_count = 0;
        if (t->period_us == 0) {
            t->armed = 0; /* one-shot */
        } else {
            t->delay_left_us = t->period_us;
        }
        mpx_event_post(MICROPIXEL_TIMER_EVENT_EXPIRED, MICROPIXEL_SERVICE_TIMER, i + 1,
                       &payload, sizeof(payload));
    }
}

/* ---------------- Storage(RAM KV,M4 换 flash VM 区) ---------------- */

#define MPX_KV_SLOTS 8

typedef struct mpx_kv {
    uint8_t used;
    uint8_t key[MICROPIXEL_STORAGE_MAX_KEY_BYTES + 1];
    uint8_t *value;
    uint32_t value_length;
} mpx_kv_t;

static mpx_kv_t mpx_kv_table[MPX_KV_SLOTS];

static mpx_kv_t *mpx_kv_find(const char *key, uint32_t key_length)
{
    uint32_t i;
    for (i = 0; i < MPX_KV_SLOTS; i++) {
        if (mpx_kv_table[i].used && strlen((char *)mpx_kv_table[i].key) == key_length &&
            memcmp(mpx_kv_table[i].key, key, key_length) == 0) {
            return &mpx_kv_table[i];
        }
    }
    return NULL;
}

/* ---------------- 触摸桥(CST816 轮询任务回调 -> INPUT 事件) ---------------- */

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

/* ---------------- Random(xorshift32) ---------------- */

static uint32_t mpx_random_state = 0x6D2B79F5U;

static uint32_t mpx_random_next(void)
{
    uint32_t x = mpx_random_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    mpx_random_state = x;
    return x;
}

/* ---------------- 工具(对齐 service_endpoints.cpp 的读写语义) ---------------- */

static int32_t mpx_write_fixed(const void *value, uint32_t size, uint8_t *response,
                               uint32_t response_capacity, uint32_t *response_size_out)
{
    if (response == NULL || response_capacity < size) {
        return MICROPIXEL_STATUS_BUFFER_TOO_SMALL;
    }
    memcpy(response, value, size);
    *response_size_out = size;
    return MICROPIXEL_STATUS_OK;
}

static int32_t mpx_read_request(const uint8_t *request, uint32_t request_size, void *wire,
                                uint32_t wire_size)
{
    if (request == NULL || request_size != wire_size) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
    memcpy(wire, request, wire_size);
    return MICROPIXEL_STATUS_OK;
}

/* ---------------- 各服务 Call ---------------- */

static int32_t mpx_system_call(uint32_t method_id, const uint8_t *request, uint32_t request_size,
                               uint8_t *response, uint32_t response_capacity, uint32_t *response_size_out)
{
    if (method_id == MICROPIXEL_SYSTEM_METHOD_GET_LOCALE) {
        micropixel_system_locale_response_t locale;
        memset(&locale, 0, sizeof(locale));
        locale.size = (uint16_t)sizeof(locale);
        locale.tag_length = 2;
        memcpy(locale.tag, "en", 2);
        return mpx_write_fixed(&locale, sizeof(locale), response, response_capacity, response_size_out);
    }
    if (method_id == MICROPIXEL_SYSTEM_METHOD_GET_LAUNCH_ARGUMENTS) {
        micropixel_system_launch_arguments_response_t args;
        memset(&args, 0, sizeof(args));
        args.size = (uint16_t)sizeof(args);
        return mpx_write_fixed(&args, sizeof(args), response, response_capacity, response_size_out);
    }
    (void)request;
    (void)request_size;
    return MICROPIXEL_STATUS_UNSUPPORTED;
}

static int32_t mpx_timer_call(uint32_t method_id, const uint8_t *request, uint32_t request_size,
                              uint8_t *response, uint32_t response_capacity, uint32_t *response_size_out)
{
    if (method_id == MICROPIXEL_TIMER_METHOD_CREATE) {
        uint32_t i;
        if (request_size != 0) {
            return MICROPIXEL_STATUS_INVALID_ARGUMENT;
        }
        for (i = 0; i < MPX_TIMER_COUNT; i++) {
            if (!mpx_timers[i].used) {
                micropixel_handle_response_t resp;
                memset(&resp, 0, sizeof(resp));
                resp.size = (uint16_t)sizeof(resp);
                resp.handle = i + 1;
                mpx_timers[i].used = 1;
                mpx_timers[i].armed = 0;
                return mpx_write_fixed(&resp, sizeof(resp), response, response_capacity,
                                       response_size_out);
            }
        }
        return MICROPIXEL_STATUS_RESOURCE_EXHAUSTED;
    }
    if (method_id == MICROPIXEL_TIMER_METHOD_START) {
        micropixel_timer_start_request_t wire;
        if (mpx_read_request(request, request_size, &wire, sizeof(wire)) != MICROPIXEL_STATUS_OK ||
            wire.timer_handle == 0 || wire.timer_handle > MPX_TIMER_COUNT ||
            !mpx_timers[wire.timer_handle - 1].used) {
            return MICROPIXEL_STATUS_INVALID_ARGUMENT;
        }
        mpx_timers[wire.timer_handle - 1].armed = 1;
        mpx_timers[wire.timer_handle - 1].delay_left_us = wire.initial_delay_us;
        mpx_timers[wire.timer_handle - 1].period_us = wire.period_us;
        return MICROPIXEL_STATUS_OK;
    }
    if (method_id == MICROPIXEL_TIMER_METHOD_CANCEL || method_id == MICROPIXEL_TIMER_METHOD_DESTROY) {
        micropixel_handle_request_t wire;
        if (mpx_read_request(request, request_size, &wire, sizeof(wire)) != MICROPIXEL_STATUS_OK ||
            wire.handle == 0 || wire.handle > MPX_TIMER_COUNT ||
            !mpx_timers[wire.handle - 1].used) {
            return MICROPIXEL_STATUS_INVALID_ARGUMENT;
        }
        mpx_timers[wire.handle - 1].armed = 0;
        if (method_id == MICROPIXEL_TIMER_METHOD_DESTROY) {
            mpx_timers[wire.handle - 1].used = 0;
        }
        return MICROPIXEL_STATUS_OK;
    }
    return MICROPIXEL_STATUS_UNSUPPORTED;
}

static int32_t mpx_storage_call(uint32_t method_id, const uint8_t *request, uint32_t request_size,
                                uint8_t *response, uint32_t response_capacity, uint32_t *response_size_out)
{
    if (method_id == MICROPIXEL_STORAGE_METHOD_SET) {
        micropixel_storage_set_request_t wire;
        if (request == NULL || request_size < sizeof(wire)) {
            return MICROPIXEL_STATUS_INVALID_ARGUMENT;
        }
        memcpy(&wire, request, sizeof(wire));
        if (wire.key_length == 0 || wire.key_length > MICROPIXEL_STORAGE_MAX_KEY_BYTES ||
            wire.value_length > MICROPIXEL_STORAGE_MAX_VALUE_BYTES ||
            wire.size != sizeof(wire) + wire.key_length + wire.value_length ||
            wire.size != request_size) {
            return MICROPIXEL_STATUS_INVALID_ARGUMENT;
        }
        const char *key = (const char *)(request + sizeof(wire));
        const uint8_t *value = request + sizeof(wire) + wire.key_length;
        mpx_kv_t *slot = mpx_kv_find(key, wire.key_length);
        if (slot == NULL) {
            uint32_t i;
            for (i = 0; i < MPX_KV_SLOTS; i++) {
                if (!mpx_kv_table[i].used) {
                    slot = &mpx_kv_table[i];
                    slot->used = 1;
                    break;
                }
            }
            if (slot == NULL) {
                return MICROPIXEL_STATUS_RESOURCE_EXHAUSTED;
            }
            memcpy(slot->key, key, wire.key_length);
            slot->key[wire.key_length] = 0;
        }
        if (slot->value_length != wire.value_length) {
            uint8_t *buffer = (uint8_t *)malloc(wire.value_length != 0 ? wire.value_length : 1);
            if (buffer == NULL) {
                return MICROPIXEL_STATUS_RESOURCE_EXHAUSTED;
            }
            free(slot->value);
            slot->value = buffer;
        }
        memcpy(slot->value, value, wire.value_length);
        slot->value_length = wire.value_length;
        return MICROPIXEL_STATUS_OK;
    }

    micropixel_storage_key_request_t wire;
    if (request == NULL || request_size < sizeof(wire)) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
    memcpy(&wire, request, sizeof(wire));
    if (wire.key_length == 0 || wire.key_length > MICROPIXEL_STORAGE_MAX_KEY_BYTES ||
        wire.size != sizeof(wire) + wire.key_length || wire.size != request_size) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
    const char *key = (const char *)(request + sizeof(wire));
    mpx_kv_t *slot = mpx_kv_find(key, wire.key_length);
    if (method_id == MICROPIXEL_STORAGE_METHOD_GET) {
        if (slot == NULL) {
            return MICROPIXEL_STATUS_NOT_FOUND;
        }
        if (response == NULL || response_capacity < slot->value_length) {
            *response_size_out = slot->value_length;
            return MICROPIXEL_STATUS_BUFFER_TOO_SMALL;
        }
        memcpy(response, slot->value, slot->value_length);
        *response_size_out = slot->value_length;
        return MICROPIXEL_STATUS_OK;
    }
    if (method_id == MICROPIXEL_STORAGE_METHOD_REMOVE) {
        if (slot == NULL) {
            return MICROPIXEL_STATUS_NOT_FOUND;
        }
        free(slot->value);
        slot->value = NULL;
        slot->value_length = 0;
        slot->used = 0;
        return MICROPIXEL_STATUS_OK;
    }
    return MICROPIXEL_STATUS_UNSUPPORTED;
}

static int32_t mpx_random_call(uint32_t method_id, const uint8_t *request, uint32_t request_size,
                               uint8_t *response, uint32_t response_capacity, uint32_t *response_size_out)
{
    if (method_id == MICROPIXEL_RANDOM_METHOD_GET_U32) {
        micropixel_random_u32_response_t resp;
        memset(&resp, 0, sizeof(resp));
        resp.size = (uint16_t)sizeof(resp);
        resp.value = mpx_random_next();
        (void)request;
        (void)request_size;
        return mpx_write_fixed(&resp, sizeof(resp), response, response_capacity, response_size_out);
    }
    return MICROPIXEL_STATUS_UNSUPPORTED;
}

static int32_t mpx_devices_call(uint32_t method_id, const uint8_t *request, uint32_t request_size,
                                uint8_t *response, uint32_t response_capacity, uint32_t *response_size_out)
{
    if (method_id == MICROPIXEL_DEVICES_METHOD_LIST) {
        micropixel_devices_list_response_t resp;
        memset(&resp, 0, sizeof(resp));
        resp.size = (uint16_t)sizeof(resp);
        resp.count = 0;
        resp.generation = 1;
        resp.total_count = 0;
        return mpx_write_fixed(&resp, sizeof(resp), response, response_capacity, response_size_out);
    }
    (void)request;
    (void)request_size;
    (void)response;
    (void)response_capacity;
    (void)response_size_out;
    return MICROPIXEL_STATUS_NOT_FOUND;
}

static int32_t mpx_input_call(uint32_t method_id, const uint8_t *request, uint32_t request_size,
                              uint8_t *response, uint32_t response_capacity, uint32_t *response_size_out)
{
    if (method_id == MICROPIXEL_INPUT_METHOD_GET_INFO) {
        micropixel_input_info_t info;
        memset(&info, 0, sizeof(info));
        info.size = (uint16_t)sizeof(info);
        info.max_touch_points = 1;
        info.capabilities = 0;
        info.logical_width = mpx_display_width();
        info.logical_height = mpx_display_height();
        return mpx_write_fixed(&info, sizeof(info), response, response_capacity, response_size_out);
    }
    (void)request;
    (void)request_size;
    return MICROPIXEL_STATUS_UNSUPPORTED;
}

static int32_t mpx_graphics_call(uint32_t method_id, const uint8_t *request, uint32_t request_size,
                                 uint8_t *response, uint32_t response_capacity, uint32_t *response_size_out)
{
    if (method_id == MICROPIXEL_GRAPHICS_METHOD_GET_INFO) {
        micropixel_graphics_info_t info;
        int32_t status = mpx_display_get_info(&info);
        if (status != MICROPIXEL_STATUS_OK) {
            return status;
        }
        return mpx_write_fixed(&info, sizeof(info), response, response_capacity, response_size_out);
    }
    if (method_id == MICROPIXEL_GRAPHICS_METHOD_SURFACE_CREATE) {
        micropixel_surface_create_request_t wire;
        uint32_t handle;
        micropixel_surface_create_response_t resp;
        if (mpx_read_request(request, request_size, &wire, sizeof(wire)) != MICROPIXEL_STATUS_OK ||
            wire.flags != 0 || wire.buffer_count < 1 || wire.buffer_count > 2 ||
            wire.pixel_format != MICROPIXEL_PIXEL_FORMAT_RGB565 || wire.width == 0 || wire.height == 0) {
            return MICROPIXEL_STATUS_INVALID_ARGUMENT;
        }
        int32_t status = mpx_display_surface_create(wire.width, wire.height, wire.pixel_format,
                                                    wire.buffer_count, &handle);
        if (status != MICROPIXEL_STATUS_OK) {
            return status;
        }
        memset(&resp, 0, sizeof(resp));
        resp.size = (uint16_t)sizeof(resp);
        resp.surface_handle = handle;
        resp.native_pixel_format = MICROPIXEL_PIXEL_FORMAT_RGB565;
        resp.native_flags = 0;
        resp.max_full_frame_fps = 30;
        return mpx_write_fixed(&resp, sizeof(resp), response, response_capacity, response_size_out);
    }
    if (method_id == MICROPIXEL_GRAPHICS_METHOD_SURFACE_PRESENT) {
        micropixel_surface_present_request_t wire;
        if (mpx_read_request(request, request_size, &wire, sizeof(wire)) != MICROPIXEL_STATUS_OK ||
            wire.flags != 0 || wire.pixels != 0 || wire.length != 0) {
            return MICROPIXEL_STATUS_INVALID_ARGUMENT;
        }
        int32_t status = mpx_display_surface_present(wire.surface_handle, wire.buffer_index);
        if (status != MICROPIXEL_STATUS_OK) {
            return status;
        }
        /* 同步上屏完成,缓冲立即释放:投递 SURFACE_RELEASED */
        micropixel_surface_event_payload_t payload;
        memset(&payload, 0, sizeof(payload));
        payload.surface_handle = wire.surface_handle;
        payload.buffer_index = wire.buffer_index;
        payload.timestamp_us = mpx_app_time_now();
        mpx_event_post(MICROPIXEL_GRAPHICS_EVENT_SURFACE_RELEASED, MICROPIXEL_SERVICE_GRAPHICS,
                       wire.surface_handle, &payload, sizeof(payload));
        return MICROPIXEL_STATUS_OK;
    }
    if (method_id == MICROPIXEL_GRAPHICS_METHOD_RASTER_TEXTURE_UPLOAD) {
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
    if (method_id == MICROPIXEL_GRAPHICS_METHOD_SURFACE_DESTROY) {
        micropixel_handle_request_t wire;
        if (mpx_read_request(request, request_size, &wire, sizeof(wire)) != MICROPIXEL_STATUS_OK) {
            return MICROPIXEL_STATUS_INVALID_ARGUMENT;
        }
        mpx_display_surface_destroy(wire.handle);
        return MICROPIXEL_STATUS_OK;
    }
    return MICROPIXEL_STATUS_UNSUPPORTED;
}

/* ---------------- Submit(通道) ---------------- */

static int32_t mpx_graphics_submit(uint32_t channel_id, const uint8_t *bytes, uint32_t length)
{
    if (channel_id != MICROPIXEL_GRAPHICS_CHANNEL_RASTER) {
        return MICROPIXEL_STATUS_UNSUPPORTED;
    }
    return mpx_raster_submit(bytes, length);
}

/* ---------------- 注册表入口 ---------------- */

int32_t mpx_service_open(uint32_t service_id, uint32_t required_interface_version,
                         micropixel_service_info_t *info_out, uint32_t info_capacity)
{
    uint32_t i;
    if (info_capacity < sizeof(micropixel_service_info_t)) {
        return MICROPIXEL_STATUS_BUFFER_TOO_SMALL;
    }
    uint16_t required_major = (uint16_t)(required_interface_version >> 16);
    uint16_t required_minor = (uint16_t)required_interface_version;
    for (i = 0; i < MPX_SERVICE_TABLE_COUNT; i++) {
        const mpx_service_desc_t *d = &mpx_service_table[i];
        if (d->service_id != service_id) {
            continue;
        }
        if (d->interface_major != required_major || d->interface_minor < required_minor) {
            return MICROPIXEL_STATUS_VERSION_MISMATCH;
        }
        memset(info_out, 0, sizeof(*info_out));
        info_out->size = (uint16_t)sizeof(*info_out);
        info_out->service_id = d->service_id;
        info_out->service_handle = i + 1;
        info_out->interface_major = d->interface_major;
        info_out->interface_minor = d->interface_minor;
        info_out->flags = d->flags;
        info_out->capabilities = d->capabilities;
        info_out->max_request_bytes = d->max_request_bytes;
        info_out->max_response_bytes = d->max_response_bytes;
        info_out->max_submit_bytes = d->max_submit_bytes;
        return MICROPIXEL_STATUS_OK;
    }
    return MICROPIXEL_STATUS_NOT_FOUND;
}

int32_t mpx_service_call(uint32_t service_handle, uint32_t method_id, const uint8_t *request,
                         uint32_t request_size, uint8_t *response, uint32_t response_capacity,
                         uint32_t *response_size_out)
{
    *response_size_out = 0;
    if (service_handle == 0 || service_handle > MPX_SERVICE_TABLE_COUNT) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
    const mpx_service_desc_t *d = &mpx_service_table[service_handle - 1];
    if ((d->flags & MICROPIXEL_SERVICE_FLAG_CALL) == 0) {
        return MICROPIXEL_STATUS_UNSUPPORTED;
    }
    if ((request == NULL && request_size != 0) || (response == NULL && response_capacity != 0) ||
        request_size > d->max_request_bytes) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
    switch (d->service_id) {
    case MICROPIXEL_SERVICE_TIMER:
        return mpx_timer_call(method_id, request, request_size, response, response_capacity,
                              response_size_out);
    case MICROPIXEL_SERVICE_STORAGE:
        return mpx_storage_call(method_id, request, request_size, response, response_capacity,
                                response_size_out);
    case MICROPIXEL_SERVICE_RANDOM:
        return mpx_random_call(method_id, request, request_size, response, response_capacity,
                               response_size_out);
    case MICROPIXEL_SERVICE_SYSTEM:
        return mpx_system_call(method_id, request, request_size, response, response_capacity,
                               response_size_out);
    case MICROPIXEL_SERVICE_DEVICES:
        return mpx_devices_call(method_id, request, request_size, response, response_capacity,
                                response_size_out);
    case MICROPIXEL_SERVICE_GRAPHICS:
        return mpx_graphics_call(method_id, request, request_size, response, response_capacity,
                                 response_size_out);
    case MICROPIXEL_SERVICE_INPUT:
        return mpx_input_call(method_id, request, request_size, response, response_capacity,
                              response_size_out);
    default:
        return MICROPIXEL_STATUS_NOT_FOUND;
    }
}

int32_t mpx_service_submit(uint32_t service_handle, uint32_t channel_id, const uint8_t *bytes,
                           uint32_t length)
{
    if (service_handle == 0 || service_handle > MPX_SERVICE_TABLE_COUNT) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
    const mpx_service_desc_t *d = &mpx_service_table[service_handle - 1];
    if ((d->flags & MICROPIXEL_SERVICE_FLAG_SUBMIT) == 0) {
        return MICROPIXEL_STATUS_UNSUPPORTED;
    }
    if (bytes == NULL || length == 0 || length > d->max_submit_bytes) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
    if (d->service_id == MICROPIXEL_SERVICE_GRAPHICS) {
        return mpx_graphics_submit(channel_id, bytes, length);
    }
    return MICROPIXEL_STATUS_UNSUPPORTED;
}

void mpx_services_init(void)
{
    if (os_mutex_create(&mpx_event_lock) == 0) {
        mpx_event_lock_created = 1;
    }
    if (os_sem_create(&mpx_event_sem, 0) == 0) {
        mpx_event_sem_created = 1;
    }
    mpx_event_head = 0;
    mpx_event_count = 0;
    mpx_event_sequence = 0;
    mpx_random_state ^= os_time_get();
    if (mpx_random_state == 0) {
        mpx_random_state = 0x6D2B79F5U;
    }
    memset(mpx_timers, 0, sizeof(mpx_timers));
    memset(mpx_kv_table, 0, sizeof(mpx_kv_table));
    mpx_timer_sys_timer_id = sys_timer_add(NULL, mpx_timer_tick_cb, 10);
    mpx_raster_init();
#if (defined MINI_LCD_SUPPORT) && MINI_LCD_SUPPORT
    cst816_register_event(mpx_touch_cb);
#endif
}

void mpx_services_deinit(void)
{
    mpx_raster_deinit();
    mpx_guest_inst = NULL;
    if (mpx_timer_sys_timer_id) {
        sys_timer_del(mpx_timer_sys_timer_id);
        mpx_timer_sys_timer_id = 0;
    }
    mpx_event_sem_created = 0;
    mpx_event_lock_created = 0;
}
