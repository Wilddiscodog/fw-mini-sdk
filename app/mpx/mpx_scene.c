/* 场景通道：把 guest 已经换成面板像素的节点画进 RGB565 帧并上屏。
 * 贴图从资源服务的解码结果里采样。文字走 LVGL 点阵。 */
#include "app_config.h"

#include "system/includes.h"

#include <stdlib.h>
#include <string.h>

#include "mpx_services.h"
#include "mpx_display.h"
#include "mpx_res.h"

extern volatile unsigned char mpx_display_hold;

#if (defined MINI_LVGL_SUPPORT) && MINI_LVGL_SUPPORT
#include "lvgl.h"
#endif

#if (defined MINI_LCD_SUPPORT) && MINI_LCD_SUPPORT
extern void lcd_lvgl_full(u16 xs, u16 xe, u16 ys, u16 ye, char *img);
#endif

#define MPX_TAG "[mpx] "
#define MPX_NODES 256
#define MPX_CONS 64
#define MPX_INSTS 1024
#define MPX_TEXT 8192

typedef struct mpx_node {
    uint8_t used;
    uint8_t kind;
    uint8_t visible;
    uint8_t opacity;
    uint16_t parent;
    int32_t x, y, w, h;
    uint32_t rgb;
    uint32_t stroke_rgb;
    uint32_t radius;
    uint32_t stroke_w;
    uint32_t texture;
    int32_t src_x, src_y, src_w, src_h;
    uint16_t text_off;
    uint16_t text_len;
    uint16_t inst_first;
    uint16_t inst_cap;
} mpx_node_t;

typedef struct mpx_con {
    uint8_t used;
    uint8_t visible;
    uint16_t parent;
    int32_t clip_x, clip_y, clip_w, clip_h;
    int32_t tx, ty;
} mpx_con_t;

typedef struct mpx_inst {
    uint8_t visible;
    uint8_t opacity;
    int32_t x, y, w, h;
    int32_t src_x, src_y, src_w, src_h;
    uint32_t rgb;
} mpx_inst_t;

static mpx_node_t mpx_nodes[MPX_NODES];
static mpx_con_t mpx_cons[MPX_CONS];
static mpx_inst_t mpx_insts[MPX_INSTS];
static uint16_t mpx_inst_used;
static uint8_t mpx_text[MPX_TEXT];
static uint16_t mpx_text_used;
static uint32_t mpx_bg = 0x101418;
static uint32_t mpx_revision;
static int mpx_scene_valid;
static uint8_t *mpx_fb;
static uint16_t mpx_fb_w;
static uint16_t mpx_fb_h;
static int mpx_clip_x0, mpx_clip_y0, mpx_clip_x1, mpx_clip_y1;
static int mpx_scene_logged;

static void mpx_clear_scene(void)
{
    memset(mpx_nodes, 0, sizeof(mpx_nodes));
    memset(mpx_cons, 0, sizeof(mpx_cons));
    memset(mpx_insts, 0, sizeof(mpx_insts));
    mpx_inst_used = 0;
    mpx_text_used = 0;
    mpx_scene_valid = 0;
}

void mpx_scene_reset(void)
{
    mpx_clear_scene();
    mpx_revision = 0;
    mpx_bg = 0x101418;
    mpx_scene_logged = 0;
    if (mpx_fb) {
        free(mpx_fb);
        mpx_fb = NULL;
    }
}

static int mpx_read(const uint8_t *p, const uint8_t *end, void *dst, uint32_t n)
{
    if (p + n > end) {
        return -1;
    }
    memcpy(dst, p, n);
    return 0;
}

static uint16_t mpx_rgb565(uint32_t rgb)
{
    return (uint16_t)((((rgb >> 16) & 0xf8) << 8) | (((rgb >> 8) & 0xfc) << 3) | ((rgb & 0xff) >> 3));
}

static void mpx_put(int x, int y, uint32_t rgb, uint8_t opacity)
{
    uint16_t c;
    uint16_t old;
    uint8_t *p;
    int or, og, ob, nr, ng, nb;
    if (mpx_fb == NULL || x < mpx_clip_x0 || y < mpx_clip_y0 || x >= mpx_clip_x1 || y >= mpx_clip_y1) {
        return;
    }
    c = mpx_rgb565(rgb);
    p = mpx_fb + ((y * mpx_fb_w + x) << 1);
    if (opacity < 250) {
        old = (uint16_t)((p[0] << 8) | p[1]);
        or = (old >> 11) & 31;
        og = (old >> 5) & 63;
        ob = old & 31;
        nr = (c >> 11) & 31;
        ng = (c >> 5) & 63;
        nb = c & 31;
        or += ((nr - or) * opacity) / 255;
        og += ((ng - og) * opacity) / 255;
        ob += ((nb - ob) * opacity) / 255;
        c = (uint16_t)((or << 11) | (og << 5) | ob);
    }
    p[0] = (uint8_t)(c >> 8);
    p[1] = (uint8_t)c;
}

static void mpx_fill(int x, int y, int w, int h, uint32_t rgb, uint8_t opacity)
{
    int x1, y1, ix, iy;
    if (w <= 0 || h <= 0 || opacity == 0) {
        return;
    }
    x1 = x + w;
    y1 = y + h;
    if (x < mpx_clip_x0) {
        x = mpx_clip_x0;
    }
    if (y < mpx_clip_y0) {
        y = mpx_clip_y0;
    }
    if (x1 > mpx_clip_x1) {
        x1 = mpx_clip_x1;
    }
    if (y1 > mpx_clip_y1) {
        y1 = mpx_clip_y1;
    }
    for (iy = y; iy < y1; iy++) {
        for (ix = x; ix < x1; ix++) {
            mpx_put(ix, iy, rgb, opacity);
        }
    }
}

static int mpx_node_origin(const mpx_node_t *node, int *ox, int *oy)
{
    uint16_t id = node->parent;
    int guard = 0;
    *ox = node->x;
    *oy = node->y;
    while (id != 0) {
        const mpx_con_t *con;
        if (id >= MPX_CONS || !mpx_cons[id].used || !mpx_cons[id].visible) {
            return 0;
        }
        con = &mpx_cons[id];
        *ox += con->tx;
        *oy += con->ty;
        id = con->parent;
        if (++guard > 16) {
            return 0;
        }
    }
    return 1;
}

static void mpx_set_clip_screen(void)
{
    mpx_clip_x0 = 0;
    mpx_clip_y0 = 0;
    mpx_clip_x1 = mpx_fb_w;
    mpx_clip_y1 = mpx_fb_h;
}

static void mpx_clip_parent(uint16_t id)
{
    int guard = 0;
    while (id != 0 && id < MPX_CONS && mpx_cons[id].used && guard++ < 16) {
        mpx_con_t *con = &mpx_cons[id];
        if (con->clip_w > 0 && con->clip_h > 0) {
            if (con->clip_x > mpx_clip_x0) {
                mpx_clip_x0 = con->clip_x;
            }
            if (con->clip_y > mpx_clip_y0) {
                mpx_clip_y0 = con->clip_y;
            }
            if (con->clip_x + con->clip_w < mpx_clip_x1) {
                mpx_clip_x1 = con->clip_x + con->clip_w;
            }
            if (con->clip_y + con->clip_h < mpx_clip_y1) {
                mpx_clip_y1 = con->clip_y + con->clip_h;
            }
        }
        id = con->parent;
    }
}

#if (defined MINI_LVGL_SUPPORT) && MINI_LVGL_SUPPORT
static uint32_t mpx_utf8(const uint8_t **p, const uint8_t *end)
{
    uint32_t c;
    if (*p >= end) {
        return 0;
    }
    c = *(*p)++;
    if (c < 0x80) {
        return c;
    }
    if ((c & 0xe0) == 0xc0 && *p < end) {
        return ((c & 0x1f) << 6) | (*(*p)++ & 0x3f);
    }
    if ((c & 0xf0) == 0xe0 && *p + 1 < end) {
        uint32_t c1 = *(*p)++;
        uint32_t c2 = *(*p)++;
        return ((c & 0x0f) << 12) | ((c1 & 0x3f) << 6) | (c2 & 0x3f);
    }
    return c;
}

static int mpx_px(int value, uint32_t num, uint32_t den)
{
    int64_t v = value;
    int64_t scaled;

    if (den == 0 || num == den) {
        return value;
    }
    if (v < 0) {
        scaled = -(((-v) * (int64_t)num + (int64_t)den / 2) / (int64_t)den);
        if (scaled == 0) {
            scaled = -1;
        }
    } else if (v > 0) {
        scaled = (v * (int64_t)num + (int64_t)den / 2) / (int64_t)den;
        if (scaled == 0) {
            scaled = 1;
        }
    } else {
        scaled = 0;
    }
    return (int)scaled;
}

static void mpx_glyph(const lv_font_t *font, uint32_t letter, int x, int y, uint32_t rgb, uint8_t opacity,
                      uint32_t num, uint32_t den)
{
    lv_font_glyph_dsc_t dsc;
    const uint8_t *map;
    int row, col;
    int dw, dh, src_oy;
    if (!lv_font_get_glyph_dsc(font, &dsc, letter, 0) || dsc.box_w == 0 || dsc.box_h == 0) {
        return;
    }
    map = lv_font_get_glyph_bitmap(font, letter);
    if (map == NULL) {
        return;
    }
    src_oy = (int)font->line_height - (int)font->base_line - (int)dsc.box_h - (int)dsc.ofs_y;
    dw = mpx_px((int)dsc.box_w, num, den);
    dh = mpx_px((int)dsc.box_h, num, den);
    y += mpx_px(src_oy, num, den);
    x += mpx_px((int)dsc.ofs_x, num, den);
    for (row = 0; row < dh; row++) {
        int sy = (int)(((int64_t)row * dsc.box_h) / dh);
        for (col = 0; col < dw; col++) {
            int sx = (int)(((int64_t)col * dsc.box_w) / dw);
            uint32_t bit = (uint32_t)(sy * dsc.box_w + sx) * dsc.bpp;
            uint8_t byte = map[bit >> 3];
            uint8_t shade = (uint8_t)((byte >> (8 - dsc.bpp - (bit & 7))) & ((1u << dsc.bpp) - 1u));
            uint8_t opa;
            if (shade == 0) {
                continue;
            }
            opa = (uint8_t)((shade * 255) / ((1u << dsc.bpp) - 1u));
            if (opacity < 250) {
                opa = (uint8_t)((opa * opacity) / 255);
            }
            mpx_put(x + col, y + row, rgb, opa);
        }
    }
}

static void mpx_draw_text(const mpx_node_t *node, int x, int y)
{
    const lv_font_t *font = &lv_font_simsun_16_cjk;
    const uint8_t *p;
    const uint8_t *end;
    int unscaled = 0;
    uint32_t num = 1;
    uint32_t den = 1;
    if (node->text_len == 0 || node->text_off + node->text_len > mpx_text_used) {
        return;
    }
    mpx_display_scale(&num, &den);
    p = mpx_text + node->text_off;
    end = p + node->text_len;
    while (p < end) {
        uint32_t letter = mpx_utf8(&p, end);
        lv_font_glyph_dsc_t dsc;
        if (letter == 0) {
            break;
        }
        mpx_glyph(font, letter, x + mpx_px(unscaled, num, den), y, node->rgb, node->opacity, num, den);
        if (lv_font_get_glyph_dsc(font, &dsc, letter, 0)) {
            unscaled += dsc.adv_w ? dsc.adv_w : dsc.box_w;
        } else {
            unscaled += 8;
        }
    }
}
#endif

static void mpx_blit_tex(int dx, int dy, int dw, int dh, uint32_t handle, int sx, int sy, int sw, int sh,
                         uint8_t opacity, uint32_t fallback)
{
    int yy;
    int xx;

    if (dw <= 0 || dh <= 0) {
        return;
    }
    if (handle == 0 || sw <= 0 || sh <= 0) {
        mpx_fill(dx, dy, dw, dh, fallback, opacity);
        return;
    }
    for (yy = 0; yy < dh; yy++) {
        int syy = sy + (int)(((int64_t)yy * sh) / dh);
        for (xx = 0; xx < dw; xx++) {
            int sxx = sx + (int)(((int64_t)xx * sw) / dw);
            uint32_t rgb;
            uint8_t alpha;
            uint32_t opa;
            if (sxx < 0 || syy < 0) {
                continue;
            }
            if (!mpx_texture_sample(handle, (uint32_t)sxx, (uint32_t)syy, &rgb, &alpha)) {
                continue;
            }
            opa = (uint32_t)opacity * alpha / 255u;
            if (opa == 0) {
                continue;
            }
            mpx_put(dx + xx, dy + yy, rgb, (uint8_t)opa);
        }
    }
}

static void mpx_draw_node(const mpx_node_t *node)
{
    int x, y;
    uint16_t i;
    if (!node->used || !node->visible || node->opacity == 0) {
        return;
    }
    if (!mpx_node_origin(node, &x, &y)) {
        return;
    }
    mpx_set_clip_screen();
    mpx_clip_parent(node->parent);
    if (mpx_clip_x0 >= mpx_clip_x1 || mpx_clip_y0 >= mpx_clip_y1) {
        return;
    }
    if (node->kind == MICROPIXEL_GRAPHICS_SCENE_OP_RECT) {
        mpx_fill(x, y, node->w, node->h, node->rgb, node->opacity);
    } else if (node->kind == MICROPIXEL_GRAPHICS_SCENE_OP_TEXTURE) {
        mpx_blit_tex(x, y, node->w, node->h, node->texture, node->src_x, node->src_y, node->src_w, node->src_h,
                     node->opacity, node->rgb);
    } else if (node->kind == MICROPIXEL_GRAPHICS_SCENE_OP_ROUNDED_RECT) {
        if (node->stroke_w > 0) {
            mpx_fill(x, y, node->w, node->h, node->stroke_rgb, node->opacity);
            mpx_fill(x + (int)node->stroke_w, y + (int)node->stroke_w,
                     node->w - (int)node->stroke_w * 2, node->h - (int)node->stroke_w * 2,
                     node->rgb, node->opacity);
        } else {
            mpx_fill(x, y, node->w, node->h, node->rgb, node->opacity);
        }
    } else if (node->kind == MICROPIXEL_GRAPHICS_SCENE_OP_TEXT) {
#if (defined MINI_LVGL_SUPPORT) && MINI_LVGL_SUPPORT
        mpx_draw_text(node, x, y);
#endif
    } else if (node->kind == MICROPIXEL_GRAPHICS_SCENE_OP_SPRITE_BATCH) {
        uint16_t n = node->inst_cap;
        if (node->inst_first + n > MPX_INSTS) {
            n = 0;
        }
        for (i = 0; i < n; i++) {
            mpx_inst_t *inst = &mpx_insts[node->inst_first + i];
            if (!inst->visible || inst->opacity == 0) {
                continue;
            }
            {
                uint8_t opa = (uint8_t)((uint32_t)node->opacity * inst->opacity / 255u);
                if (node->texture != 0) {
                    mpx_blit_tex(x + inst->x, y + inst->y, inst->w, inst->h, node->texture, inst->src_x, inst->src_y,
                                 inst->src_w, inst->src_h, opa, inst->rgb);
                } else {
                    mpx_fill(x + inst->x, y + inst->y, inst->w, inst->h, inst->rgb, opa);
                }
            }
        }
    }
}

static void mpx_present(void)
{
    uint32_t i;
    if (mpx_fb == NULL) {
        mpx_fb_w = mpx_display_width();
        mpx_fb_h = mpx_display_height();
        mpx_fb = (uint8_t *)malloc((size_t)mpx_fb_w * mpx_fb_h * 2);
        if (mpx_fb == NULL) {
            return;
        }
    }
    mpx_set_clip_screen();
    mpx_fill(0, 0, mpx_fb_w, mpx_fb_h, mpx_bg, 255);
    for (i = 0; i < MPX_NODES; i++) {
        mpx_draw_node(&mpx_nodes[i]);
    }
#if (defined MINI_LCD_SUPPORT) && MINI_LCD_SUPPORT
    mpx_display_hold = 1;
    lcd_lvgl_full(0, (u16)(mpx_fb_w - 1), 0, (u16)(mpx_fb_h - 1), (char *)mpx_fb);
#endif
    if (!mpx_scene_logged) {
        printf(MPX_TAG "scene present %ux%u\n", (unsigned)mpx_fb_w, (unsigned)mpx_fb_h);
        mpx_scene_logged = 1;
    }
}

static int mpx_store_text(mpx_node_t *node, const uint8_t *text, uint16_t len)
{
    if ((uint32_t)mpx_text_used + len > MPX_TEXT) {
        return -1;
    }
    node->text_off = mpx_text_used;
    node->text_len = len;
    if (len) {
        memcpy(mpx_text + mpx_text_used, text, len);
    }
    mpx_text_used = (uint16_t)(mpx_text_used + len);
    return 0;
}

static int32_t mpx_apply_record(const uint8_t *rec, const uint8_t *end)
{
    micropixel_graphics_scene_record_header_t hdr;
    if (mpx_read(rec, end, &hdr, sizeof(hdr)) != 0 || hdr.size < sizeof(hdr) || rec + hdr.size > end) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
    if (hdr.opcode == MICROPIXEL_GRAPHICS_SCENE_OP_BACKGROUND) {
        micropixel_graphics_scene_background_record_t bg;
        if (mpx_read(rec, end, &bg, sizeof(bg)) != 0) {
            return MICROPIXEL_STATUS_INVALID_ARGUMENT;
        }
        if (bg.property_mask & MICROPIXEL_GRAPHICS_SCENE_BACKGROUND_COLOR) {
            mpx_bg = bg.rgb888;
        }
        return MICROPIXEL_STATUS_OK;
    }
    if (hdr.opcode == MICROPIXEL_GRAPHICS_SCENE_OP_CONTAINER) {
        micropixel_graphics_scene_container_record_t box;
        mpx_con_t *con;
        if (mpx_read(rec, end, &box, sizeof(box)) != 0 || box.container_id == 0 ||
            box.container_id >= MPX_CONS) {
            return MICROPIXEL_STATUS_RESOURCE_EXHAUSTED;
        }
        con = &mpx_cons[box.container_id];
        con->used = 1;
        if (box.property_mask & MICROPIXEL_GRAPHICS_SCENE_CONTAINER_CLIP) {
            con->clip_x = box.clip_x;
            con->clip_y = box.clip_y;
            con->clip_w = box.clip_width;
            con->clip_h = box.clip_height;
        }
        if (box.property_mask & MICROPIXEL_GRAPHICS_SCENE_CONTAINER_TRANSLATION) {
            con->tx = box.translate_x;
            con->ty = box.translate_y;
        }
        if (box.property_mask & MICROPIXEL_GRAPHICS_SCENE_CONTAINER_APPEARANCE) {
            con->visible = box.visible ? 1 : 0;
        }
        if (box.property_mask & MICROPIXEL_GRAPHICS_SCENE_CONTAINER_STRUCTURE) {
            con->parent = box.parent_container_id;
        }
        if (!con->visible && (box.property_mask & MICROPIXEL_GRAPHICS_SCENE_CONTAINER_APPEARANCE) == 0 &&
            con->used) {
            con->visible = 1;
        }
        return MICROPIXEL_STATUS_OK;
    }
    if (hdr.opcode == MICROPIXEL_GRAPHICS_SCENE_OP_NODE_LINK) {
        micropixel_graphics_scene_node_link_record_t link;
        if (mpx_read(rec, end, &link, sizeof(link)) != 0 || link.node_id >= MPX_NODES) {
            return MICROPIXEL_STATUS_RESOURCE_EXHAUSTED;
        }
        mpx_nodes[link.node_id].used = 1;
        mpx_nodes[link.node_id].parent = link.parent_container_id;
        return MICROPIXEL_STATUS_OK;
    }
    if (hdr.opcode == MICROPIXEL_GRAPHICS_SCENE_OP_RECT ||
        hdr.opcode == MICROPIXEL_GRAPHICS_SCENE_OP_ROUNDED_RECT ||
        hdr.opcode == MICROPIXEL_GRAPHICS_SCENE_OP_TEXTURE ||
        hdr.opcode == MICROPIXEL_GRAPHICS_SCENE_OP_TEXT ||
        hdr.opcode == MICROPIXEL_GRAPHICS_SCENE_OP_SPRITE_BATCH) {
        micropixel_graphics_scene_node_header_t node_hdr;
        mpx_node_t *node;
        if (mpx_read(rec, end, &node_hdr, sizeof(node_hdr)) != 0 || node_hdr.node_id >= MPX_NODES) {
            return MICROPIXEL_STATUS_RESOURCE_EXHAUSTED;
        }
        node = &mpx_nodes[node_hdr.node_id];
        node->used = 1;
        if (node_hdr.property_mask & MICROPIXEL_GRAPHICS_SCENE_NODE_KIND) {
            node->kind = (uint8_t)hdr.opcode;
        }
        if (node_hdr.property_mask & MICROPIXEL_GRAPHICS_SCENE_NODE_VISIBILITY) {
            node->visible = (node_hdr.flags & MICROPIXEL_GRAPHICS_SCENE_NODE_VISIBLE) ? 1 : 0;
        } else if (!node->visible && node->kind == 0) {
            node->visible = 1;
        }
        if (hdr.opcode == MICROPIXEL_GRAPHICS_SCENE_OP_RECT) {
            micropixel_graphics_scene_rect_record_t box;
            if (mpx_read(rec, end, &box, sizeof(box)) != 0) {
                return MICROPIXEL_STATUS_INVALID_ARGUMENT;
            }
            if (node_hdr.property_mask & MICROPIXEL_GRAPHICS_SCENE_NODE_GEOMETRY) {
                node->x = box.x;
                node->y = box.y;
                node->w = box.width;
                node->h = box.height;
            }
            if (node_hdr.property_mask & MICROPIXEL_GRAPHICS_SCENE_NODE_APPEARANCE) {
                node->rgb = box.rgb888;
                node->opacity = box.opacity ? box.opacity : 255;
            }
        } else if (hdr.opcode == MICROPIXEL_GRAPHICS_SCENE_OP_ROUNDED_RECT) {
            micropixel_graphics_scene_rounded_rect_record_t box;
            if (mpx_read(rec, end, &box, sizeof(box)) != 0) {
                return MICROPIXEL_STATUS_INVALID_ARGUMENT;
            }
            if (node_hdr.property_mask & MICROPIXEL_GRAPHICS_SCENE_NODE_GEOMETRY) {
                node->x = box.x;
                node->y = box.y;
                node->w = box.width;
                node->h = box.height;
                node->radius = box.radius;
                node->stroke_w = box.stroke_width;
            }
            if (node_hdr.property_mask & MICROPIXEL_GRAPHICS_SCENE_NODE_APPEARANCE) {
                node->rgb = box.fill_rgb888;
                node->stroke_rgb = box.stroke_rgb888;
                node->opacity = box.opacity ? box.opacity : 255;
            }
        } else if (hdr.opcode == MICROPIXEL_GRAPHICS_SCENE_OP_TEXTURE) {
            micropixel_graphics_scene_texture_record_t box;
            if (mpx_read(rec, end, &box, sizeof(box)) != 0) {
                return MICROPIXEL_STATUS_INVALID_ARGUMENT;
            }
            if (node_hdr.property_mask & MICROPIXEL_GRAPHICS_SCENE_NODE_GEOMETRY) {
                node->x = box.x;
                node->y = box.y;
                node->w = box.width;
                node->h = box.height;
            }
            if (node_hdr.property_mask & MICROPIXEL_GRAPHICS_SCENE_NODE_APPEARANCE) {
                node->opacity = box.opacity ? box.opacity : 255;
            }
            if (node_hdr.property_mask & MICROPIXEL_GRAPHICS_SCENE_NODE_CONTENT) {
                node->texture = box.texture_handle;
                node->src_x = box.source_x;
                node->src_y = box.source_y;
                node->src_w = box.source_width;
                node->src_h = box.source_height;
                if (node->texture == 0) {
                    node->rgb = 0x6E7C93;
                }
            }
        } else if (hdr.opcode == MICROPIXEL_GRAPHICS_SCENE_OP_TEXT) {
            micropixel_graphics_scene_text_record_t box;
            if (mpx_read(rec, end, &box, sizeof(box)) != 0) {
                return MICROPIXEL_STATUS_INVALID_ARGUMENT;
            }
            if (node_hdr.property_mask & MICROPIXEL_GRAPHICS_SCENE_NODE_GEOMETRY) {
                node->x = box.x;
                node->y = box.y;
            }
            if (node_hdr.property_mask & MICROPIXEL_GRAPHICS_SCENE_NODE_APPEARANCE) {
                node->rgb = box.rgb888;
                node->opacity = 255;
            }
            if (node_hdr.property_mask & MICROPIXEL_GRAPHICS_SCENE_NODE_CONTENT) {
                if (rec + sizeof(box) + box.text_length > end) {
                    return MICROPIXEL_STATUS_INVALID_ARGUMENT;
                }
                if (mpx_store_text(node, rec + sizeof(box), box.text_length) != 0) {
                    return MICROPIXEL_STATUS_RESOURCE_EXHAUSTED;
                }
            }
        } else if (hdr.opcode == MICROPIXEL_GRAPHICS_SCENE_OP_SPRITE_BATCH) {
            micropixel_graphics_scene_sprite_batch_record_t box;
            if (mpx_read(rec, end, &box, sizeof(box)) != 0) {
                return MICROPIXEL_STATUS_INVALID_ARGUMENT;
            }
            if (node->inst_cap == 0) {
                if ((uint32_t)mpx_inst_used + box.capacity > MPX_INSTS) {
                    return MICROPIXEL_STATUS_RESOURCE_EXHAUSTED;
                }
                node->inst_first = mpx_inst_used;
                node->inst_cap = box.capacity;
                mpx_inst_used = (uint16_t)(mpx_inst_used + box.capacity);
            }
            if (node_hdr.property_mask & MICROPIXEL_GRAPHICS_SCENE_NODE_APPEARANCE) {
                node->opacity = box.opacity ? box.opacity : 255;
            }
            if (node_hdr.property_mask & MICROPIXEL_GRAPHICS_SCENE_NODE_CONTENT) {
                node->texture = box.texture_handle;
            }
        }
        if (node->opacity == 0) {
            node->opacity = 255;
        }
        if ((node_hdr.property_mask & MICROPIXEL_GRAPHICS_SCENE_NODE_VISIBILITY) == 0 && node->kind != 0) {
            node->visible = 1;
        }
        return MICROPIXEL_STATUS_OK;
    }
    if (hdr.opcode == MICROPIXEL_GRAPHICS_SCENE_OP_BATCH_INSTANCES) {
        micropixel_graphics_scene_batch_instances_record_t box;
        mpx_node_t *node;
        uint16_t i;
        if (mpx_read(rec, end, &box, sizeof(box)) != 0 || box.batch_node_id >= MPX_NODES) {
            return MICROPIXEL_STATUS_INVALID_ARGUMENT;
        }
        node = &mpx_nodes[box.batch_node_id];
        if (!node->used || node->inst_cap == 0 ||
            (uint32_t)box.first_instance + box.instance_count > node->inst_cap) {
            return MICROPIXEL_STATUS_INVALID_ARGUMENT;
        }
        for (i = 0; i < box.instance_count; i++) {
            micropixel_graphics_scene_sprite_instance_t inst;
            mpx_inst_t *dst = &mpx_insts[node->inst_first + box.first_instance + i];
            const uint8_t *ip = rec + sizeof(box) + i * sizeof(inst);
            if (mpx_read(ip, end, &inst, sizeof(inst)) != 0) {
                return MICROPIXEL_STATUS_INVALID_ARGUMENT;
            }
            if (box.property_mask & MICROPIXEL_GRAPHICS_SCENE_INSTANCE_GEOMETRY) {
                dst->x = inst.x;
                dst->y = inst.y;
                dst->w = inst.width;
                dst->h = inst.height;
            }
            if (box.property_mask & MICROPIXEL_GRAPHICS_SCENE_INSTANCE_APPEARANCE) {
                dst->rgb = inst.rgb888 ? inst.rgb888 : 0xC8D0DC;
                dst->opacity = inst.opacity ? inst.opacity : 255;
            }
            if (box.property_mask & MICROPIXEL_GRAPHICS_SCENE_INSTANCE_CONTENT) {
                dst->src_x = inst.source_x;
                dst->src_y = inst.source_y;
                dst->src_w = inst.source_width;
                dst->src_h = inst.source_height;
            }
            if (box.property_mask & MICROPIXEL_GRAPHICS_SCENE_INSTANCE_VISIBILITY) {
                dst->visible = (inst.flags & MICROPIXEL_GRAPHICS_SCENE_INSTANCE_VISIBLE) ? 1 : 0;
            } else if (!dst->visible) {
                dst->visible = 1;
            }
        }
        return MICROPIXEL_STATUS_OK;
    }
    return MICROPIXEL_STATUS_UNSUPPORTED;
}

int32_t mpx_scene_submit(const uint8_t *bytes, uint32_t length)
{
    micropixel_graphics_scene_header_t hdr;
    const uint8_t *p;
    const uint8_t *end;
    uint16_t left;
    int keyframe;
    if (bytes == NULL || length < sizeof(hdr) || mpx_read(bytes, bytes + length, &hdr, sizeof(hdr)) != 0) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
    if (hdr.magic != MICROPIXEL_GRAPHICS_SCENE_MAGIC || hdr.total_size != length || hdr.flags != 0) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
    keyframe = hdr.kind == MICROPIXEL_GRAPHICS_SCENE_KEYFRAME;
    if (!keyframe && hdr.kind != MICROPIXEL_GRAPHICS_SCENE_PATCH) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
    if (!keyframe && (!mpx_scene_valid || hdr.base_revision != mpx_revision)) {
        return MICROPIXEL_STATUS_STALE_STATE;
    }
    if (keyframe) {
        mpx_clear_scene();
    }
    p = bytes + sizeof(hdr);
    end = bytes + length;
    left = hdr.record_count;
    while (left--) {
        micropixel_graphics_scene_record_header_t rec;
        int32_t st;
        if (mpx_read(p, end, &rec, sizeof(rec)) != 0 || rec.size < sizeof(rec) || p + rec.size > end) {
            mpx_scene_valid = 0;
            return MICROPIXEL_STATUS_INVALID_ARGUMENT;
        }
        st = mpx_apply_record(p, end);
        if (st != MICROPIXEL_STATUS_OK) {
            mpx_scene_valid = 0;
            return st;
        }
        p += rec.size;
    }
    if (p != end) {
        mpx_scene_valid = 0;
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
    mpx_revision = hdr.revision;
    mpx_scene_valid = 1;
    mpx_present();
    return MICROPIXEL_STATUS_OK;
}
