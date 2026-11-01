/* app/mpx/mpx_raster.c - 见 mpx_raster.h
 *
 * 面板(JD9853)字节序 = 大端,canonical RGB565(小端 u16)写入缓冲前字节
 * 交换;调色板存储即面板序,SPRITE/RECT 落像素时直接按字节写。
 */
#include "mpx_raster.h"

#include <stdlib.h>
#include <string.h>

#include "micropixel_abi.h"
#include "mpx_display.h"

#define MPX_TAG "[mpx] "

#define MPX_RT_SLOTS 8
#define MPX_RP_SLOTS 8
#define MPX_RP_MAX_LIGHTS 8 /* ABI 上限 32, 本板按 8 档省内存 */
#define MPX_MAX_TEX_DIM 4096

typedef struct mpx_rt {
    uint8_t used;
    uint16_t width;
    uint16_t height;
    uint16_t layout; /* 1=COLUMN_MAJOR, 2=ROW_MAJOR */
    uint8_t *texels; /* width*height INDEX8 */
} mpx_rt_t;

typedef struct mpx_rp {
    uint8_t used;
    uint16_t light_levels;
    uint16_t *entries; /* light_levels*256, 面板字节序 */
} mpx_rp_t;

static mpx_rt_t mpx_rt[MPX_RT_SLOTS];
static mpx_rp_t mpx_rp[MPX_RP_SLOTS];

void mpx_raster_init(void)
{
    memset(mpx_rt, 0, sizeof(mpx_rt));
    memset(mpx_rp, 0, sizeof(mpx_rp));
}

void mpx_raster_deinit(void)
{
    uint32_t i;
    for (i = 0; i < MPX_RT_SLOTS; i++) {
        free(mpx_rt[i].texels);
        mpx_rt[i].texels = NULL;
        mpx_rt[i].used = 0;
    }
    for (i = 0; i < MPX_RP_SLOTS; i++) {
        free(mpx_rp[i].entries);
        mpx_rp[i].entries = NULL;
        mpx_rp[i].used = 0;
    }
}

int32_t mpx_raster_texture_upload(uint8_t slot, uint16_t width, uint16_t height,
                                  uint16_t layout, const uint8_t *texels, uint32_t length)
{
    if (slot >= MPX_RT_SLOTS || width == 0 || height == 0 || width > MPX_MAX_TEX_DIM ||
        height > MPX_MAX_TEX_DIM || length != (uint32_t)width * height ||
        (layout != MICROPIXEL_RASTER_LAYOUT_COLUMN_MAJOR && layout != MICROPIXEL_RASTER_LAYOUT_ROW_MAJOR)) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
    mpx_rt_t *t = &mpx_rt[slot];
    uint8_t *buffer = (uint8_t *)malloc(length);
    if (buffer == NULL) {
        return MICROPIXEL_STATUS_RESOURCE_EXHAUSTED;
    }
    memcpy(buffer, texels, length);
    free(t->texels);
    t->texels = buffer;
    t->width = width;
    t->height = height;
    t->layout = layout;
    t->used = 1;
    return MICROPIXEL_STATUS_OK;
}

int32_t mpx_raster_palette_upload(uint8_t slot, uint16_t light_levels,
                                  const uint8_t *entries, uint32_t length)
{
    if (slot >= MPX_RP_SLOTS || light_levels == 0 || light_levels > MPX_RP_MAX_LIGHTS ||
        length != (uint32_t)light_levels * 256U * 2U) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
    mpx_rp_t *p = &mpx_rp[slot];
    uint16_t *buffer = (uint16_t *)malloc(length);
    if (buffer == NULL) {
        return MICROPIXEL_STATUS_RESOURCE_EXHAUSTED;
    }
    /* canonical(LE u16) -> 面板字节序(BE) */
    for (uint32_t i = 0; i < length / 2; i++) {
        uint16_t canonical = (uint16_t)(entries[i * 2] | (entries[i * 2 + 1] << 8));
        buffer[i] = (uint16_t)((canonical << 8) | (canonical >> 8));
    }
    free(p->entries);
    p->entries = buffer;
    p->light_levels = light_levels;
    p->used = 1;
    return MICROPIXEL_STATUS_OK;
}

/* ---------------- 执行 ---------------- */

typedef struct mpx_target {
    uint8_t *data;
    uint32_t width;
    uint32_t height;
    uint32_t pitch; /* 字节 */
} mpx_target_t;

static inline uint16_t mpx_canon_to_panel(uint16_t canonical)
{
    return (uint16_t)((canonical << 8) | (canonical >> 8));
}

static inline uint16_t mpx_panel_to_canon(uint16_t panel)
{
    return (uint16_t)((panel << 8) | (panel >> 8));
}

static inline void mpx_put(uint8_t *row, uint32_t x, uint16_t panel_value)
{
    row[x * 2] = (uint8_t)(panel_value >> 8);
    row[x * 2 + 1] = (uint8_t)(panel_value & 0xff);
}

static inline uint16_t mpx_get(const uint8_t *row, uint32_t x)
{
    return (uint16_t)((row[x * 2] << 8) | row[x * 2 + 1]);
}

static const uint16_t *mpx_palette_entry(uint8_t palette_slot, uint8_t light_level)
{
    if (palette_slot >= MPX_RP_SLOTS || !mpx_rp[palette_slot].used ||
        light_level >= mpx_rp[palette_slot].light_levels) {
        return NULL;
    }
    return mpx_rp[palette_slot].entries + (uint32_t)light_level * 256;
}

static uint8_t mpx_texel(const mpx_rt_t *t, uint16_t u, uint16_t v)
{
    if (t->layout == MICROPIXEL_RASTER_LAYOUT_COLUMN_MAJOR) {
        return t->texels[(uint32_t)u * t->height + v];
    }
    return t->texels[(uint32_t)v * t->width + u];
}

static int32_t mpx_exec_rect(const uint8_t *rec, const mpx_target_t *target)
{
    micropixel_raster_rect_t r;
    memcpy(&r, rec, sizeof(r));
    if (r.opacity == 0) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
    int32_t x0 = r.x, y0 = r.y;
    int32_t x1 = r.x + (int32_t)r.width - 1, y1 = r.y + (int32_t)r.height - 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= (int32_t)target->width) x1 = (int32_t)target->width - 1;
    if (y1 >= (int32_t)target->height) y1 = (int32_t)target->height - 1;
    if (x0 > x1 || y0 > y1) {
        return MICROPIXEL_STATUS_OK; /* 完全在外: 合法空操作 */
    }
    uint16_t panel = mpx_canon_to_panel(r.color);
    for (int32_t y = y0; y <= y1; y++) {
        uint8_t *row = target->data + (uint32_t)y * target->pitch;
        if (r.opacity == 255) {
            for (int32_t x = x0; x <= x1; x++) {
                mpx_put(row, (uint32_t)x, panel);
            }
        } else {
            for (int32_t x = x0; x <= x1; x++) {
                uint16_t dst = mpx_panel_to_canon(mpx_get(row, (uint32_t)x));
                uint32_t dr = dst >> 11, dg = (dst >> 5) & 0x3f, db = dst & 0x1f;
                uint32_t sr = r.color >> 11, sg = (r.color >> 5) & 0x3f, sb = r.color & 0x1f;
                dr = (dr * r.opacity + sr * (255 - r.opacity)) / 255;
                dg = (dg * r.opacity + sg * (255 - r.opacity)) / 255;
                db = (db * r.opacity + sb * (255 - r.opacity)) / 255;
                mpx_put(row, (uint32_t)x,
                        mpx_canon_to_panel((uint16_t)((dr << 11) | (dg << 5) | db)));
            }
        }
    }
    return MICROPIXEL_STATUS_OK;
}

static int32_t mpx_exec_sprite(const uint8_t *rec, const mpx_target_t *target)
{
    micropixel_raster_sprite_t s;
    memcpy(&s, rec, sizeof(s));
    const mpx_rt_t *t = &mpx_rt[s.texture_slot];
    const uint16_t *pal = NULL;
    if (t->used) {
        pal = mpx_palette_entry(s.palette_slot, s.light_level);
        if (pal == NULL) {
            return MICROPIXEL_STATUS_INVALID_ARGUMENT;
        }
    }
    uint16_t panel_color = mpx_canon_to_panel(s.color);
    for (uint32_t dy = 0; dy < s.height; dy++) {
        int32_t ty = (int32_t)s.y + (int32_t)dy;
        if (ty < 0 || ty >= (int32_t)target->height) {
            continue;
        }
        uint32_t sy = s.source_y + (uint16_t)((dy * s.source_height) / s.height);
        uint8_t *row = target->data + (uint32_t)ty * target->pitch;
        for (uint32_t dx = 0; dx < s.width; dx++) {
            int32_t tx = (int32_t)s.x + (int32_t)dx;
            if (tx < 0 || tx >= (int32_t)target->width) {
                continue;
            }
            uint32_t sx = s.source_x + (uint16_t)((dx * s.source_width) / s.width);
            uint8_t texel = mpx_texel(t, (uint16_t)sx, (uint16_t)sy);
            if ((s.flags & MICROPIXEL_RASTER_SPRITE_TRANSPARENT_INDEX0) && texel == 0) {
                continue;
            }
            uint16_t panel;
            if (s.flags & MICROPIXEL_RASTER_SPRITE_SOLID_COLOR) {
                panel = panel_color;
            } else {
                panel = pal[texel];
            }
            if (s.flags & MICROPIXEL_RASTER_SPRITE_ADDITIVE) {
                uint16_t dst = mpx_panel_to_canon(mpx_get(row, (uint32_t)tx));
                uint16_t src = mpx_panel_to_canon(panel);
                uint32_t r = (dst >> 11) + (src >> 11);
                uint32_t g = ((dst >> 5) & 0x3f) + ((src >> 5) & 0x3f);
                uint32_t b = (dst & 0x1f) + (src & 0x1f);
                if (r > 31) r = 31;
                if (g > 63) g = 63;
                if (b > 31) b = 31;
                panel = mpx_canon_to_panel((uint16_t)((r << 11) | (g << 5) | b));
            }
            mpx_put(row, (uint32_t)tx, panel);
        }
    }
    return MICROPIXEL_STATUS_OK;
}

/* 第一遍:仅校验,不写像素 */
static int32_t mpx_validate_record(const uint8_t *rec, uint32_t rec_size)
{
    uint8_t type = rec[0];
    switch (type) {
    case MICROPIXEL_RASTER_RECORD_RECT:
        if (rec_size != sizeof(micropixel_raster_rect_t)) {
            return MICROPIXEL_STATUS_INVALID_ARGUMENT;
        }
        if (rec[2] == 0) { /* opacity 0 拒绝 */
            return MICROPIXEL_STATUS_INVALID_ARGUMENT;
        }
        return MICROPIXEL_STATUS_OK;
    case MICROPIXEL_RASTER_RECORD_SPRITE: {
        if (rec_size != sizeof(micropixel_raster_sprite_t)) {
            return MICROPIXEL_STATUS_INVALID_ARGUMENT;
        }
        micropixel_raster_sprite_t s;
        memcpy(&s, rec, sizeof(s));
        if (s.texture_slot >= MPX_RT_SLOTS || !mpx_rt[s.texture_slot].used ||
            s.width == 0 || s.height == 0 ||
            (uint32_t)s.source_x + s.source_width > mpx_rt[s.texture_slot].width ||
            (uint32_t)s.source_y + s.source_height > mpx_rt[s.texture_slot].height ||
            s.source_width == 0 || s.source_height == 0 ||
            !(s.flags & MICROPIXEL_RASTER_SPRITE_SOLID_COLOR) &&
                mpx_palette_entry(s.palette_slot, s.light_level) == NULL) {
            return MICROPIXEL_STATUS_INVALID_ARGUMENT;
        }
        if (s.flags & MICROPIXEL_RASTER_SPRITE_ADDITIVE) {
            /* 未通告 CAP_RASTER_SPRITE_ADDITIVE 的宿主拒绝 */
            return MICROPIXEL_STATUS_UNSUPPORTED;
        }
        return MICROPIXEL_STATUS_OK;
    }
    default:
        return MICROPIXEL_STATUS_UNSUPPORTED;
    }
}

int32_t mpx_raster_submit(const uint8_t *bytes, uint32_t length)
{
    micropixel_raster_header_t header;
    if (bytes == NULL || length < sizeof(header)) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
    memcpy(&header, bytes, sizeof(header));
    if (header.magic != MICROPIXEL_GRAPHICS_RASTER_MAGIC || header.total_size != length ||
        header.record_count == 0 || header.flags != 0) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
    uint8_t *data = NULL;
    uint32_t width, height, pitch;
    int32_t status = mpx_display_surface_buffer(header.surface_handle, header.buffer_index,
                                                &width, &height, &pitch, &data);
    if (status != MICROPIXEL_STATUS_OK) {
        return status;
    }

    /* 两遍:整表校验(尺寸/类型/槽位)通过后才开始写像素 */
    const uint8_t *rec = bytes + sizeof(header);
    const uint8_t *end = bytes + length;
    for (uint16_t i = 0; i < header.record_count; i++) {
        if (rec + 4 > end) {
            return MICROPIXEL_STATUS_INVALID_ARGUMENT;
        }
        uint32_t rec_size;
        int32_t status2 = mpx_validate_record(rec, (uint32_t)(end - rec));
        if (status2 != MICROPIXEL_STATUS_OK) {
            return status2;
        }
        /* 每型尺寸为定长,按 type 前进 */
        switch (rec[0]) {
        case MICROPIXEL_RASTER_RECORD_RECT:
            rec_size = sizeof(micropixel_raster_rect_t);
            break;
        case MICROPIXEL_RASTER_RECORD_SPRITE:
            rec_size = sizeof(micropixel_raster_sprite_t);
            break;
        default:
            return MICROPIXEL_STATUS_INVALID_ARGUMENT; /* validate 已拒,防御 */
        }
        if (rec + rec_size > end) {
            return MICROPIXEL_STATUS_INVALID_ARGUMENT;
        }
        rec += rec_size;
    }
    if (rec != end) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT; /* 尾部多余字节 */
    }

    mpx_target_t target = { data, width, height, pitch };
    rec = bytes + sizeof(header);
    for (uint16_t i = 0; i < header.record_count; i++) {
        if (rec[0] == MICROPIXEL_RASTER_RECORD_RECT) {
            mpx_exec_rect(rec, &target);
            rec += sizeof(micropixel_raster_rect_t);
        } else {
            mpx_exec_sprite(rec, &target);
            rec += sizeof(micropixel_raster_sprite_t);
        }
    }
    return MICROPIXEL_STATUS_OK;
}
