/* 资源服务：从当前包解 PNG，按 guest 给出的比例收成贴图。 */
#include "app_config.h"

#if (defined MINI_WASM_SUPPORT) && MINI_WASM_SUPPORT

#include "system/includes.h"

#include <stdlib.h>
#include <string.h>

#include "asm/wdt.h"
#include "extra/libs/png/lodepng.h"
#include "lvgl.h"
#include "mpx_compat.h"
#include "mpx_res.h"
#include "mpx_store.h"

#define MPX_TAG "[mpx] "
#define MPX_TEX_MAX 24

typedef struct mpx_tex {
    uint8_t used;
    uint32_t logical_w;
    uint32_t logical_h;
    uint32_t phys_w;
    uint32_t phys_h;
    uint32_t *px;
} mpx_tex_t;

static mpx_tex_t mpx_texs[MPX_TEX_MAX];
static char mpx_text_buf[1025];
static uint32_t mpx_scale_num = 1;
static uint32_t mpx_scale_den = 1;

static uint32_t mpx_apply_scale(uint32_t value, uint32_t num, uint32_t den)
{
    uint32_t scaled;

    if (value == 0 || den == 0) {
        return 0;
    }
    scaled = (uint32_t)(((uint64_t)value * num + den / 2u) / den);
    if (scaled == 0) {
        scaled = 1;
    }
    return scaled;
}

void mpx_display_scale(uint32_t *num, uint32_t *den)
{
    if (num) {
        *num = mpx_scale_num ? mpx_scale_num : 1;
    }
    if (den) {
        *den = mpx_scale_den ? mpx_scale_den : 1;
    }
}

static uint32_t mpx_scaled_dim(uint32_t value, uint32_t num, uint32_t den)
{
    uint32_t scaled;

    if (value == 0 || den == 0) {
        return 0;
    }
    scaled = (uint32_t)(((uint64_t)value * num + den / 2u) / den);
    if (scaled == 0) {
        scaled = 1;
    }
    return scaled;
}

void mpx_texture_reset(void)
{
    uint32_t i;

    for (i = 0; i < MPX_TEX_MAX; i++) {
        if (mpx_texs[i].px != NULL) {
            free(mpx_texs[i].px);
        }
    }
    memset(mpx_texs, 0, sizeof(mpx_texs));
    mpx_scale_num = 1;
    mpx_scale_den = 1;
}

int mpx_texture_sample(uint32_t handle, uint32_t x, uint32_t y, uint32_t *rgb, uint8_t *alpha)
{
    mpx_tex_t *tex;
    uint32_t pix;

    if (handle == 0 || handle > MPX_TEX_MAX || rgb == NULL || alpha == NULL) {
        return 0;
    }
    tex = &mpx_texs[handle - 1];
    if (!tex->used || tex->px == NULL || x >= tex->phys_w || y >= tex->phys_h) {
        return 0;
    }
    pix = tex->px[y * tex->phys_w + x];
    *rgb = pix & 0x00ffffffu;
    *alpha = (uint8_t)(pix >> 24);
    return 1;
}

static int32_t mpx_texture_load(uint32_t asset_id, uint32_t num, uint32_t den, micropixel_texture_info_t *info)
{
    const uint8_t *png = NULL;
    uint32_t png_size = 0;
    uint32_t logical_w = 0;
    uint32_t logical_h = 0;
    uint8_t *rgba = NULL;
    unsigned rw = 0;
    unsigned rh = 0;
    unsigned err;
    uint32_t pw;
    uint32_t ph;
    uint32_t *px = NULL;
    uint32_t y;
    uint32_t slot;
    mpx_tex_t *tex;

    if (asset_id == 0 || num == 0 || den == 0 || num > 4096u || den > 4096u) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
    mpx_scale_num = num;
    mpx_scale_den = den;
    if (mpx_store_live_png(asset_id, &png, &png_size, &logical_w, &logical_h) != 0 || png == NULL) {
        return MICROPIXEL_STATUS_NOT_FOUND;
    }
    wdt_clear();
    err = lodepng_decode32(&rgba, &rw, &rh, png, png_size);
    wdt_clear();
    if (err != 0 || rgba == NULL || rw == 0 || rh == 0) {
        lodepng_free(rgba);
        return MICROPIXEL_STATUS_INTERNAL;
    }
    if (logical_w == 0 || logical_h == 0) {
        logical_w = rw;
        logical_h = rh;
    }
    pw = mpx_scaled_dim(rw, num, den);
    ph = mpx_scaled_dim(rh, num, den);
    if (pw == 0 || ph == 0 || (uint64_t)pw * ph > 1024u * 1024u) {
        lodepng_free(rgba);
        return MICROPIXEL_STATUS_RESOURCE_EXHAUSTED;
    }
    px = (uint32_t *)malloc((size_t)pw * ph * sizeof(uint32_t));
    if (px == NULL) {
        lodepng_free(rgba);
        return MICROPIXEL_STATUS_RESOURCE_EXHAUSTED;
    }
    for (y = 0; y < ph; y++) {
        uint32_t sy = (uint32_t)(((uint64_t)y * rh) / ph);
        uint32_t x;
        if (sy >= rh) {
            sy = rh - 1;
        }
        for (x = 0; x < pw; x++) {
            uint32_t sx = (uint32_t)(((uint64_t)x * rw) / pw);
            const uint8_t *p;
            if (sx >= rw) {
                sx = rw - 1;
            }
            p = rgba + ((size_t)sy * rw + sx) * 4u;
            px[y * pw + x] = ((uint32_t)p[3] << 24) | ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
        }
        if ((y & 15u) == 0) {
            wdt_clear();
        }
    }
    lodepng_free(rgba);
    slot = MPX_TEX_MAX;
    for (y = 0; y < MPX_TEX_MAX; y++) {
        if (!mpx_texs[y].used) {
            slot = y;
            break;
        }
    }
    if (slot == MPX_TEX_MAX) {
        free(px);
        return MICROPIXEL_STATUS_RESOURCE_EXHAUSTED;
    }
    tex = &mpx_texs[slot];
    memset(tex, 0, sizeof(*tex));
    tex->used = 1;
    tex->logical_w = logical_w;
    tex->logical_h = logical_h;
    tex->phys_w = pw;
    tex->phys_h = ph;
    tex->px = px;
    memset(info, 0, sizeof(*info));
    info->size = (uint16_t)sizeof(*info);
    info->texture_handle = slot + 1u;
    info->width = logical_w;
    info->height = logical_h;
    info->physical_width = pw;
    info->physical_height = ph;
    info->pixel_format = MICROPIXEL_PIXEL_FORMAT_RGB565;
    info->flags = 0;
    printf(MPX_TAG "tex %u %ux%u -> %ux%u\n", asset_id, logical_w, logical_h, pw, ph);
    return MICROPIXEL_STATUS_OK;
}

static int32_t mpx_texture_unload(uint32_t handle)
{
    mpx_tex_t *tex;

    if (handle == 0 || handle > MPX_TEX_MAX) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
    tex = &mpx_texs[handle - 1];
    if (!tex->used) {
        return MICROPIXEL_STATUS_NOT_FOUND;
    }
    free(tex->px);
    memset(tex, 0, sizeof(*tex));
    return MICROPIXEL_STATUS_OK;
}

static int32_t mpx_write_out(const void *value, uint32_t size, uint8_t *response,
                             uint32_t response_capacity, uint32_t *response_size_out)
{
    if (response == NULL || response_size_out == NULL || response_capacity < size) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
    memcpy(response, value, size);
    *response_size_out = size;
    return MICROPIXEL_STATUS_OK;
}

int32_t mpx_resource_call(uint32_t method_id, const uint8_t *request, uint32_t request_size,
                          uint8_t *response, uint32_t response_capacity, uint32_t *response_size_out)
{
    if (method_id == MICROPIXEL_RESOURCE_METHOD_TEXTURE_LOAD) {
        micropixel_texture_load_request_t wire;
        micropixel_texture_info_t info;
        int32_t status;
        if (request == NULL || request_size < sizeof(wire)) {
            return MICROPIXEL_STATUS_INVALID_ARGUMENT;
        }
        memcpy(&wire, request, sizeof(wire));
        if (wire.size < sizeof(wire) || wire.reserved0 != 0) {
            return MICROPIXEL_STATUS_INVALID_ARGUMENT;
        }
        status = mpx_texture_load(wire.asset_id, wire.scale_numerator, wire.scale_denominator, &info);
        if (status != MICROPIXEL_STATUS_OK) {
            return status;
        }
        return mpx_write_out(&info, sizeof(info), response, response_capacity, response_size_out);
    }
    if (method_id == MICROPIXEL_RESOURCE_METHOD_TEXTURE_UNLOAD) {
        micropixel_handle_request_t wire;
        if (request == NULL || request_size < sizeof(wire)) {
            return MICROPIXEL_STATUS_INVALID_ARGUMENT;
        }
        memcpy(&wire, request, sizeof(wire));
        if (wire.size < sizeof(wire) || wire.reserved0 != 0) {
            return MICROPIXEL_STATUS_INVALID_ARGUMENT;
        }
        return mpx_texture_unload(wire.handle);
    }
    return MICROPIXEL_STATUS_UNSUPPORTED;
}

int32_t mpx_measure_text(const uint8_t *request, uint32_t request_size, uint8_t *response,
                         uint32_t response_capacity, uint32_t *response_size_out)
{
    micropixel_graphics_measure_text_request_t header;
    micropixel_text_metrics_t metrics;
    const lv_font_t *font = &lv_font_simsun_16_cjk;
    uint32_t px;
    lv_coord_t width;

    if (request == NULL || request_size < sizeof(header)) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
    memcpy(&header, request, sizeof(header));
    if (header.font_handle == 0 || header.text_length == 0 ||
        request_size < sizeof(header) + header.text_length || header.text_length > 1024) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
    memcpy(mpx_text_buf, request + sizeof(header), header.text_length);
    mpx_text_buf[header.text_length] = 0;
    width = lv_txt_get_width(mpx_text_buf, header.text_length, font, 0, LV_TEXT_FLAG_NONE);
    if (width < 0) {
        width = 0;
    }
    switch (header.font_handle) {
    case MICROPIXEL_SYSTEM_FONT_SMALL:
        px = 12;
        break;
    case MICROPIXEL_SYSTEM_FONT_LARGE:
        px = 22;
        break;
    case MICROPIXEL_SYSTEM_FONT_TITLE:
        px = 28;
        break;
    default:
        px = 16;
        break;
    }
    memset(&metrics, 0, sizeof(metrics));
    metrics.size = (uint16_t)sizeof(metrics);
    metrics.width = mpx_apply_scale((uint32_t)width * px / 16u, mpx_scale_num, mpx_scale_den);
    metrics.height = mpx_apply_scale((uint32_t)font->line_height * px / 16u, mpx_scale_num, mpx_scale_den);
    metrics.baseline = (int32_t)mpx_apply_scale((uint32_t)font->base_line * px / 16u, mpx_scale_num, mpx_scale_den);
    return mpx_write_out(&metrics, sizeof(metrics), response, response_capacity, response_size_out);
}

#endif
