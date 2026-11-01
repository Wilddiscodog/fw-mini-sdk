/* app/mpx/mpx_display.c - Graphics 服务的 surface 缓冲与上屏
 *
 * 缓冲保存在 SDRAM 堆,字节序即面板序(JD9853 高字节在前),present 时
 * 走 lcd_lvgl_full(内含 TE 同步与 EMI DMA),整帧一次性写出。
 * 与 watch_ui 的 LVGL 任务并存期间会互相覆盖屏幕,属预期(bring-up 阶段)。
 */
#include "mpx_display.h"

#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#if (defined MINI_LCD_SUPPORT) && MINI_LCD_SUPPORT
#include "lcd_config.h"
#define MPX_LCD_W LCD_W
#define MPX_LCD_H LCD_H
#else
#define MPX_LCD_W 296
#define MPX_LCD_H 240
#endif

#include "mpx_services.h"

#define MPX_TAG "[mpx] "
#define MPX_MAX_SURFACES 4
#define MPX_MAX_BUFFERS_PER_SURFACE 2

typedef struct mpx_surface_buffer {
    uint8_t *data; /* pitch*height*2 字节, 面板字节序 RGB565 */
} mpx_surface_buffer_t;

typedef struct mpx_surface {
    uint8_t used;
    uint32_t width;
    uint32_t height;
    uint32_t buffer_count;
    mpx_surface_buffer_t buffers[MPX_MAX_BUFFERS_PER_SURFACE];
} mpx_surface_t;

static mpx_surface_t mpx_surfaces[MPX_MAX_SURFACES];

extern void lcd_lvgl_full(u16 xs, u16 xe, u16 ys, u16 ye, char *img);

uint16_t mpx_display_width(void)
{
    return MPX_LCD_W;
}

uint16_t mpx_display_height(void)
{
    return MPX_LCD_H;
}

int32_t mpx_display_init(void)
{
    memset(mpx_surfaces, 0, sizeof(mpx_surfaces));
    return MICROPIXEL_STATUS_OK;
}

void mpx_display_deinit(void)
{
    uint32_t i;
    for (i = 0; i < MPX_MAX_SURFACES; i++) {
        if (mpx_surfaces[i].used) {
            mpx_display_surface_destroy(i + 1);
        }
    }
}

int32_t mpx_display_get_info(micropixel_graphics_info_t *info)
{
    memset(info, 0, sizeof(*info));
    info->size = (uint16_t)sizeof(*info);
    info->width = MPX_LCD_W;
    info->height = MPX_LCD_H;
    info->pixel_format = MICROPIXEL_PIXEL_FORMAT_RGB565;
    info->safe_inset_top = 0;
    info->safe_inset_right = 0;
    info->safe_inset_bottom = 0;
    info->safe_inset_left = 0;
    info->native_pixel_format = MICROPIXEL_PIXEL_FORMAT_RGB565;
    info->native_flags = 0;
    info->max_full_frame_fps = 30;
    info->max_surface_buffers = MPX_MAX_BUFFERS_PER_SURFACE;
    info->max_text_bytes = 1024;
    info->max_scene_bytes = 0;          /* Scene 通道不启用 */
    info->max_raster_bytes = 32 * 1024; /* RASTER 通道(M3) */
    return MICROPIXEL_STATUS_OK;
}

int32_t mpx_display_surface_create(uint32_t width, uint32_t height, uint32_t pixel_format,
                                   uint32_t buffer_count, uint32_t *surface_handle_out)
{
    uint32_t i, j;
    if (pixel_format != MICROPIXEL_PIXEL_FORMAT_RGB565 || width == 0 || height == 0 ||
        width > 1024 || height > 1024 || buffer_count < 1 || buffer_count > MPX_MAX_BUFFERS_PER_SURFACE) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
    for (i = 0; i < MPX_MAX_SURFACES; i++) {
        mpx_surface_t *s = &mpx_surfaces[i];
        if (s->used) {
            continue;
        }
        for (j = 0; j < buffer_count; j++) {
            s->buffers[j].data = (uint8_t *)malloc((size_t)width * height * 2);
            if (s->buffers[j].data == NULL) {
                while (j--) {
                    free(s->buffers[j].data);
                    s->buffers[j].data = NULL;
                }
                return MICROPIXEL_STATUS_RESOURCE_EXHAUSTED;
            }
        }
        s->used = 1;
        s->width = width;
        s->height = height;
        s->buffer_count = buffer_count;
        *surface_handle_out = i + 1;
        printf(MPX_TAG "surface %u: %ux%ux%u RGB565\n", (unsigned)(i + 1), (unsigned)width,
               (unsigned)height, (unsigned)buffer_count);
        return MICROPIXEL_STATUS_OK;
    }
    return MICROPIXEL_STATUS_RESOURCE_EXHAUSTED;
}

int32_t mpx_display_surface_present(uint32_t surface_handle, uint32_t buffer_index)
{
    if (surface_handle == 0 || surface_handle > MPX_MAX_SURFACES ||
        !mpx_surfaces[surface_handle - 1].used) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
    mpx_surface_t *s = &mpx_surfaces[surface_handle - 1];
    if (buffer_index >= s->buffer_count || s->buffers[buffer_index].data == NULL) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
#if (defined MINI_LCD_SUPPORT) && MINI_LCD_SUPPORT
    if (s->width == MPX_LCD_W && s->height == MPX_LCD_H) {
        lcd_lvgl_full(0, (u16)(MPX_LCD_W - 1), 0, (u16)(MPX_LCD_H - 1),
                      (char *)s->buffers[buffer_index].data);
    } else {
        /* 非 1:1 尺寸: v1 简化为拒绝,SDK 侧通常按 panel 尺寸建 surface */
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
#endif
    return MICROPIXEL_STATUS_OK;
}

void mpx_display_surface_destroy(uint32_t surface_handle)
{
    uint32_t j;
    if (surface_handle == 0 || surface_handle > MPX_MAX_SURFACES ||
        !mpx_surfaces[surface_handle - 1].used) {
        return;
    }
    mpx_surface_t *s = &mpx_surfaces[surface_handle - 1];
    for (j = 0; j < s->buffer_count; j++) {
        free(s->buffers[j].data);
        s->buffers[j].data = NULL;
    }
    s->used = 0;
    s->buffer_count = 0;
}

int32_t mpx_display_surface_buffer(uint32_t surface_handle, uint32_t buffer_index,
                                   uint32_t *width_out, uint32_t *height_out,
                                   uint32_t *pitch_out, uint8_t **data_out)
{
    if (surface_handle == 0 || surface_handle > MPX_MAX_SURFACES ||
        !mpx_surfaces[surface_handle - 1].used) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
    mpx_surface_t *s = &mpx_surfaces[surface_handle - 1];
    if (buffer_index >= s->buffer_count || s->buffers[buffer_index].data == NULL) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
    *width_out = s->width;
    *height_out = s->height;
    *pitch_out = s->width * 2;
    *data_out = s->buffers[buffer_index].data;
    return MICROPIXEL_STATUS_OK;
}
