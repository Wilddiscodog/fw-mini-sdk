/*
 * 倾斜迷宫 —— 陀螺仪控球滚到终点 (296x240)
 */
#include "app_config.h"
#include "system/includes.h"
#include "lvgl.h"
#include "lcd_config.h"
#include "watch_maze.h"
#include "watch_style.h"
#include "watch_ui.h"
#include "watch_power.h"
#include "watch_game_bgm.h"
#include "imu.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#if (defined TCFG_IMU_ENABLE) && TCFG_IMU_ENABLE

#define TOP_H     28
#define GW        LCD_W
#define GH        (LCD_H - TOP_H)
#define TICK_MS   33

#define COLS      15
#define ROWS      11
#define CELL      18
#define BOARD_W   (COLS * CELL)
#define BOARD_H   (ROWS * CELL)
#define OX        ((GW - BOARD_W) / 2)
#define OY        ((GH - BOARD_H) / 2)

#define BALL_R    5
#define ACC       900.0f
#define FRICTION  0.955f
#define MAX_V     160.0f

#define COL_BG    0x1A2744
#define COL_WALL  0x5B8DEF
#define COL_FLOOR 0x243656
#define COL_GOAL  0x06D6A0
#define COL_BALL  0xFFD54F
#define COL_BAR   0xFF8FAB

enum { ST_HELP = 0, ST_PLAY, ST_WIN, ST_OVER };

/* #=墙 .=空 S=起点 G=终点 */
static const char *const LEVELS[] = {
    /* LV1 */
    "###############"
    "#S....#.......#"
    "###.#.#.#####.#"
    "#...#.#.....#.#"
    "#.###.#####.#.#"
    "#.#.......#...#"
    "#.#.#####.###.#"
    "#.#.#.....#...#"
    "#.#.#.#####.#.#"
    "#...#.......#G#"
    "###############",
    /* LV2 */
    "###############"
    "#S#...........#"
    "#.#.#########.#"
    "#.#.#.....#...#"
    "#.#.#.###.#.###"
    "#...#.#.#.#...#"
    "#####.#.#.###.#"
    "#.....#.#.....#"
    "#.#####.#####.#"
    "#...........#G#"
    "###############",
    /* LV3 */
    "###############"
    "#S..#.....#...#"
    "###.#.###.#.#.#"
    "#...#.#...#.#.#"
    "#.###.#.###.#.#"
    "#.#...#.#...#.#"
    "#.#.#####.###.#"
    "#.#.......#...#"
    "#.#########.#.#"
    "#...........#G#"
    "###############",
};

#define LEVEL_N  ((int)(sizeof(LEVELS) / sizeof(LEVELS[0])))

static struct {
    lv_obj_t *root, *canvas, *lbl_lv, *lbl_time, *help, *win;
    lv_color_t *buf;
    lv_timer_t *tick;

    u8 map[ROWS][COLS];
    u8 st;
    u8 level;
    u16 remain_s;
    u16 tick_acc;

    float bx, by, vx, vy;
    float zero_ax, zero_ay;
    u8 zeroed;
    s16 gx, gy; /* 终点格 */
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

static void fill_circle(lv_color_t *fb, s16 cx, s16 cy, u16 r, lv_color_t c)
{
    s16 dy, dx, x0, x1;
    for (dy = -(s16)r; dy <= (s16)r; dy++) {
        s16 yy = (s16)(cy + dy);
        if ((u16)yy >= GH) {
            continue;
        }
        dx = (s16)(sqrtf((float)(r * r - dy * dy)) + 0.5f);
        x0 = (s16)(cx - dx);
        x1 = (s16)(cx + dx);
        if (x0 < 0) {
            x0 = 0;
        }
        if (x1 >= GW) {
            x1 = GW - 1;
        }
        for (; x0 <= x1; x0++) {
            fb[yy * GW + x0] = c;
        }
    }
}

static int cell_wall(s16 cx, s16 cy)
{
    if (cx < 0 || cy < 0 || cx >= COLS || cy >= ROWS) {
        return 1;
    }
    return G.map[cy][cx] == 1;
}

static void load_level(u8 lv)
{
    const char *src;
    int r, c;
    s16 sx = 1, sy = 1;

    if (lv >= LEVEL_N) {
        lv = (u8)(LEVEL_N - 1);
    }
    G.level = lv;
    src = LEVELS[lv];
    G.gx = COLS - 2;
    G.gy = ROWS - 2;
    for (r = 0; r < ROWS; r++) {
        for (c = 0; c < COLS; c++) {
            char ch = src[r * COLS + c];
            if (ch == '#') {
                G.map[r][c] = 1;
            } else if (ch == 'S') {
                G.map[r][c] = 0;
                sx = (s16)c;
                sy = (s16)r;
            } else if (ch == 'G') {
                G.map[r][c] = 2;
                G.gx = (s16)c;
                G.gy = (s16)r;
            } else {
                G.map[r][c] = 0;
            }
        }
    }
    G.bx = (float)(OX + sx * CELL + CELL / 2);
    G.by = (float)(OY + sy * CELL + CELL / 2);
    G.vx = G.vy = 0;
    G.zeroed = 0;
    G.remain_s = (u16)(55 + lv * 10);
    G.tick_acc = 0;
    if (G.lbl_lv) {
        lv_label_set_text_fmt(G.lbl_lv, "LV%d", G.level + 1);
    }
    if (G.lbl_time) {
        lv_label_set_text_fmt(G.lbl_time, "%ds", G.remain_s);
    }
}

static void render(void)
{
    lv_color_t *fb = G.buf;
    s16 r, c, x, y;

    if (!fb) {
        return;
    }
    fill_rect(fb, 0, 0, GW, GH, C(COL_BG));
    for (r = 0; r < ROWS; r++) {
        for (c = 0; c < COLS; c++) {
            x = (s16)(OX + c * CELL);
            y = (s16)(OY + r * CELL);
            if (G.map[r][c] == 1) {
                fill_rect(fb, x, y, CELL - 1, CELL - 1, C(COL_WALL));
                fill_rect(fb, (s16)(x + 1), (s16)(y + 1), CELL - 3, 2, C(0xA8C8FF));
            } else if (G.map[r][c] == 2) {
                fill_rect(fb, x, y, CELL - 1, CELL - 1, C(0x1B4332));
                fill_rect(fb, (s16)(x + 3), (s16)(y + 3), CELL - 7, CELL - 7, C(COL_GOAL));
            } else {
                fill_rect(fb, x, y, CELL - 1, CELL - 1, C(COL_FLOOR));
            }
        }
    }
    if (G.st == ST_PLAY || G.st == ST_WIN) {
        fill_circle(fb, (s16)G.bx, (s16)G.by, BALL_R, C(COL_BALL));
        fill_circle(fb, (s16)(G.bx - 2), (s16)(G.by - 2), 2, C(0xFFFFFF));
    }
    lv_obj_invalidate(G.canvas);
}

static void collide_walls(void)
{
    s16 minc, maxc, minr, maxr, c, r;
    float nx = G.bx, ny = G.by;

    minc = (s16)((G.bx - BALL_R - OX) / CELL) - 1;
    maxc = (s16)((G.bx + BALL_R - OX) / CELL) + 1;
    minr = (s16)((G.by - BALL_R - OY) / CELL) - 1;
    maxr = (s16)((G.by + BALL_R - OY) / CELL) + 1;
    if (minc < 0) {
        minc = 0;
    }
    if (minr < 0) {
        minr = 0;
    }
    if (maxc >= COLS) {
        maxc = COLS - 1;
    }
    if (maxr >= ROWS) {
        maxr = ROWS - 1;
    }

    for (r = minr; r <= maxr; r++) {
        for (c = minc; c <= maxc; c++) {
            float l, t, rr, b, cx, cy, pl, pr, pt, pb, best;
            if (!cell_wall(c, r)) {
                continue;
            }
            l = (float)(OX + c * CELL);
            t = (float)(OY + r * CELL);
            rr = l + CELL - 1;
            b = t + CELL - 1;
            cx = G.bx;
            cy = G.by;
            if (cx + BALL_R <= l || cx - BALL_R >= rr ||
                cy + BALL_R <= t || cy - BALL_R >= b) {
                continue;
            }
            pl = cx + BALL_R - l;
            pr = rr - (cx - BALL_R);
            pt = cy + BALL_R - t;
            pb = b - (cy - BALL_R);
            best = pl;
            if (pr < best) {
                best = pr;
            }
            if (pt < best) {
                best = pt;
            }
            if (pb < best) {
                best = pb;
            }
            if (best == pl) {
                nx = l - BALL_R;
                G.vx = -G.vx * 0.45f;
            } else if (best == pr) {
                nx = rr + BALL_R;
                G.vx = -G.vx * 0.45f;
            } else if (best == pt) {
                ny = t - BALL_R;
                G.vy = -G.vy * 0.45f;
            } else {
                ny = b + BALL_R;
                G.vy = -G.vy * 0.45f;
            }
        }
    }
    G.bx = nx;
    G.by = ny;
}

static void maze_step(void)
{
    struct sc7u22_si si;
    float ax, ay;
    s16 cc, cr;

    if (G.st != ST_PLAY) {
        return;
    }

    if (imu_read_si(&si) == 0) {
        if (!G.zeroed) {
            G.zero_ax = si.ax;
            G.zero_ay = si.ay;
            G.zeroed = 1;
        }
        ax = -(si.ay - G.zero_ay) * ACC;
        ay = -(si.ax - G.zero_ax) * ACC;
        G.vx = G.vx * FRICTION + ax * TICK_MS / 1000.0f;
        G.vy = G.vy * FRICTION + ay * TICK_MS / 1000.0f;
        if (G.vx > MAX_V) {
            G.vx = MAX_V;
        }
        if (G.vx < -MAX_V) {
            G.vx = -MAX_V;
        }
        if (G.vy > MAX_V) {
            G.vy = MAX_V;
        }
        if (G.vy < -MAX_V) {
            G.vy = -MAX_V;
        }
    }

    G.bx += G.vx * TICK_MS / 1000.0f;
    G.by += G.vy * TICK_MS / 1000.0f;
    collide_walls();

    cc = (s16)((G.bx - OX) / CELL);
    cr = (s16)((G.by - OY) / CELL);
    if (cc == G.gx && cr == G.gy) {
        G.st = ST_WIN;
        if (G.win) {
            lv_obj_clear_flag(G.win, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    G.tick_acc = (u16)(G.tick_acc + TICK_MS);
    if (G.tick_acc >= 1000) {
        G.tick_acc = 0;
        if (G.remain_s > 0) {
            G.remain_s--;
            if (G.lbl_time) {
                lv_label_set_text_fmt(G.lbl_time, "%ds", G.remain_s);
            }
        }
        if (!G.remain_s) {
            G.st = ST_OVER;
            watch_ui_toast("时间到");
            load_level(G.level);
            G.st = ST_PLAY;
        }
    }
}

static void maze_tick(lv_timer_t *t)
{
    (void)t;
    if (!watch_power_input_enabled()) {
        return;
    }
    if (G.st == ST_HELP) {
        return;
    }
    maze_step();
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
    load_level(0);
    watch_game_bgm_start(WATCH_BGM_ACTIVE);
    render();
}

static void win_next_cb(lv_event_t *e)
{
    (void)e;
    if (!watch_power_input_enabled()) {
        return;
    }
    if (G.win) {
        lv_obj_add_flag(G.win, LV_OBJ_FLAG_HIDDEN);
    }
    if (G.level + 1 < LEVEL_N) {
        load_level((u8)(G.level + 1));
        G.st = ST_PLAY;
        watch_ui_toast("下一关");
    } else {
        load_level(0);
        G.st = ST_PLAY;
        watch_ui_toast("再玩一遍");
    }
    render();
}

static void app_maze_cleanup(void);

static void maze_del_cb(lv_event_t *e)
{
    (void)e;
    app_maze_cleanup();
}

static void maze_back_cb(lv_event_t *e)
{
    (void)e;
    watch_ui_close_app();
}

lv_obj_t *app_maze_create(lv_obj_t *parent)
{
    lv_obj_t *root, *bar, *back, *bl, *card, *lab, *ok;
    lv_obj_t *wcard, *wlab, *wnext;

    memset(&G, 0, sizeof(G));
    root = lv_obj_create(parent);
    lv_obj_set_size(root, LCD_W, LCD_H);
    lv_obj_set_style_bg_color(root, C(COL_BG), 0);
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
    lv_obj_add_event_cb(back, maze_back_cb, LV_EVENT_CLICKED, NULL);
    bl = lv_label_create(back);
    lv_obj_set_style_text_font(bl, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(bl, C(0xFFFFFF), 0);
    lv_label_set_text(bl, "返回");
    lv_obj_center(bl);

    G.lbl_time = lv_label_create(bar);
    lv_obj_set_style_text_font(G.lbl_time, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(G.lbl_time, C(0xFFFFFF), 0);
    lv_label_set_text(G.lbl_time, "60s");
    lv_obj_align(G.lbl_time, LV_ALIGN_CENTER, 0, 0);

    G.lbl_lv = lv_label_create(bar);
    lv_obj_set_style_text_font(G.lbl_lv, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(G.lbl_lv, C(0xFFF8E8), 0);
    lv_label_set_text(G.lbl_lv, "LV1");
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
    lv_obj_add_flag(G.canvas, LV_OBJ_FLAG_GESTURE_BUBBLE);

    /* help */
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
    lv_obj_set_style_border_color(card, C(0x5B8DEF), 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lab = lv_label_create(card);
    lv_obj_set_style_text_font(lab, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(lab, C(WATCH_COL_TEXT), 0);
    lv_obj_set_width(lab, 226);
    lv_label_set_long_mode(lab, LV_LABEL_LONG_WRAP);
    lv_label_set_text(lab,
                      "玩法提示\n"
                      "倾斜手表控制小球\n"
                      "滚到绿色终点即通关\n"
                      "撞墙会减速, 抓紧时间!");
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

    /* win */
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
    lv_obj_set_style_border_color(wcard, C(0x06D6A0), 0);
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
    lv_obj_set_style_bg_color(wnext, C(0x4D96FF), 0);
    lv_obj_set_style_shadow_width(wnext, 0, 0);
    lv_obj_add_event_cb(wnext, win_next_cb, LV_EVENT_CLICKED, NULL);
    {
        lv_obj_t *t = lv_label_create(wnext);
        lv_obj_set_style_text_font(t, &lv_font_watch_16_cn, 0);
        lv_obj_set_style_text_color(t, C(0xFFFFFF), 0);
        lv_label_set_text(t, "下一关");
        lv_obj_center(t);
    }

    if (imu_init()) {
        watch_ui_toast("陀螺仪未就绪");
    }

    G.st = ST_HELP;
    load_level(0);
    render();
    G.tick = lv_timer_create(maze_tick, TICK_MS, NULL);
    lv_obj_add_event_cb(root, maze_del_cb, LV_EVENT_DELETE, NULL);
    lv_obj_move_foreground(G.help);
    return root;
}

void app_maze_exit(void)
{
    app_maze_cleanup();
}

static void app_maze_cleanup(void)
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

#else /* !IMU */

lv_obj_t *app_maze_create(lv_obj_t *parent)
{
    lv_obj_t *lb = lv_label_create(parent);
    lv_label_set_text(lb, "no IMU");
    lv_obj_center(lb);
    return lb;
}
void app_maze_exit(void)
{
}
#endif
