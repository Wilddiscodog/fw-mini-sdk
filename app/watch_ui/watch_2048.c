/*
 * 2048 —— 滑动合并同数
 */
#include "app_config.h"
#include "system/includes.h"
#include "lvgl.h"
#include "lcd_config.h"
#include "watch_2048.h"
#include "watch_style.h"
#include "watch_ui.h"
#include "watch_power.h"
#include "watch_game_bgm.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define TOP_H  28
#define GW     LCD_W
#define GH     (LCD_H - TOP_H)
#define N      4
#define CELL   48
#define GAP    4
#define BOARD  (N * CELL + (N + 1) * GAP)
#define OX     ((GW - BOARD) / 2)
#define OY     ((GH - BOARD) / 2)

enum { ST_HELP = 0, ST_PLAY, ST_OVER, ST_WIN };

static const u32 TILE_RGB[] = {
    0xCDC1B4, /* 0 empty-ish */
    0xEEE4DA, 0xEDE0C8, 0xF2B179, 0xF59563,
    0xF67C5F, 0xF65E3B, 0xEDCF72, 0xEDCC61,
    0xEDC850, 0xEDC53F, 0xEDC22E, 0x3C3A32,
};

static struct {
    lv_obj_t *root, *canvas, *lbl, *help, *over;
    lv_color_t *buf;
    u16 grid[N][N];
    u32 score;
    u8 st;
    s16 tx0, ty0;
    u8 swiping;
} G;

static lv_color_t C(u32 rgb)
{
    return lv_color_make((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

static void fill_rect(lv_color_t *fb, s16 x, s16 y, s16 w, s16 h, lv_color_t c)
{
    s16 i, j;
    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            s16 xx = (s16)(x + i), yy = (s16)(y + j);
            if ((u16)xx < GW && (u16)yy < GH) {
                fb[yy * GW + xx] = c;
            }
        }
    }
}

static int log2_tile(u16 v)
{
    int n = 0;
    while (v > 1) {
        v >>= 1;
        n++;
    }
    return n;
}

static void spawn(void)
{
    int empties[N * N];
    int n = 0, i, r, c, pick;

    for (r = 0; r < N; r++) {
        for (c = 0; c < N; c++) {
            if (!G.grid[r][c]) {
                empties[n++] = r * N + c;
            }
        }
    }
    if (!n) {
        return;
    }
    pick = empties[rand() % n];
    G.grid[pick / N][pick % N] = (rand() % 10 == 0) ? 4 : 2;
}

static int can_move(void)
{
    int r, c;
    for (r = 0; r < N; r++) {
        for (c = 0; c < N; c++) {
            if (!G.grid[r][c]) {
                return 1;
            }
            if (c + 1 < N && G.grid[r][c] == G.grid[r][c + 1]) {
                return 1;
            }
            if (r + 1 < N && G.grid[r][c] == G.grid[r + 1][c]) {
                return 1;
            }
        }
    }
    return 0;
}

static int slide_line(u16 line[N], u32 *add)
{
    u16 tmp[N];
    int i, w = 0, changed = 0;

    memset(tmp, 0, sizeof(tmp));
    for (i = 0; i < N; i++) {
        if (line[i]) {
            tmp[w++] = line[i];
        }
    }
    for (i = 0; i < N - 1; i++) {
        if (tmp[i] && tmp[i] == tmp[i + 1]) {
            tmp[i] = (u16)(tmp[i] * 2);
            *add += tmp[i];
            tmp[i + 1] = 0;
            {
                int j;
                for (j = i + 1; j < N - 1; j++) {
                    tmp[j] = tmp[j + 1];
                }
                tmp[N - 1] = 0;
            }
        }
    }
    for (i = 0; i < N; i++) {
        if (line[i] != tmp[i]) {
            changed = 1;
        }
        line[i] = tmp[i];
    }
    return changed;
}

/* dir: 0左 1右 2上 3下 */
static void do_move(int dir)
{
    int r, c, changed = 0;
    u16 line[N];
    u32 add = 0;

    if (G.st != ST_PLAY) {
        return;
    }
    if (dir == 0 || dir == 1) {
        for (r = 0; r < N; r++) {
            for (c = 0; c < N; c++) {
                line[c] = G.grid[r][dir == 0 ? c : (N - 1 - c)];
            }
            if (slide_line(line, &add)) {
                changed = 1;
            }
            for (c = 0; c < N; c++) {
                G.grid[r][dir == 0 ? c : (N - 1 - c)] = line[c];
            }
        }
    } else {
        for (c = 0; c < N; c++) {
            for (r = 0; r < N; r++) {
                line[r] = G.grid[dir == 2 ? r : (N - 1 - r)][c];
            }
            if (slide_line(line, &add)) {
                changed = 1;
            }
            for (r = 0; r < N; r++) {
                G.grid[dir == 2 ? r : (N - 1 - r)][c] = line[r];
            }
        }
    }
    if (!changed) {
        return;
    }
    G.score += add;
    if (G.lbl) {
        lv_label_set_text_fmt(G.lbl, "%u", G.score);
    }
    spawn();
    for (r = 0; r < N; r++) {
        for (c = 0; c < N; c++) {
            if (G.grid[r][c] >= 2048) {
                G.st = ST_WIN;
                if (G.over) {
                    lv_obj_clear_flag(G.over, LV_OBJ_FLAG_HIDDEN);
                }
                return;
            }
        }
    }
    if (!can_move()) {
        G.st = ST_OVER;
        if (G.over) {
            lv_obj_clear_flag(G.over, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void soft_reset(void)
{
    memset(G.grid, 0, sizeof(G.grid));
    G.score = 0;
    spawn();
    spawn();
    if (G.lbl) {
        lv_label_set_text(G.lbl, "0");
    }
    if (G.over) {
        lv_obj_add_flag(G.over, LV_OBJ_FLAG_HIDDEN);
    }
}

static void render(void)
{
    lv_color_t *fb = G.buf;
    int r, c;
    s16 x, y;
    char num[8];

    if (!fb) {
        return;
    }
    fill_rect(fb, 0, 0, GW, GH, C(0xFAF8EF));
    fill_rect(fb, OX, OY, BOARD, BOARD, C(0xBBADA0));
    for (r = 0; r < N; r++) {
        for (c = 0; c < N; c++) {
            u16 v = G.grid[r][c];
            int idx = v ? log2_tile(v) : 0;
            u32 rgb = TILE_RGB[idx < 12 ? idx : 12];
            x = (s16)(OX + GAP + c * (CELL + GAP));
            y = (s16)(OY + GAP + r * (CELL + GAP));
            fill_rect(fb, x, y, CELL, CELL, C(rgb));
            if (v) {
                lv_draw_label_dsc_t dsc;
                snprintf(num, sizeof(num), "%u", v);
                lv_draw_label_dsc_init(&dsc);
                dsc.color = (v <= 4) ? C(0x776E65) : C(0xF9F6F2);
                dsc.font = (v >= 1000) ? &lv_font_montserrat_14 : &lv_font_montserrat_20;
                dsc.align = LV_TEXT_ALIGN_CENTER;
                lv_canvas_draw_text(G.canvas, x, (lv_coord_t)(y + CELL / 2 - 10),
                                    CELL, &dsc, num);
            }
        }
    }
    lv_obj_invalidate(G.canvas);
}

static void help_ok(lv_event_t *e)
{
    (void)e;
    if (G.help) {
        lv_obj_add_flag(G.help, LV_OBJ_FLAG_HIDDEN);
    }
    G.st = ST_PLAY;
    soft_reset();
    watch_game_bgm_start(WATCH_BGM_CANDY);
    render();
}

static void over_ok(lv_event_t *e)
{
    (void)e;
    G.st = ST_PLAY;
    soft_reset();
    render();
}

static void canvas_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_get_act();
    lv_point_t p;
    s16 dx, dy, adx, ady;

    if (!indev || G.st != ST_PLAY) {
        return;
    }
    lv_indev_get_point(indev, &p);
    p.y = (lv_coord_t)(p.y - TOP_H);
    if (code == LV_EVENT_PRESSED) {
        G.tx0 = p.x;
        G.ty0 = p.y;
        G.swiping = 0;
    } else if (code == LV_EVENT_RELEASED && !G.swiping) {
        return;
    } else if (code == LV_EVENT_PRESSING || code == LV_EVENT_RELEASED) {
        dx = (s16)(p.x - G.tx0);
        dy = (s16)(p.y - G.ty0);
        adx = dx < 0 ? (s16)(-dx) : dx;
        ady = dy < 0 ? (s16)(-dy) : dy;
        if (!G.swiping && (adx > 24 || ady > 24)) {
            G.swiping = 1;
            if (adx > ady) {
                do_move(dx > 0 ? 1 : 0);
            } else {
                do_move(dy > 0 ? 3 : 2);
            }
            render();
        }
    }
}

static void cleanup(void)
{
    watch_game_bgm_stop();
    if (G.buf) {
        free(G.buf);
        G.buf = NULL;
    }
}
static void del_cb(lv_event_t *e)
{
    (void)e;
    cleanup();
}
static void back_cb(lv_event_t *e)
{
    (void)e;
    watch_ui_close_app();
}

lv_obj_t *app_2048_create(lv_obj_t *parent)
{
    lv_obj_t *root, *bar, *back, *bl, *card, *lab, *ok, *ocard, *ol, *ob;

    memset(&G, 0, sizeof(G));
    root = lv_obj_create(parent);
    lv_obj_set_size(root, LCD_W, LCD_H);
    lv_obj_set_style_bg_color(root, C(0xFAF8EF), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_GESTURE_BUBBLE);
    G.root = root;

    bar = lv_obj_create(root);
    lv_obj_set_size(bar, LCD_W, TOP_H);
    lv_obj_set_style_bg_color(bar, C(0xEDC22E), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_hor(bar, 4, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    back = lv_btn_create(bar);
    lv_obj_set_size(back, 44, 22);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(back, C(0xF67C5F), 0);
    lv_obj_set_style_radius(back, 10, 0);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
    bl = lv_label_create(back);
    lv_obj_set_style_text_font(bl, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(bl, C(0xFFFFFF), 0);
    lv_label_set_text(bl, "返回");
    lv_obj_center(bl);

    G.lbl = lv_label_create(bar);
    lv_obj_set_style_text_font(G.lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(G.lbl, C(0x776E65), 0);
    lv_label_set_text(G.lbl, "0");
    lv_obj_align(G.lbl, LV_ALIGN_RIGHT_MID, -8, 0);

    G.buf = malloc((u32)GW * GH * sizeof(lv_color_t));
    if (!G.buf) {
        watch_ui_toast("内存不足");
        return root;
    }
    G.canvas = lv_canvas_create(root);
    lv_canvas_set_buffer(G.canvas, G.buf, GW, GH, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_size(G.canvas, GW, GH);
    lv_obj_align(G.canvas, LV_ALIGN_TOP_LEFT, 0, TOP_H);
    lv_obj_add_flag(G.canvas, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(G.canvas, canvas_cb, LV_EVENT_ALL, NULL);

    G.help = lv_obj_create(root);
    lv_obj_set_size(G.help, LCD_W, LCD_H);
    lv_obj_set_style_bg_color(G.help, C(0), 0);
    lv_obj_set_style_bg_opa(G.help, LV_OPA_50, 0);
    lv_obj_set_style_border_width(G.help, 0, 0);
    lv_obj_set_style_pad_all(G.help, 0, 0);
    card = lv_obj_create(G.help);
    lv_obj_set_size(card, 250, 140);
    lv_obj_center(card);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_bg_color(card, C(0xFFF8E8), 0);
    lv_obj_set_style_border_width(card, 3, 0);
    lv_obj_set_style_border_color(card, C(0xEDC22E), 0);
    lab = lv_label_create(card);
    lv_obj_set_style_text_font(lab, &lv_font_watch_16_cn, 0);
    lv_obj_set_width(lab, 226);
    lv_label_set_long_mode(lab, LV_LABEL_LONG_WRAP);
    lv_label_set_text(lab, "玩法提示\n上下左右滑动合并\n相同数字合成更大\n合成 2048 通关");
    lv_obj_align(lab, LV_ALIGN_TOP_MID, 0, 8);
    ok = lv_btn_create(card);
    lv_obj_set_size(ok, 120, 32);
    lv_obj_align(ok, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_bg_color(ok, C(0xF2B179), 0);
    lv_obj_set_style_radius(ok, 12, 0);
    lv_obj_set_style_shadow_width(ok, 0, 0);
    lv_obj_add_event_cb(ok, help_ok, LV_EVENT_CLICKED, NULL);
    {
        lv_obj_t *t = lv_label_create(ok);
        lv_obj_set_style_text_font(t, &lv_font_watch_16_cn, 0);
        lv_obj_set_style_text_color(t, C(0xFFFFFF), 0);
        lv_label_set_text(t, "知道了");
        lv_obj_center(t);
    }

    G.over = lv_obj_create(root);
    lv_obj_set_size(G.over, LCD_W, LCD_H);
    lv_obj_set_style_bg_color(G.over, C(0), 0);
    lv_obj_set_style_bg_opa(G.over, LV_OPA_40, 0);
    lv_obj_set_style_border_width(G.over, 0, 0);
    lv_obj_set_style_pad_all(G.over, 0, 0);
    lv_obj_add_flag(G.over, LV_OBJ_FLAG_HIDDEN);
    ocard = lv_obj_create(G.over);
    lv_obj_set_size(ocard, 180, 100);
    lv_obj_center(ocard);
    lv_obj_set_style_radius(ocard, 14, 0);
    lv_obj_set_style_bg_color(ocard, C(0xFFFFFF), 0);
    lv_obj_set_style_border_width(ocard, 3, 0);
    lv_obj_set_style_border_color(ocard, C(0xF67C5F), 0);
    ol = lv_label_create(ocard);
    lv_obj_set_style_text_font(ol, &lv_font_watch_16_cn, 0);
    lv_label_set_text(ol, "再来一局");
    lv_obj_align(ol, LV_ALIGN_TOP_MID, 0, 14);
    ob = lv_btn_create(ocard);
    lv_obj_set_size(ob, 100, 30);
    lv_obj_align(ob, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(ob, C(0xEDC22E), 0);
    lv_obj_set_style_radius(ob, 10, 0);
    lv_obj_set_style_shadow_width(ob, 0, 0);
    lv_obj_add_event_cb(ob, over_ok, LV_EVENT_CLICKED, NULL);
    {
        lv_obj_t *t = lv_label_create(ob);
        lv_obj_set_style_text_font(t, &lv_font_watch_16_cn, 0);
        lv_obj_set_style_text_color(t, C(0xFFFFFF), 0);
        lv_label_set_text(t, "再来");
        lv_obj_center(t);
    }

    G.st = ST_HELP;
    soft_reset();
    render();
    lv_obj_add_event_cb(root, del_cb, LV_EVENT_DELETE, NULL);
    lv_obj_move_foreground(G.help);
    return root;
}

void app_2048_exit(void)
{
    cleanup();
}
