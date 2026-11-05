/*
 * 俄罗斯方块 —— 卡通触控版 (296x240)
 * 主棋盘居中放大; 左中=旋转, 右中=落下, 左下=向左, 右下=向右
 */
#include "app_config.h"
#include "system/includes.h"
#include "lvgl.h"
#include "lcd_config.h"
#include "watch_tetris.h"
#include "watch_style.h"
#include "watch_ui.h"
#include "watch_power.h"
#include "watch_game_bgm.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define TOP_H         28
#define GW            LCD_W
#define GH            (LCD_H - TOP_H)

/* 尽量放大棋盘: 两侧留给控制按钮 */
#define COLS          10
#define ROWS          16
#define CELL          13
#define BOARD_W       (COLS * CELL)          /* 130 */
#define BOARD_H       (ROWS * CELL)          /* 208 */
#define BOARD_X       ((GW - BOARD_W) / 2)   /* 居中 ≈83 */
#define BOARD_Y       ((GH - BOARD_H) / 2)   /* 居中 ≈2 */

#define BTN_SIDE_W    70
#define BTN_SIDE_H    48
#define BTN_CORNER_W  72
#define BTN_CORNER_H  40

#define TICK_MS       33
#define LOCK_MS       350

#define COL_BG0       0xB8E8FF
#define COL_BG1       0xFFF0C8
#define COL_BOARD     0xFFFFFF
#define COL_GRID      0xD7EEF8
#define COL_FRAME     0x5BB8E8
#define COL_BAR       0xFF8FAB
#define COL_PANEL     0xFFFFFF

enum {
    ST_HELP = 0,
    ST_PLAY,
    ST_OVER,
};

/* 7 种方块, 4 旋转 x 4 格相对坐标(压缩为 ny*4+nx, 原点在 2x2/4x4 盒内) */
static const s8 SHAPES[7][4][4] = {
    /* I */ {{1, 5, 9, 13}, {4, 5, 6, 7}, {1, 5, 9, 13}, {4, 5, 6, 7}},
    /* O */ {{5, 6, 9, 10}, {5, 6, 9, 10}, {5, 6, 9, 10}, {5, 6, 9, 10}},
    /* T */ {{1, 4, 5, 6}, {1, 5, 6, 9}, {4, 5, 6, 9}, {1, 4, 5, 9}},
    /* S */ {{5, 6, 8, 9}, {1, 5, 6, 10}, {5, 6, 8, 9}, {1, 5, 6, 10}},
    /* Z */ {{4, 5, 9, 10}, {2, 5, 6, 9}, {4, 5, 9, 10}, {2, 5, 6, 9}},
    /* J */ {{0, 4, 5, 6}, {1, 2, 5, 9}, {4, 5, 6, 10}, {1, 5, 8, 9}},
    /* L */ {{2, 4, 5, 6}, {1, 5, 9, 10}, {4, 5, 6, 8}, {0, 1, 5, 9}},
};

static const u32 PIECE_RGB[8] = {
    0x000000,
    0x4ECDC4, /* I 青 */
    0xFFE66D, /* O 黄 */
    0xA66CFF, /* T 紫 */
    0x7CFC98, /* S 绿 */
    0xFF6B6B, /* Z 红 */
    0x4D96FF, /* J 蓝 */
    0xFF9F43, /* L 橙 */
};

static struct {
    lv_obj_t *root, *canvas, *lbl_score, *lbl_lv, *help, *over;
    lv_color_t *buf;
    lv_timer_t *tick;

    u8 grid[ROWS][COLS];
    u8 st;
    u8 type, next, rot;
    s8 px, py;
    u16 fall_acc;
    u16 fall_need;
    u16 lock_acc;
    u8 locking;
    u32 score;
    u16 lines;
    u8 level;
    u8 anim; /* 消行闪烁帧 */

    s16 touch_x0, touch_y0;
    u8 swiping;
} G;

static lv_color_t C(u32 rgb)
{
    return lv_color_make((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

static void cell_xy(u8 idx, s8 *x, s8 *y)
{
    *x = (s8)(idx & 3);
    *y = (s8)(idx >> 2);
}

static int collide(u8 type, u8 rot, s8 x, s8 y)
{
    u8 i;
    s8 cx, cy;

    for (i = 0; i < 4; i++) {
        cell_xy((u8)SHAPES[type][rot][i], &cx, &cy);
        cx = (s8)(x + cx);
        cy = (s8)(y + cy);
        if (cx < 0 || cx >= COLS || cy >= ROWS) {
            return 1;
        }
        if (cy >= 0 && G.grid[cy][cx]) {
            return 1;
        }
    }
    return 0;
}

static void place_piece(void)
{
    u8 i;
    s8 cx, cy;

    for (i = 0; i < 4; i++) {
        cell_xy((u8)SHAPES[G.type][G.rot][i], &cx, &cy);
        cx = (s8)(G.px + cx);
        cy = (s8)(G.py + cy);
        if (cy >= 0 && cy < ROWS && cx >= 0 && cx < COLS) {
            G.grid[cy][cx] = (u8)(G.type + 1);
        }
    }
}

static int clear_lines(void)
{
    int r, c, wr, cleared = 0;

    wr = ROWS - 1;
    for (r = ROWS - 1; r >= 0; r--) {
        int full = 1;
        for (c = 0; c < COLS; c++) {
            if (!G.grid[r][c]) {
                full = 0;
                break;
            }
        }
        if (!full) {
            if (wr != r) {
                memcpy(G.grid[wr], G.grid[r], COLS);
            }
            wr--;
        } else {
            cleared++;
        }
    }
    while (wr >= 0) {
        memset(G.grid[wr], 0, COLS);
        wr--;
    }
    return cleared;
}

static void update_speed(void)
{
    /* 等级越高下落越快 */
    u16 tab[] = {900, 750, 620, 500, 400, 320, 260, 210, 170, 140};
    u8 lv = G.level;
    if (lv >= 10) {
        lv = 9;
    }
    G.fall_need = tab[lv];
}

static void spawn(void)
{
    G.type = G.next;
    G.next = (u8)(rand() % 7);
    G.rot = 0;
    G.px = 3;
    G.py = -1;
    G.lock_acc = 0;
    G.locking = 0;
    if (collide(G.type, G.rot, G.px, G.py)) {
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
    G.lines = 0;
    G.level = 1;
    G.fall_acc = 0;
    G.next = (u8)(rand() % 7);
    update_speed();
    spawn();
    if (G.lbl_score) {
        lv_label_set_text_fmt(G.lbl_score, "%u", G.score);
    }
    if (G.lbl_lv) {
        lv_label_set_text_fmt(G.lbl_lv, "LV%d", G.level);
    }
    if (G.over) {
        lv_obj_add_flag(G.over, LV_OBJ_FLAG_HIDDEN);
    }
}

static void try_move(s8 dx, s8 dy)
{
    if (G.st != ST_PLAY) {
        return;
    }
    if (!collide(G.type, G.rot, (s8)(G.px + dx), (s8)(G.py + dy))) {
        G.px = (s8)(G.px + dx);
        G.py = (s8)(G.py + dy);
        if (dy) {
            G.lock_acc = 0;
            G.locking = 0;
        }
    } else if (dy > 0) {
        G.locking = 1;
    }
}

static void try_rotate(void)
{
    u8 nr;
    s8 kicks[5] = {0, -1, 1, -2, 2};
    u8 k;

    if (G.st != ST_PLAY) {
        return;
    }
    nr = (u8)((G.rot + 1) & 3);
    for (k = 0; k < 5; k++) {
        if (!collide(G.type, nr, (s8)(G.px + kicks[k]), G.py)) {
            G.rot = nr;
            G.px = (s8)(G.px + kicks[k]);
            G.lock_acc = 0;
            return;
        }
    }
}

static void hard_drop(void)
{
    if (G.st != ST_PLAY) {
        return;
    }
    while (!collide(G.type, G.rot, G.px, (s8)(G.py + 1))) {
        G.py++;
        G.score += 2;
    }
    place_piece();
    {
        int n = clear_lines();
        if (n) {
            static const u16 add[] = {0, 100, 300, 500, 800};
            G.score += (u32)add[n] * G.level;
            G.lines = (u16)(G.lines + n);
            G.level = (u8)(1 + G.lines / 8);
            if (G.level > 10) {
                G.level = 10;
            }
            update_speed();
            if (G.lbl_lv) {
                lv_label_set_text_fmt(G.lbl_lv, "LV%d", G.level);
            }
        }
    }
    if (G.lbl_score) {
        lv_label_set_text_fmt(G.lbl_score, "%u", G.score);
    }
    spawn();
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
            put_px(fb, x + i, y + j, c);
        }
    }
}

static void draw_candy(lv_color_t *fb, s16 x, s16 y, u32 rgb)
{
    u8 r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    u8 r2 = (u8)((r * 7) / 10), g2 = (u8)((g * 7) / 10), b2 = (u8)((b * 7) / 10);
    fill_rect(fb, x, y, CELL - 1, CELL - 1, C(rgb));
    /* 底部阴影 */
    fill_rect(fb, x, (s16)(y + CELL - 3), CELL - 1, 2, lv_color_make(r2, g2, b2));
    /* 高光 */
    fill_rect(fb, (s16)(x + 1), (s16)(y + 1), 3, 2, C(0xFFFFFF));
}

static void draw_piece(lv_color_t *fb, u8 type, u8 rot, s8 bx, s8 by, s16 ox, s16 oy)
{
    u8 i;
    s8 cx, cy;
    for (i = 0; i < 4; i++) {
        cell_xy((u8)SHAPES[type][rot][i], &cx, &cy);
        cx = (s8)(bx + cx);
        cy = (s8)(by + cy);
        if (cy < 0) {
            continue;
        }
        draw_candy(fb, (s16)(ox + cx * CELL), (s16)(oy + cy * CELL), PIECE_RGB[type + 1]);
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

    /* 奶油蓝渐变底 */
    for (y = 0; y < GH; y++) {
        u8 r0 = 0xB8, g0 = 0xE8, b0 = 0xFF;
        u8 r1 = 0xFF, g1 = 0xF0, b1 = 0xC8;
        t = (u32)y * 256 / (GH ? GH : 1);
        lv_color_t lc = lv_color_make((u8)((r0 * (256 - t) + r1 * t) >> 8),
                                      (u8)((g0 * (256 - t) + g1 * t) >> 8),
                                      (u8)((b0 * (256 - t) + b1 * t) >> 8));
        for (x = 0; x < GW; x++) {
            fb[y * GW + x] = lc;
        }
    }

    /* 棋盘底板 */
    fill_rect(fb, BOARD_X - 3, BOARD_Y - 3, BOARD_W + 6, BOARD_H + 6, C(COL_FRAME));
    fill_rect(fb, BOARD_X - 1, BOARD_Y - 1, BOARD_W + 2, BOARD_H + 2, C(COL_BOARD));
    for (r = 0; r < ROWS; r++) {
        for (c = 0; c < COLS; c++) {
            s16 cx = (s16)(BOARD_X + c * CELL);
            s16 cy = (s16)(BOARD_Y + r * CELL);
            if ((r + c) & 1) {
                fill_rect(fb, cx, cy, CELL - 1, CELL - 1, C(COL_GRID));
            }
            if (G.grid[r][c]) {
                draw_candy(fb, cx, cy, PIECE_RGB[G.grid[r][c]]);
            }
        }
    }

    if (G.st == ST_PLAY || G.st == ST_OVER) {
        /* 幽灵落点 */
        if (G.st == ST_PLAY) {
            s8 gy = G.py;
            while (!collide(G.type, G.rot, G.px, (s8)(gy + 1))) {
                gy++;
            }
            if (gy != G.py) {
                u8 i;
                s8 cx, cy;
                for (i = 0; i < 4; i++) {
                    cell_xy((u8)SHAPES[G.type][G.rot][i], &cx, &cy);
                    cx = (s8)(G.px + cx);
                    cy = (s8)(gy + cy);
                    if (cy >= 0) {
                        fill_rect(fb, (s16)(BOARD_X + cx * CELL + 2),
                                  (s16)(BOARD_Y + cy * CELL + 2),
                                  CELL - 5, CELL - 5, C(0xAAD4E8));
                    }
                }
            }
            draw_piece(fb, G.type, G.rot, G.px, G.py, BOARD_X, BOARD_Y);
        }
    }

    /* 右上角下一块预览(侧栏空隙) */
    {
        s16 px = (s16)(BOARD_X + BOARD_W + 6);
        s16 py = (s16)(BOARD_Y + 4);
        if (px + 56 < GW) {
            fill_rect(fb, px, py, 56, 50, C(COL_PANEL));
            fill_rect(fb, (s16)(px + 2), (s16)(py + 2), 52, 46, C(0xFFF8E8));
            draw_piece(fb, G.next, 0, 0, 0, (s16)(px + 8), (s16)(py + 6));
        }
    }

    lv_obj_invalidate(G.canvas);
}

static void lock_and_continue(void)
{
    int n;

    place_piece();
    n = clear_lines();
    if (n) {
        static const u16 add[] = {0, 100, 300, 500, 800};
        G.score += (u32)add[n] * G.level;
        G.lines = (u16)(G.lines + n);
        G.level = (u8)(1 + G.lines / 8);
        if (G.level > 10) {
            G.level = 10;
        }
        update_speed();
        if (G.lbl_lv) {
            lv_label_set_text_fmt(G.lbl_lv, "LV%d", G.level);
        }
    }
    if (G.lbl_score) {
        lv_label_set_text_fmt(G.lbl_score, "%u", G.score);
    }
    spawn();
}

static void tetris_step(void)
{
    if (G.st != ST_PLAY) {
        return;
    }
    G.fall_acc = (u16)(G.fall_acc + TICK_MS);
    if (G.fall_acc >= G.fall_need) {
        G.fall_acc = 0;
        if (!collide(G.type, G.rot, G.px, (s8)(G.py + 1))) {
            G.py++;
            G.locking = 0;
            G.lock_acc = 0;
        } else {
            G.locking = 1;
        }
    }
    if (G.locking) {
        G.lock_acc = (u16)(G.lock_acc + TICK_MS);
        if (G.lock_acc >= LOCK_MS) {
            lock_and_continue();
        }
    }
}

static void tetris_tick(lv_timer_t *t)
{
    (void)t;
    if (!watch_power_input_enabled()) {
        return;
    }
    if (G.st == ST_HELP) {
        return;
    }
    tetris_step();
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
    watch_game_bgm_start(WATCH_BGM_SOFT);
    render();
}

static void over_restart_cb(lv_event_t *e)
{
    (void)e;
    if (!watch_power_input_enabled()) {
        return;
    }
    G.st = ST_PLAY;
    soft_reset();
    watch_game_bgm_start(WATCH_BGM_SOFT);
    render();
}

static void btn_left(lv_event_t *e)
{
    (void)e;
    try_move(-1, 0);
    render();
}
static void btn_right(lv_event_t *e)
{
    (void)e;
    try_move(1, 0);
    render();
}
static void btn_rot(lv_event_t *e)
{
    (void)e;
    try_rotate();
    render();
}
static void btn_drop(lv_event_t *e)
{
    (void)e;
    hard_drop();
    render();
}

static void board_touch_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev;
    lv_point_t p;

    if (!watch_power_input_enabled() || G.st != ST_PLAY) {
        return;
    }
    indev = lv_indev_get_act();
    if (!indev) {
        return;
    }
    lv_indev_get_point(indev, &p);
    /* 转到 canvas 局部坐标 */
    p.y = (lv_coord_t)(p.y - TOP_H);

    if (code == LV_EVENT_PRESSED) {
        G.touch_x0 = p.x;
        G.touch_y0 = p.y;
        G.swiping = 0;
    } else if (code == LV_EVENT_PRESSING) {
        s16 dx = (s16)(p.x - G.touch_x0);
        s16 dy = (s16)(p.y - G.touch_y0);
        s16 adx = dx < 0 ? (s16)(-dx) : dx;
        s16 ady = dy < 0 ? (s16)(-dy) : dy;
        /* 仅左右滑移动; 下滑快落已去掉, 用右侧【落下】 */
        if (!G.swiping && adx > 18 && adx > ady) {
            G.swiping = 1;
            try_move(dx > 0 ? 1 : -1, 0);
            G.touch_x0 = p.x;
            G.touch_y0 = p.y;
            render();
        } else if (G.swiping && adx > 22) {
            try_move(dx > 0 ? 1 : -1, 0);
            G.touch_x0 = p.x;
            render();
        }
    } else if (code == LV_EVENT_RELEASED) {
        if (!G.swiping) {
            try_rotate();
            render();
        }
        G.swiping = 0;
    }
}

static void make_ctrl_btn(lv_obj_t *parent, const char *txt, u32 col,
                          lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                          lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_t *lab;
    lv_obj_set_size(btn, w, h);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_radius(btn, 14, 0);
    lv_obj_set_style_bg_color(btn, C(col), 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_border_color(btn, C(0xFFFFFF), 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lab = lv_label_create(btn);
    lv_obj_set_style_text_font(lab, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(lab, C(0xFFFFFF), 0);
    lv_label_set_text(lab, txt);
    lv_obj_center(lab);
}

static void app_tetris_cleanup(void);

static void tetris_del_cb(lv_event_t *e)
{
    (void)e;
    app_tetris_cleanup();
}

static void tetris_back_cb(lv_event_t *e)
{
    (void)e;
    watch_ui_close_app();
}

lv_obj_t *app_tetris_create(lv_obj_t *parent)
{
    lv_obj_t *root, *bar, *back, *bl, *card, *lab, *ok;
    lv_obj_t *over_card, *olab, *orek;
    lv_coord_t mid_y, bot_y;

    memset(&G, 0, sizeof(G));
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
    lv_obj_add_event_cb(back, tetris_back_cb, LV_EVENT_CLICKED, NULL);
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
    lv_obj_add_flag(G.canvas, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(G.canvas, board_touch_cb, LV_EVENT_ALL, NULL);

    /* 四向控制: 左中旋转 / 右中落下 / 左下向左 / 右下向右 */
    mid_y = (lv_coord_t)(TOP_H + (GH - BTN_SIDE_H) / 2);
    bot_y = (lv_coord_t)(LCD_H - BTN_CORNER_H - 4);
    make_ctrl_btn(root, "旋转", 0xA66CFF, 4, mid_y, BTN_SIDE_W, BTN_SIDE_H, btn_rot);
    make_ctrl_btn(root, "落下", 0xFF6B6B,
                  (lv_coord_t)(LCD_W - BTN_SIDE_W - 4), mid_y,
                  BTN_SIDE_W, BTN_SIDE_H, btn_drop);
    make_ctrl_btn(root, "向左", 0x4D96FF, 4, bot_y, BTN_CORNER_W, BTN_CORNER_H, btn_left);
    make_ctrl_btn(root, "向右", 0x06D6A0,
                  (lv_coord_t)(LCD_W - BTN_CORNER_W - 4), bot_y,
                  BTN_CORNER_W, BTN_CORNER_H, btn_right);

    /* 玩法弹窗 */
    G.help = lv_obj_create(root);
    lv_obj_set_size(G.help, LCD_W, LCD_H);
    lv_obj_set_style_bg_color(G.help, C(0x000000), 0);
    lv_obj_set_style_bg_opa(G.help, LV_OPA_50, 0);
    lv_obj_set_style_border_width(G.help, 0, 0);
    lv_obj_set_style_radius(G.help, 0, 0);
    lv_obj_set_style_pad_all(G.help, 0, 0);
    lv_obj_clear_flag(G.help, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(G.help, LV_OBJ_FLAG_CLICKABLE);

    card = lv_obj_create(G.help);
    lv_obj_set_size(card, 250, 180);
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
                      "左下【向左】右下【向右】\n"
                      "左侧【旋转】右侧【落下】\n"
                      "也可点棋盘旋转、左右滑移\n"
                      "消行得分, 别堆满!");
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

    /* 结束弹窗 */
    G.over = lv_obj_create(root);
    lv_obj_set_size(G.over, LCD_W, LCD_H);
    lv_obj_set_style_bg_color(G.over, C(0x000000), 0);
    lv_obj_set_style_bg_opa(G.over, LV_OPA_40, 0);
    lv_obj_set_style_border_width(G.over, 0, 0);
    lv_obj_set_style_pad_all(G.over, 0, 0);
    lv_obj_clear_flag(G.over, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(G.over, LV_OBJ_FLAG_HIDDEN);

    over_card = lv_obj_create(G.over);
    lv_obj_set_size(over_card, 200, 110);
    lv_obj_center(over_card);
    lv_obj_set_style_radius(over_card, 16, 0);
    lv_obj_set_style_bg_color(over_card, C(0xFFFFFF), 0);
    lv_obj_set_style_border_width(over_card, 3, 0);
    lv_obj_set_style_border_color(over_card, C(0xFF6B6B), 0);
    lv_obj_clear_flag(over_card, LV_OBJ_FLAG_SCROLLABLE);

    olab = lv_label_create(over_card);
    lv_obj_set_style_text_font(olab, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(olab, C(WATCH_COL_TEXT), 0);
    lv_label_set_text(olab, "游戏结束");
    lv_obj_align(olab, LV_ALIGN_TOP_MID, 0, 14);

    orek = lv_btn_create(over_card);
    lv_obj_set_size(orek, 110, 32);
    lv_obj_align(orek, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_radius(orek, 12, 0);
    lv_obj_set_style_bg_color(orek, C(0x4D96FF), 0);
    lv_obj_set_style_shadow_width(orek, 0, 0);
    lv_obj_add_event_cb(orek, over_restart_cb, LV_EVENT_CLICKED, NULL);
    {
        lv_obj_t *t = lv_label_create(orek);
        lv_obj_set_style_text_font(t, &lv_font_watch_16_cn, 0);
        lv_obj_set_style_text_color(t, C(0xFFFFFF), 0);
        lv_label_set_text(t, "再来");
        lv_obj_center(t);
    }

    G.st = ST_HELP;
    G.next = (u8)(rand() % 7);
    render();
    G.tick = lv_timer_create(tetris_tick, TICK_MS, NULL);
    lv_obj_add_event_cb(root, tetris_del_cb, LV_EVENT_DELETE, NULL);
    lv_obj_move_foreground(G.help);
    return root;
}

void app_tetris_exit(void)
{
    app_tetris_cleanup();
}

static void app_tetris_cleanup(void)
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
