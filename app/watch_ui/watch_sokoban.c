/*
 * 推箱子 —— 点方向按钮推动箱子到目标点
 */
#include "app_config.h"
#include "system/includes.h"
#include "lvgl.h"
#include "lcd_config.h"
#include "watch_sokoban.h"
#include "watch_style.h"
#include "watch_ui.h"
#include "watch_power.h"
#include "watch_game_bgm.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define TOP_H   28
#define GW      LCD_W
#define GH      (LCD_H - TOP_H)
#define COLS    8
#define ROWS    8
#define CELL    22
#define OX      ((GW - COLS * CELL) / 2)
#define OY      4

enum { ST_HELP = 0, ST_PLAY, ST_WIN };

/* #:墙  .:空  @:人  $:箱  *:目标  +:人在目标  =:箱在目标 */
static const char *const LEVELS[] = {
    "########"
    "#@     #"
    "# $$   #"
    "#  * * #"
    "#      #"
    "#  **  #"
    "#      #"
    "########",
    "########"
    "#@  #  #"
    "# $ # *#"
    "#  $#  #"
    "#   $ *#"
    "#  #   #"
    "#* #   #"
    "########",
    "########"
    "#@$*   #"
    "# #$ # #"
    "#  $ * #"
    "# ##$# #"
    "# *  * #"
    "#      #"
    "########",
};
#define LEVEL_N ((int)(sizeof(LEVELS) / sizeof(LEVELS[0])))

static struct {
    lv_obj_t *root, *canvas, *lbl, *help, *win;
    lv_color_t *buf;
    u8 wall[ROWS][COLS];
    u8 goal[ROWS][COLS];
    u8 box[ROWS][COLS];
    s8 px, py;
    u8 st, level;
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

static void load_level(u8 lv)
{
    const char *s;
    int r, c;

    if (lv >= LEVEL_N) {
        lv = (u8)(LEVEL_N - 1);
    }
    G.level = lv;
    s = LEVELS[lv];
    memset(G.wall, 0, sizeof(G.wall));
    memset(G.goal, 0, sizeof(G.goal));
    memset(G.box, 0, sizeof(G.box));
    G.px = 1;
    G.py = 1;
    for (r = 0; r < ROWS; r++) {
        for (c = 0; c < COLS; c++) {
            char ch = s[r * COLS + c];
            if (ch == '#') {
                G.wall[r][c] = 1;
            } else if (ch == '*') {
                G.goal[r][c] = 1;
            } else if (ch == '$') {
                G.box[r][c] = 1;
            } else if (ch == '=') {
                G.box[r][c] = 1;
                G.goal[r][c] = 1;
            } else if (ch == '@') {
                G.px = (s8)c;
                G.py = (s8)r;
            } else if (ch == '+') {
                G.px = (s8)c;
                G.py = (s8)r;
                G.goal[r][c] = 1;
            }
        }
    }
    if (G.lbl) {
        lv_label_set_text_fmt(G.lbl, "LV%d", G.level + 1);
    }
}

static int won(void)
{
    int r, c;
    for (r = 0; r < ROWS; r++) {
        for (c = 0; c < COLS; c++) {
            if (G.goal[r][c] && !G.box[r][c]) {
                return 0;
            }
        }
    }
    return 1;
}

static void try_move(s8 dx, s8 dy)
{
    s8 nx = (s8)(G.px + dx), ny = (s8)(G.py + dy);
    s8 nnx, nny;

    if (G.st != ST_PLAY) {
        return;
    }
    if (nx < 0 || ny < 0 || nx >= COLS || ny >= ROWS || G.wall[ny][nx]) {
        return;
    }
    if (G.box[ny][nx]) {
        nnx = (s8)(nx + dx);
        nny = (s8)(ny + dy);
        if (nnx < 0 || nny < 0 || nnx >= COLS || nny >= ROWS ||
            G.wall[nny][nnx] || G.box[nny][nnx]) {
            return;
        }
        G.box[ny][nx] = 0;
        G.box[nny][nnx] = 1;
    }
    G.px = nx;
    G.py = ny;
    if (won()) {
        G.st = ST_WIN;
        if (G.win) {
            lv_obj_clear_flag(G.win, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void render(void)
{
    lv_color_t *fb = G.buf;
    s16 r, c, x, y;

    if (!fb) {
        return;
    }
    fill_rect(fb, 0, 0, GW, GH, C(0xFFF4E8));
    for (r = 0; r < ROWS; r++) {
        for (c = 0; c < COLS; c++) {
            x = (s16)(OX + c * CELL);
            y = (s16)(OY + r * CELL);
            if (G.wall[r][c]) {
                fill_rect(fb, x, y, CELL - 1, CELL - 1, C(0x8D6E63));
                fill_rect(fb, (s16)(x + 1), (s16)(y + 1), CELL - 3, 2, C(0xBCAAA4));
            } else {
                fill_rect(fb, x, y, CELL - 1, CELL - 1, C(0xFFE0B2));
                if (G.goal[r][c]) {
                    fill_rect(fb, (s16)(x + 6), (s16)(y + 6), CELL - 13, CELL - 13, C(0x81C784));
                }
                if (G.box[r][c]) {
                    fill_rect(fb, (s16)(x + 3), (s16)(y + 3), CELL - 7, CELL - 7,
                              G.goal[r][c] ? C(0x66BB6A) : C(0xFFB74D));
                    fill_rect(fb, (s16)(x + 5), (s16)(y + 5), 4, 3, C(0xFFFFFF));
                }
            }
        }
    }
    x = (s16)(OX + G.px * CELL + 5);
    y = (s16)(OY + G.py * CELL + 5);
    fill_rect(fb, x, y, CELL - 11, CELL - 11, C(0x42A5F5));
    fill_rect(fb, (s16)(x + 2), (s16)(y + 2), 3, 3, C(0xFFFFFF));
    lv_obj_invalidate(G.canvas);
}

static void help_ok(lv_event_t *e)
{
    (void)e;
    if (G.help) {
        lv_obj_add_flag(G.help, LV_OBJ_FLAG_HIDDEN);
    }
    G.st = ST_PLAY;
    load_level(0);
    watch_game_bgm_start(WATCH_BGM_SOFT);
    render();
}

static void win_next(lv_event_t *e)
{
    (void)e;
    if (G.win) {
        lv_obj_add_flag(G.win, LV_OBJ_FLAG_HIDDEN);
    }
    if (G.level + 1 < LEVEL_N) {
        load_level((u8)(G.level + 1));
    } else {
        load_level(0);
        watch_ui_toast("再来");
    }
    G.st = ST_PLAY;
    render();
}

static void btn_dir(lv_event_t *e)
{
    intptr_t d = (intptr_t)lv_event_get_user_data(e);
    s8 dx = 0, dy = 0;
    if (d == 0) {
        dy = -1;
    } else if (d == 1) {
        dy = 1;
    } else if (d == 2) {
        dx = -1;
    } else {
        dx = 1;
    }
    try_move(dx, dy);
    render();
}

static void btn_reset(lv_event_t *e)
{
    (void)e;
    if (G.st == ST_PLAY || G.st == ST_WIN) {
        if (G.win) {
            lv_obj_add_flag(G.win, LV_OBJ_FLAG_HIDDEN);
        }
        load_level(G.level);
        G.st = ST_PLAY;
        render();
    }
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
    } else if (code == LV_EVENT_PRESSING) {
        dx = (s16)(p.x - G.tx0);
        dy = (s16)(p.y - G.ty0);
        adx = dx < 0 ? (s16)(-dx) : dx;
        ady = dy < 0 ? (s16)(-dy) : dy;
        if (!G.swiping && (adx > 20 || ady > 20)) {
            G.swiping = 1;
            if (adx > ady) {
                try_move(dx > 0 ? 1 : -1, 0);
            } else {
                try_move(0, dy > 0 ? 1 : -1);
            }
            G.tx0 = p.x;
            G.ty0 = p.y;
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

static void make_btn(lv_obj_t *p, const char *t, u32 col, lv_coord_t x, lv_coord_t y,
                     lv_coord_t w, lv_coord_t h, lv_event_cb_t cb, void *ud)
{
    lv_obj_t *b = lv_btn_create(p);
    lv_obj_t *l;
    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_bg_color(b, C(col), 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(l, C(0xFFFFFF), 0);
    lv_label_set_text(l, t);
    lv_obj_center(l);
}

lv_obj_t *app_sokoban_create(lv_obj_t *parent)
{
    lv_obj_t *root, *bar, *back, *bl, *card, *lab, *ok, *wcard, *wl, *wn;
    lv_coord_t by;

    memset(&G, 0, sizeof(G));
    root = lv_obj_create(parent);
    lv_obj_set_size(root, LCD_W, LCD_H);
    lv_obj_set_style_bg_color(root, C(0xFFF4E8), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_GESTURE_BUBBLE);
    G.root = root;

    bar = lv_obj_create(root);
    lv_obj_set_size(bar, LCD_W, TOP_H);
    lv_obj_set_style_bg_color(bar, C(0xFFB74D), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_hor(bar, 4, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    back = lv_btn_create(bar);
    lv_obj_set_size(back, 44, 22);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(back, C(0xF57C00), 0);
    lv_obj_set_style_radius(back, 10, 0);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
    bl = lv_label_create(back);
    lv_obj_set_style_text_font(bl, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(bl, C(0xFFFFFF), 0);
    lv_label_set_text(bl, "返回");
    lv_obj_center(bl);

    G.lbl = lv_label_create(bar);
    lv_obj_set_style_text_font(G.lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(G.lbl, C(0xFFFFFF), 0);
    lv_label_set_text(G.lbl, "LV1");
    lv_obj_align(G.lbl, LV_ALIGN_RIGHT_MID, -6, 0);

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

    by = (lv_coord_t)(LCD_H - 40);
    make_btn(root, "上", 0x42A5F5, 110, (lv_coord_t)(by - 36), 70, 32, btn_dir, (void *)0);
    make_btn(root, "下", 0x42A5F5, 110, by, 70, 32, btn_dir, (void *)1);
    make_btn(root, "左", 0x66BB6A, 36, by, 70, 32, btn_dir, (void *)2);
    make_btn(root, "右", 0x66BB6A, 184, by, 70, 32, btn_dir, (void *)3);
    make_btn(root, "复位", 0xEF5350, 4, (lv_coord_t)(TOP_H + 4), 52, 28, btn_reset, NULL);

    G.help = lv_obj_create(root);
    lv_obj_set_size(G.help, LCD_W, LCD_H);
    lv_obj_set_style_bg_color(G.help, C(0), 0);
    lv_obj_set_style_bg_opa(G.help, LV_OPA_50, 0);
    lv_obj_set_style_border_width(G.help, 0, 0);
    lv_obj_set_style_pad_all(G.help, 0, 0);
    lv_obj_clear_flag(G.help, LV_OBJ_FLAG_SCROLLABLE);
    card = lv_obj_create(G.help);
    lv_obj_set_size(card, 250, 150);
    lv_obj_center(card);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_bg_color(card, C(0xFFF8E8), 0);
    lv_obj_set_style_border_width(card, 3, 0);
    lv_obj_set_style_border_color(card, C(0xFFB74D), 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lab = lv_label_create(card);
    lv_obj_set_style_text_font(lab, &lv_font_watch_16_cn, 0);
    lv_obj_set_width(lab, 226);
    lv_label_set_long_mode(lab, LV_LABEL_LONG_WRAP);
    lv_label_set_text(lab, "玩法提示\n推动箱子到绿点\n按钮或滑动控制\n复位可重来本关");
    lv_obj_align(lab, LV_ALIGN_TOP_MID, 0, 8);
    ok = lv_btn_create(card);
    lv_obj_set_size(ok, 120, 32);
    lv_obj_align(ok, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_bg_color(ok, C(0x66BB6A), 0);
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

    G.win = lv_obj_create(root);
    lv_obj_set_size(G.win, LCD_W, LCD_H);
    lv_obj_set_style_bg_color(G.win, C(0), 0);
    lv_obj_set_style_bg_opa(G.win, LV_OPA_40, 0);
    lv_obj_set_style_border_width(G.win, 0, 0);
    lv_obj_set_style_pad_all(G.win, 0, 0);
    lv_obj_add_flag(G.win, LV_OBJ_FLAG_HIDDEN);
    wcard = lv_obj_create(G.win);
    lv_obj_set_size(wcard, 180, 100);
    lv_obj_center(wcard);
    lv_obj_set_style_radius(wcard, 14, 0);
    lv_obj_set_style_bg_color(wcard, C(0xFFFFFF), 0);
    lv_obj_set_style_border_width(wcard, 3, 0);
    lv_obj_set_style_border_color(wcard, C(0x66BB6A), 0);
    wl = lv_label_create(wcard);
    lv_obj_set_style_text_font(wl, &lv_font_watch_16_cn, 0);
    lv_label_set_text(wl, "通关!");
    lv_obj_align(wl, LV_ALIGN_TOP_MID, 0, 12);
    wn = lv_btn_create(wcard);
    lv_obj_set_size(wn, 100, 30);
    lv_obj_align(wn, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(wn, C(0x42A5F5), 0);
    lv_obj_set_style_radius(wn, 10, 0);
    lv_obj_set_style_shadow_width(wn, 0, 0);
    lv_obj_add_event_cb(wn, win_next, LV_EVENT_CLICKED, NULL);
    {
        lv_obj_t *t = lv_label_create(wn);
        lv_obj_set_style_text_font(t, &lv_font_watch_16_cn, 0);
        lv_obj_set_style_text_color(t, C(0xFFFFFF), 0);
        lv_label_set_text(t, "下一关");
        lv_obj_center(t);
    }

    G.st = ST_HELP;
    load_level(0);
    render();
    lv_obj_add_event_cb(root, del_cb, LV_EVENT_DELETE, NULL);
    lv_obj_move_foreground(G.help);
    return root;
}

void app_sokoban_exit(void)
{
    cleanup();
}
