/* app/mpx/mpx_raster.c - 见 mpx_raster.h
 *
 * 面板(JD9853)高字节在前。缓冲里的 uint16 保持 canonical,
 * mpx_put 先写高字节,和场景通道一致。
 */
#include "mpx_raster.h"

#include <stdlib.h>
#include <string.h>

#include "micropixel_abi.h"
#include "mpx_display.h"

#define MPX_TAG "[mpx] "

#define MPX_RT_SLOTS 16
#define MPX_RP_SLOTS 8
#define MPX_RP_MAX_LIGHTS 32 /* ABI 上限; Tomb 用 16 档 */
#define MPX_MAX_TEX_DIM 4096

extern void wdt_clear(void);

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
    /* guest 按小端字节给出 canonical RGB565,数值保持不交换。 */
    for (uint32_t i = 0; i < length / 2; i++) {
        buffer[i] = (uint16_t)(entries[i * 2] | (entries[i * 2 + 1] << 8));
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

/* 字节序只在 mpx_put / mpx_get。混合和调色板都用 canonical 数值。 */
static inline uint16_t mpx_canon_to_panel(uint16_t canonical)
{
    return canonical;
}

static inline uint16_t mpx_panel_to_canon(uint16_t panel)
{
    return panel;
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

/* 沿边扫描。顶点 x/y 是 12.4,走边时换成 16.16;u/v 从 8.8 升到 16.16。
 * 每条边、每一行用一次浮点倒数求步长,像素循环只做整数加法。 */
typedef struct mpx_edge {
    int32_t x;
    int32_t u;
    int32_t v;
    int32_t light;
    int32_t dx;
    int32_t du;
    int32_t dv;
    int32_t dlight;
    int32_t end_row;
} mpx_edge_t;

typedef struct mpx_chain {
    const micropixel_raster_vertex_t *vertices;
    int count;
    int index;
    int remaining;
    int forward;
    mpx_edge_t edge;
} mpx_chain_t;

static int mpx_is_pow2(uint16_t v)
{
    return v != 0 && (v & (uint16_t)(v - 1)) == 0;
}

static int mpx_row_ceil(int32_t y)
{
    return (y + 7) >> 4;
}

static int mpx_col_ceil(int32_t x)
{
    return (x + 0x7fff) >> 16;
}

static int32_t mpx_walk_at(int32_t start, int32_t step, int64_t num, int shift)
{
    return start + (int32_t)(((int64_t)step * num) >> shift);
}

static int mpx_setup_edge(const micropixel_raster_vertex_t *a, const micropixel_raster_vertex_t *b,
                          int32_t start_row, mpx_edge_t *edge)
{
    int32_t dy = (int32_t)b->y - (int32_t)a->y;
    int32_t first;
    float per_row;
    int32_t u0, v0, l0;
    int64_t rows16;
    if (dy <= 0) {
        return 0;
    }
    first = mpx_row_ceil(a->y);
    if (first < start_row) {
        first = start_row;
    }
    edge->end_row = mpx_row_ceil(b->y);
    if (edge->end_row <= first) {
        return 0;
    }
    per_row = 16.0f / (float)dy;
    edge->dx = (int32_t)((float)((int32_t)b->x - (int32_t)a->x) * per_row * 4096.0f);
    u0 = (int32_t)a->u << 8;
    v0 = (int32_t)a->v << 8;
    l0 = (int32_t)a->light << 16;
    edge->du = (int32_t)((float)(((int32_t)b->u << 8) - u0) * per_row);
    edge->dv = (int32_t)((float)(((int32_t)b->v << 8) - v0) * per_row);
    edge->dlight = (int32_t)((float)(((int32_t)b->light << 16) - l0) * per_row);
    rows16 = ((int64_t)first << 4) + 8 - a->y;
    edge->x = mpx_walk_at((int32_t)a->x << 12, edge->dx, rows16, 4);
    edge->u = mpx_walk_at(u0, edge->du, rows16, 4);
    edge->v = mpx_walk_at(v0, edge->dv, rows16, 4);
    edge->light = mpx_walk_at(l0, edge->dlight, rows16, 4);
    return 1;
}

static int mpx_chain_advance(mpx_chain_t *chain, int32_t row)
{
    while (chain->remaining > 0) {
        int next;
        chain->remaining--;
        if (chain->forward) {
            next = chain->index + 1;
            if (next == chain->count) {
                next = 0;
            }
        } else {
            next = chain->index == 0 ? chain->count - 1 : chain->index - 1;
        }
        {
            int covers = mpx_setup_edge(&chain->vertices[chain->index], &chain->vertices[next],
                                        row, &chain->edge);
            chain->index = next;
            if (covers) {
                return 1;
            }
        }
    }
    return 0;
}

static int64_t mpx_area2(const micropixel_raster_vertex_t *verts, int count)
{
    int64_t area = 0;
    int i;
    for (i = 0; i < count; i++) {
        const micropixel_raster_vertex_t *a = &verts[i];
        const micropixel_raster_vertex_t *b = &verts[(i + 1) % count];
        area += (int64_t)a->x * b->y - (int64_t)b->x * a->y;
    }
    return area;
}

static int32_t mpx_check_polygon(uint8_t flags, uint8_t texture_slot, uint8_t palette_slot,
                                 const micropixel_raster_vertex_t *verts, int count)
{
    uint8_t known = (uint8_t)(MICROPIXEL_RASTER_POLYGON_TRANSPARENT_INDEX0 |
                              MICROPIXEL_RASTER_POLYGON_FLAT_COLOR);
    if ((flags & (uint8_t)~known) != 0 || count < 3) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
    if (palette_slot >= MPX_RP_SLOTS || !mpx_rp[palette_slot].used) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
    int flat = (flags & MICROPIXEL_RASTER_POLYGON_FLAT_COLOR) != 0;
    if (!flat) {
        if (texture_slot >= MPX_RT_SLOTS || !mpx_rt[texture_slot].used) {
            return MICROPIXEL_STATUS_INVALID_ARGUMENT;
        }
        const mpx_rt_t *tex = &mpx_rt[texture_slot];
        if (tex->layout != MICROPIXEL_RASTER_LAYOUT_ROW_MAJOR ||
            !mpx_is_pow2(tex->width) || !mpx_is_pow2(tex->height)) {
            return MICROPIXEL_STATUS_INVALID_ARGUMENT;
        }
    }
    for (int i = 0; i < count; i++) {
        if (verts[i].light >= mpx_rp[palette_slot].light_levels) {
            return MICROPIXEL_STATUS_INVALID_ARGUMENT;
        }
    }
    return MICROPIXEL_STATUS_OK;
}

static void mpx_exec_polygon(const micropixel_raster_vertex_t *verts, int count, uint8_t flags,
                             uint8_t texture_slot, uint8_t palette_slot, const mpx_target_t *target)
{
    int64_t area;
    int top;
    int32_t min_y, max_y;
    uint32_t min_light, max_light;
    int row, row_end;
    mpx_chain_t left, right;
    int flat, transparent;
    uint32_t flat_index;
    int32_t light_low, light_high;
    const mpx_rt_t *tex;
    const uint16_t *pal;
    int tw, th;
    int i;

    if (count < 3 || count > 4) {
        return;
    }
    area = mpx_area2(verts, count);
    if (area == 0) {
        return;
    }
    flat = (flags & MICROPIXEL_RASTER_POLYGON_FLAT_COLOR) != 0;
    transparent = (flags & MICROPIXEL_RASTER_POLYGON_TRANSPARENT_INDEX0) != 0;
    top = 0;
    min_y = max_y = verts[0].y;
    min_light = max_light = verts[0].light;
    for (i = 1; i < count; i++) {
        if (verts[i].y < min_y) {
            min_y = verts[i].y;
            top = i;
        }
        if (verts[i].y > max_y) {
            max_y = verts[i].y;
        }
        if (verts[i].light < min_light) {
            min_light = verts[i].light;
        }
        if (verts[i].light > max_light) {
            max_light = verts[i].light;
        }
    }
    if (max_light >= mpx_rp[palette_slot].light_levels) {
        return;
    }
    row = mpx_row_ceil(min_y);
    if (row < 0) {
        row = 0;
    }
    row_end = mpx_row_ceil(max_y);
    if (row_end > (int)target->height) {
        row_end = (int)target->height;
    }
    if (row >= row_end) {
        return;
    }
    left.vertices = verts;
    left.count = count;
    left.index = top;
    left.remaining = count - 1;
    left.forward = area < 0;
    right = left;
    right.forward = area > 0;
    if (!mpx_chain_advance(&left, row) || !mpx_chain_advance(&right, row)) {
        return;
    }
    light_low = (int32_t)(min_light << 16);
    light_high = (int32_t)((max_light << 16) | 0xffffu);
    flat_index = flat ? ((uint32_t)verts[0].u >> 8) : 0;
    tex = flat ? NULL : &mpx_rt[texture_slot];
    pal = mpx_rp[palette_slot].entries;
    tw = tex ? (int)tex->width : 0;
    th = tex ? (int)tex->height : 0;

    for (;;) {
        mpx_edge_t *l = &left.edge;
        mpx_edge_t *r = &right.edge;
        int32_t span, x0, x1;
        if (l->x > r->x) {
            mpx_edge_t *swap = l;
            l = r;
            r = swap;
        }
        span = r->x - l->x;
        x0 = mpx_col_ceil(l->x);
        x1 = mpx_col_ceil(r->x);
        if (x0 < 0) {
            x0 = 0;
        }
        if (x1 > (int)target->width) {
            x1 = (int)target->width;
        }
        if (span > 0 && x1 > x0) {
            float per_pixel = 65536.0f / (float)span;
            int32_t du = (int32_t)((float)(r->u - l->u) * per_pixel);
            int32_t dv = (int32_t)((float)(r->v - l->v) * per_pixel);
            int32_t dlight = (int32_t)((float)(r->light - l->light) * per_pixel);
            int64_t prestep = ((int64_t)x0 << 16) + 0x8000 - l->x;
            int32_t u = mpx_walk_at(l->u, du, prestep, 16);
            int32_t v = mpx_walk_at(l->v, dv, prestep, 16);
            int32_t light = mpx_walk_at(l->light, dlight, prestep, 16);
            int last = (x1 - x0) - 1;
            int32_t light_end;
            uint8_t *rowptr = target->data + (uint32_t)row * target->pitch;
            int px;
            if (light < light_low) {
                light = light_low;
            }
            if (light > light_high) {
                light = light_high;
            }
            light_end = light + (int32_t)((int64_t)dlight * last);
            if (light_end < light_low || light_end > light_high) {
                if (light_end < light_low) {
                    light_end = light_low;
                }
                if (light_end > light_high) {
                    light_end = light_high;
                }
                dlight = last > 0 ? (light_end - light) / last : 0;
            }
            for (px = x0; px < x1; px++) {
                uint32_t lit = (uint32_t)light >> 16;
                uint32_t index;
                if (flat) {
                    index = flat_index;
                } else {
                    uint32_t ui = ((uint32_t)u >> 16) & (uint32_t)(tw - 1);
                    uint32_t vi = ((uint32_t)v >> 16) & (uint32_t)(th - 1);
                    index = tex->texels[vi * (uint32_t)tw + ui];
                }
                if (!(transparent && index == 0)) {
                    mpx_put(rowptr, (uint32_t)px, pal[(lit << 8) | index]);
                }
                u += du;
                v += dv;
                light += dlight;
            }
        }
        if (++row >= row_end) {
            break;
        }
        if ((row & 7) == 0) {
            wdt_clear();
        }
        if (row == left.edge.end_row) {
            if (!mpx_chain_advance(&left, row)) {
                break;
            }
        } else {
            left.edge.x += left.edge.dx;
            left.edge.u += left.edge.du;
            left.edge.v += left.edge.dv;
            left.edge.light += left.edge.dlight;
        }
        if (row == right.edge.end_row) {
            if (!mpx_chain_advance(&right, row)) {
                break;
            }
        } else {
            right.edge.x += right.edge.dx;
            right.edge.u += right.edge.du;
            right.edge.v += right.edge.dv;
            right.edge.light += right.edge.dlight;
        }
    }
}

static uint32_t mpx_record_size(uint8_t type)
{
    switch (type) {
    case MICROPIXEL_RASTER_RECORD_RECT:
        return sizeof(micropixel_raster_rect_t);
    case MICROPIXEL_RASTER_RECORD_SPRITE:
        return sizeof(micropixel_raster_sprite_t);
    case MICROPIXEL_RASTER_RECORD_TRIANGLE:
        return sizeof(micropixel_raster_triangle_t);
    case MICROPIXEL_RASTER_RECORD_QUAD:
        return sizeof(micropixel_raster_quad_t);
    default:
        return 0;
    }
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
    case MICROPIXEL_RASTER_RECORD_TRIANGLE: {
        if (rec_size != sizeof(micropixel_raster_triangle_t)) {
            return MICROPIXEL_STATUS_INVALID_ARGUMENT;
        }
        micropixel_raster_triangle_t tri;
        memcpy(&tri, rec, sizeof(tri));
        return mpx_check_polygon(tri.flags, tri.texture_slot, tri.palette_slot, tri.vertices, 3);
    }
    case MICROPIXEL_RASTER_RECORD_QUAD: {
        if (rec_size != sizeof(micropixel_raster_quad_t)) {
            return MICROPIXEL_STATUS_INVALID_ARGUMENT;
        }
        micropixel_raster_quad_t quad;
        memcpy(&quad, rec, sizeof(quad));
        return mpx_check_polygon(quad.flags, quad.texture_slot, quad.palette_slot, quad.vertices, 4);
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
        /* 每型尺寸为定长。必须按本条长度校验,不能拿剩余字节去比。 */
        uint32_t rec_size = mpx_record_size(rec[0]);
        if (rec_size == 0) {
            return MICROPIXEL_STATUS_UNSUPPORTED;
        }
        if (rec + rec_size > end) {
            return MICROPIXEL_STATUS_INVALID_ARGUMENT;
        }
        int32_t status2 = mpx_validate_record(rec, rec_size);
        if (status2 != MICROPIXEL_STATUS_OK) {
            return status2;
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
        } else if (rec[0] == MICROPIXEL_RASTER_RECORD_SPRITE) {
            mpx_exec_sprite(rec, &target);
        } else if (rec[0] == MICROPIXEL_RASTER_RECORD_TRIANGLE) {
            micropixel_raster_triangle_t tri;
            memcpy(&tri, rec, sizeof(tri));
            mpx_exec_polygon(tri.vertices, 3, tri.flags, tri.texture_slot, tri.palette_slot, &target);
        } else {
            micropixel_raster_quad_t quad;
            memcpy(&quad, rec, sizeof(quad));
            mpx_exec_polygon(quad.vertices, 4, quad.flags, quad.texture_slot, quad.palette_slot, &target);
        }
        rec += mpx_record_size(rec[0]);
    }
    return MICROPIXEL_STATUS_OK;
}
