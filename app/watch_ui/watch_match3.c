/*
 * 消消乐 —— 点选相邻交换, 三连消除 (296x240)
 */
#include "app_config.h"
#include "system/includes.h"
#include "lvgl.h"
#include "lcd_config.h"
#include "watch_match3.h"
#include "watch_style.h"
#include "watch_ui.h"
#include "watch_power.h"
#include "watch_game_bgm.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define TOP_H     28
#define GW        LCD_W
#define GH        (LCD_H - TOP_H)
#define TICK_MS   33

#define N         7
#define CELL      26
#define BOARD_W   (N * CELL)
#define BOARD_H   (N * CELL)
#define OX        ((GW - BOARD_W) / 2)
#define OY        ((GH - BOARD_H) / 2 + 4)

#define COLORS    5
#define TARGET0   200

#define COL_BG0   0xFFE8F0
#define COL_BAR   0xFF8FAB
#define COL_FRAME 0xFFB4C8
#define COL_SEL   0xFFFFFF

enum { ST_HELP = 0, ST_PLAY, ST_ANIM, ST_WIN };

static const u32 GEM_RGB[COLORS + 1] = {
    0x000000,
    0xFF6B6B, /* 红 */
    0xFFD166, /* 黄 */
    0x06D6A0, /* 绿 */
    0x4D96FF, /* 蓝 */
    0xA66CFF, /* 紫 */
};

static struct {
    lv_obj_t *root, *canvas, *lbl_score, *lbl_lv, *help, *win;
    lv_color_t *buf;
    lv_timer_t *tick;

    u8 grid[N][N];
    u8 mark[N][N];
    u8 st;
    u8 level;
    u32 score;
    u32 target;
    s8 sel_r, sel_c;
    u8 anim_frames;
    u8 cascading;
} G;

static lv_color_t C(u32 rgb)
{
    return lv_color_make((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

static void put_px(lv_color_t *fb, s16 x, s16 y, lv_color_t c)
{
    if ((u16)x < GW && (u16)y < GH) {
        fb[y * GW + x] = c;
    }
}

static void fill_rect(lv_color_t *fb, s16 x, s16 y, s16 w, s16 h, lv_color_t c)
{
    s16 i, j;
    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            put_px(fb, (s16)(x + i), (s16)(y + j), c);
        }
    }
}

static void draw_gem(lv_color_t *fb, s16 x, s16 y, u8 id, u8 flash)
{
    u32 rgb = GEM_RGB[id];
    u8 r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    s16 m = 2;

    if (flash) {
        fill_rect(fb, x, y, CELL - 1, CELL - 1, C(0xFFFFFF));
        return;
    }
    fill_rect(fb, (s16)(x + m), (s16)(y + m), CELL - 1 - m * 2, CELL - 1 - m * 2, C(rgb));
    fill_rect(fb, (s16)(x + m), (s16)(y + CELL - 4), CELL - 1 - m * 2, 2,
              lv_color_make((u8)(r * 7 / 10), (u8)(g * 7 / 10), (u8)(b * 7 / 10)));
    fill_rect(fb, (s16)(x + m + 2), (s16)(y + m + 2), 5, 3, C(0xFFFFFF));
}

static u8 rand_gem(void)
{
    return (u8)(1 + rand() % COLORS);
}

static int has_match_at(u8 r, u8 c, u8 id)
{
    int n;

    if (c >= 2 && G.grid[r][c - 1] == id && G.grid[r][c - 2] == id) {
        return 1;
    }
    if (r >= 2 && G.grid[r - 1][c] == id && G.grid[r - 2][c] == id) {
        return 1;
    }
    /* 也看右侧/下方形成中段 */
    n = 1;
    if (c + 1 < N && G.grid[r][c + 1] == id) {
        n++;
    }
    if (c >= 1 && G.grid[r][c - 1] == id) {
        n++;
    }
    if (n >= 3) {
        return 1;
    }
    n = 1;
    if (r + 1 < N && G.grid[r + 1][c] == id) {
        n++;
    }
    if (r >= 1 && G.grid[r - 1][c] == id) {
        n++;
    }
    return n >= 3;
}

static void fill_no_match(void)
{
    int r, c;
    u8 id;
    int guard;

    for (r = 0; r < N; r++) {
        for (c = 0; c < N; c++) {
            guard = 0;
            do {
                id = rand_gem();
                G.grid[r][c] = id;
                guard++;
            } while (has_match_at((u8)r, (u8)c, id) && guard < 20);
        }
    }
}

static int find_matches(void)
{
    int r, c, k, found = 0;
    u8 id;

    memset(G.mark, 0, sizeof(G.mark));
    for (r = 0; r < N; r++) {
        c = 0;
        while (c < N) {
            int i;
            id = G.grid[r][c];
            k = 1;
            while (c + k < N && G.grid[r][c + k] == id) {
                k++;
            }
            if (id && k >= 3) {
                found += k;
                for (i = 0; i < k; i++) {
                    G.mark[r][c + i] = 1;
                }
            }
            c += (k > 0 ? k : 1);
        }
    }
    for (c = 0; c < N; c++) {
        r = 0;
        while (r < N) {
            int i;
            id = G.grid[r][c];
            k = 1;
            while (r + k < N && G.grid[r + k][c] == id) {
                k++;
            }
            if (id && k >= 3) {
                found += k;
                for (i = 0; i < k; i++) {
                    G.mark[r + i][c] = 1;
                }
            }
            r += (k > 0 ? k : 1);
        }
    }
    return found;
}

static void apply_gravity(void)
{
    int r, c, wr;

    for (c = 0; c < N; c++) {
        wr = N - 1;
        for (r = N - 1; r >= 0; r--) {
            if (G.grid[r][c]) {
                G.grid[wr][c] = G.grid[r][c];
                if (wr != r) {
                    G.grid[r][c] = 0;
                }
                wr--;
            }
        }
        while (wr >= 0) {
            G.grid[wr][c] = rand_gem();
            wr--;
        }
    }
}

static void clear_marked(int n)
{
    int r, c;

    for (r = 0; r < N; r++) {
        for (c = 0; c < N; c++) {
            if (G.mark[r][c]) {
                G.grid[r][c] = 0;
            }
        }
    }
    G.score += (u32)(n * 10 * (G.level + 1));
    if (G.lbl_score) {
        lv_label_set_text_fmt(G.lbl_score, "%u", G.score);
    }
}

static void soft_reset(void)
{
    G.score = 0;
    G.level = 0;
    G.target = TARGET0;
    G.sel_r = G.sel_c = -1;
    G.anim_frames = 0;
    G.cascading = 0;
    fill_no_match();
    if (G.lbl_score) {
        lv_label_set_text(G.lbl_score, "0");
    }
    if (G.lbl_lv) {
        lv_label_set_text_fmt(G.lbl_lv, "%u", G.target);
    }
    if (G.win) {
        lv_obj_add_flag(G.win, LV_OBJ_FLAG_HIDDEN);
    }
}

static void render(void)
{
    lv_color_t *fb = G.buf;
    s16 x, y, r, c;
    u32 t;

    if (!fb) {
        return;
    }
    for (y = 0; y < GH; y++) {
        t = (u32)y * 256 / (GH ? GH : 1);
        lv_color_t lc = lv_color_make(
            (u8)((0xFF * (256 - t) + 0xE8 * t) >> 8),
            (u8)((0xE8 * (256 - t) + 0xF4 * t) >> 8),
            (u8)((0xF0 * (256 - t) + 0xFF * t) >> 8));
        for (x = 0; x < GW; x++) {
            fb[y * GW + x] = lc;
        }
    }

    fill_rect(fb, OX - 3, OY - 3, BOARD_W + 6, BOARD_H + 6, C(COL_FRAME));
    fill_rect(fb, OX - 1, OY - 1, BOARD_W + 2, BOARD_H + 2, C(0xFFFFFF));

    for (r = 0; r < N; r++) {
        for (c = 0; c < N; c++) {
            s16 gx = (s16)(OX + c * CELL);
            s16 gy = (s16)(OY + r * CELL);
            if ((r + c) & 1) {
                fill_rect(fb, gx, gy, CELL - 1, CELL - 1, C(0xFFF0F5));
            } else {
                fill_rect(fb, gx, gy, CELL - 1, CELL - 1, C(0xF5F8FF));
            }
            if (G.grid[r][c]) {
                u8 flash = (G.st == ST_ANIM && G.mark[r][c] && (G.anim_frames & 1));
                draw_gem(fb, gx, gy, G.grid[r][c], flash);
            }
            if (G.sel_r == r && G.sel_c == c) {
                fill_rect(fb, gx, gy, CELL - 1, 2, C(COL_SEL));
                fill_rect(fb, gx, (s16)(gy + CELL - 3), CELL - 1, 2, C(COL_SEL));
                fill_rect(fb, gx, gy, 2, CELL - 1, C(COL_SEL));
                fill_rect(fb, (s16)(gx + CELL - 3), gy, 2, CELL - 1, C(COL_SEL));
            }
        }
    }
    lv_obj_invalidate(G.canvas);
}

static void try_cascade(void)
{
    int n = find_matches();
    if (n > 0) {
        G.st = ST_ANIM;
        G.anim_frames = 6;
        G.cascading = 1;
        return;
    }
    G.cascading = 0;
    G.st = ST_PLAY;
    if (G.score >= G.target) {
        G.st = ST_WIN;
        if (G.win) {
            lv_obj_clear_flag(G.win, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static int adj(s8 r0, s8 c0, s8 r1, s8 c1)
{
    s8 dr = (s8)(r0 - r1);
    s8 dc = (s8)(c0 - c1);
    if (dr < 0) {
        dr = (s8)(-dr);
    }
    if (dc < 0) {
        dc = (s8)(-dc);
    }
    return (dr + dc) == 1;
}

static void do_swap(s8 r0, s8 c0, s8 r1, s8 c1)
{
    u8 t = G.grid[r0][c0];
    G.grid[r0][c0] = G.grid[r1][c1];
    G.grid[r1][c1] = t;
}

static void on_cell(s8 r, s8 c)
{
    if (G.st != ST_PLAY || r < 0 || c < 0 || r >= N || c >= N) {
        return;
    }
    if (G.sel_r < 0) {
        G.sel_r = r;
        G.sel_c = c;
        return;
    }
    if (G.sel_r == r && G.sel_c == c) {
        G.sel_r = G.sel_c = -1;
        return;
    }
    if (!adj(G.sel_r, G.sel_c, r, c)) {
        G.sel_r = r;
        G.sel_c = c;
        return;
    }
    do_swap(G.sel_r, G.sel_c, r, c);
    if (find_matches() == 0) {
        do_swap(G.sel_r, G.sel_c, r, c); /* 无效交换还原 */
        watch_ui_toast("不能消");
        G.sel_r = G.sel_c = -1;
        return;
    }
    G.sel_r = G.sel_c = -1;
    try_cascade();
}

static void match_tick(lv_timer_t *t)
{
    (void)t;
    if (!watch_power_input_enabled()) {
        return;
    }
    if (G.st == ST_ANIM) {
        if (G.anim_frames > 0) {
            G.anim_frames--;
        } else {
            int n = 0, r, c;
            for (r = 0; r < N; r++) {
                for (c = 0; c < N; c++) {
                    if (G.mark[r][c]) {
                        n++;
                    }
                }
            }
            clear_marked(n);
            apply_gravity();
            try_cascade();
        }
    }
    render();
}

static void canvas_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev;
    lv_point_t p;
    s8 r, c;

    if (code != LV_EVENT_CLICKED || G.st != ST_PLAY) {
        return;
    }
    if (!watch_power_input_enabled()) {
        return;
    }
    indev = lv_indev_get_act();
    if (!indev) {
        return;
    }
    lv_indev_get_point(indev, &p);
    p.y = (lv_coord_t)(p.y - TOP_H);
    c = (s8)((p.x - OX) / CELL);
    r = (s8)((p.y - OY) / CELL);
    on_cell(r, c);
    render();
}

static void help_ok_cb(lv_event_t *e)
{
    (void)e;
    if (!watch_power_input_enabled()) {
        return;
    }
    if (G.help) {
        lv_obj_add_flag(G.help, LV_OBJ_FLAG_HIDDEN);
    }
    G.st = ST_PLAY;
    soft_reset();
    watch_game_bgm_start(WATCH_BGM_CANDY);
    render();
}

static void win_next_cb(lv_event_t *e)
{
    (void)e;
    if (!watch_power_input_enabled()) {
        return;
    }
    G.level++;
    G.target = TARGET0 + G.level * 150;
    G.score = 0;
    G.sel_r = G.sel_c = -1;
    fill_no_match();
    if (G.lbl_score) {
        lv_label_set_text(G.lbl_score, "0");
    }
    if (G.lbl_lv) {
        lv_label_set_text_fmt(G.lbl_lv, "%u", G.target);
    }
    if (G.win) {
        lv_obj_add_flag(G.win, LV_OBJ_FLAG_HIDDEN);
    }
    G.st = ST_PLAY;
    watch_ui_toast("新目标!");
    render();
}

static void app_match3_cleanup(void);

static void match_del_cb(lv_event_t *e)
{
    (void)e;
    app_match3_cleanup();
}

static void match_back_cb(lv_event_t *e)
{
    (void)e;
    watch_ui_close_app();
}

lv_obj_t *app_match3_create(lv_obj_t *parent)
{
    lv_obj_t *root, *bar, *back, *bl, *card, *lab, *ok;
    lv_obj_t *wcard, *wlab, *wnext;

    memset(&G, 0, sizeof(G));
    G.sel_r = G.sel_c = -1;

    root = lv_obj_create(parent);
    lv_obj_set_size(root, LCD_W, LCD_H);
    lv_obj_set_style_bg_color(root, C(COL_BG0), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_GESTURE_BUBBLE);
    G.root = root;

    bar = lv_obj_create(root);
    lv_obj_set_size(bar, LCD_W, TOP_H);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(bar, C(COL_BAR), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_hor(bar, 4, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    back = lv_btn_create(bar);
    lv_obj_set_size(back, 44, 22);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(back, C(0xFF6B8A), 0);
    lv_obj_set_style_radius(back, 10, 0);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_add_event_cb(back, match_back_cb, LV_EVENT_CLICKED, NULL);
    bl = lv_label_create(back);
    lv_obj_set_style_text_font(bl, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(bl, C(0xFFFFFF), 0);
    lv_label_set_text(bl, "返回");
    lv_obj_center(bl);

    G.lbl_score = lv_label_create(bar);
    lv_obj_set_style_text_font(G.lbl_score, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(G.lbl_score, C(0xFFFFFF), 0);
    lv_label_set_text(G.lbl_score, "0");
    lv_obj_align(G.lbl_score, LV_ALIGN_CENTER, 0, 0);

    G.lbl_lv = lv_label_create(bar);
    lv_obj_set_style_text_font(G.lbl_lv, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(G.lbl_lv, C(0xFFF8E8), 0);
    lv_label_set_text(G.lbl_lv, "200");
    lv_obj_align(G.lbl_lv, LV_ALIGN_RIGHT_MID, -6, 0);

    G.buf = (lv_color_t *)malloc((u32)GW * GH * sizeof(lv_color_t));
    if (!G.buf) {
        watch_ui_toast("内存不足");
        return root;
    }
    G.canvas = lv_canvas_create(root);
    lv_canvas_set_buffer(G.canvas, G.buf, GW, GH, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_size(G.canvas, GW, GH);
    lv_obj_align(G.canvas, LV_ALIGN_TOP_LEFT, 0, TOP_H);
    lv_obj_clear_flag(G.canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(G.canvas, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(G.canvas, canvas_cb, LV_EVENT_CLICKED, NULL);

    G.help = lv_obj_create(root);
    lv_obj_set_size(G.help, LCD_W, LCD_H);
    lv_obj_set_style_bg_color(G.help, C(0x000000), 0);
    lv_obj_set_style_bg_opa(G.help, LV_OPA_50, 0);
    lv_obj_set_style_border_width(G.help, 0, 0);
    lv_obj_set_style_pad_all(G.help, 0, 0);
    lv_obj_clear_flag(G.help, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(G.help, LV_OBJ_FLAG_CLICKABLE);

    card = lv_obj_create(G.help);
    lv_obj_set_size(card, 250, 160);
    lv_obj_center(card);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_bg_color(card, C(0xFFF8E8), 0);
    lv_obj_set_style_border_width(card, 3, 0);
    lv_obj_set_style_border_color(card, C(0xFF8FAB), 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lab = lv_label_create(card);
    lv_obj_set_style_text_font(lab, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(lab, C(WATCH_COL_TEXT), 0);
    lv_obj_set_width(lab, 226);
    lv_label_set_long_mode(lab, LV_LABEL_LONG_WRAP);
    lv_label_set_text(lab,
                      "玩法提示\n"
                      "点选相邻两格交换\n"
                      "三同色连消除得分\n"
                      "达到右上目标即通关");
    lv_obj_align(lab, LV_ALIGN_TOP_MID, 0, 4);

    ok = lv_btn_create(card);
    lv_obj_set_size(ok, 120, 34);
    lv_obj_align(ok, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_radius(ok, 14, 0);
    lv_obj_set_style_bg_color(ok, C(0x06D6A0), 0);
    lv_obj_set_style_shadow_width(ok, 0, 0);
    lv_obj_add_event_cb(ok, help_ok_cb, LV_EVENT_CLICKED, NULL);
    {
        lv_obj_t *t = lv_label_create(ok);
        lv_obj_set_style_text_font(t, &lv_font_watch_16_cn, 0);
        lv_obj_set_style_text_color(t, C(0xFFFFFF), 0);
        lv_label_set_text(t, "知道了");
        lv_obj_center(t);
    }

    G.win = lv_obj_create(root);
    lv_obj_set_size(G.win, LCD_W, LCD_H);
    lv_obj_set_style_bg_color(G.win, C(0x000000), 0);
    lv_obj_set_style_bg_opa(G.win, LV_OPA_40, 0);
    lv_obj_set_style_border_width(G.win, 0, 0);
    lv_obj_set_style_pad_all(G.win, 0, 0);
    lv_obj_clear_flag(G.win, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(G.win, LV_OBJ_FLAG_HIDDEN);

    wcard = lv_obj_create(G.win);
    lv_obj_set_size(wcard, 200, 110);
    lv_obj_center(wcard);
    lv_obj_set_style_radius(wcard, 16, 0);
    lv_obj_set_style_bg_color(wcard, C(0xFFFFFF), 0);
    lv_obj_set_style_border_width(wcard, 3, 0);
    lv_obj_set_style_border_color(wcard, C(0xFF8FAB), 0);
    lv_obj_clear_flag(wcard, LV_OBJ_FLAG_SCROLLABLE);

    wlab = lv_label_create(wcard);
    lv_obj_set_style_text_font(wlab, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(wlab, C(WATCH_COL_TEXT), 0);
    lv_label_set_text(wlab, "通关!");
    lv_obj_align(wlab, LV_ALIGN_TOP_MID, 0, 14);

    wnext = lv_btn_create(wcard);
    lv_obj_set_size(wnext, 110, 32);
    lv_obj_align(wnext, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_radius(wnext, 12, 0);
    lv_obj_set_style_bg_color(wnext, C(0xA66CFF), 0);
    lv_obj_set_style_shadow_width(wnext, 0, 0);
    lv_obj_add_event_cb(wnext, win_next_cb, LV_EVENT_CLICKED, NULL);
    {
        lv_obj_t *t = lv_label_create(wnext);
        lv_obj_set_style_text_font(t, &lv_font_watch_16_cn, 0);
        lv_obj_set_style_text_color(t, C(0xFFFFFF), 0);
        lv_label_set_text(t, "下一关");
        lv_obj_center(t);
    }

    G.st = ST_HELP;
    soft_reset();
    render();
    G.tick = lv_timer_create(match_tick, TICK_MS, NULL);
    lv_obj_add_event_cb(root, match_del_cb, LV_EVENT_DELETE, NULL);
    lv_obj_move_foreground(G.help);
    return root;
}

void app_match3_exit(void)
{
    app_match3_cleanup();
}

static void app_match3_cleanup(void)
{
    watch_game_bgm_stop();
    if (G.tick) {
        lv_timer_del(G.tick);
        G.tick = NULL;
    }
    if (G.buf) {
        free(G.buf);
        G.buf = NULL;
    }
}
