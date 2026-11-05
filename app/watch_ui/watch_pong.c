/*
 * Pong —— 雅达利风格: 左 AI / 右玩家挡板, 触控上下拖动
 */
#include "app_config.h"
#include "system/includes.h"
#include "lvgl.h"
#include "lcd_config.h"
#include "watch_pong.h"
#include "watch_style.h"
#include "watch_ui.h"
#include "watch_power.h"
#include "watch_game_bgm.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#define TOP_H    28
#define GW       LCD_W
#define GH       (LCD_H - TOP_H)
#define TICK_MS  33
#define PAD_W    8
#define PAD_H    44
#define BALL_R   4
#define WIN_SCORE 5

enum { ST_HELP = 0, ST_PLAY, ST_OVER };

static struct {
    lv_obj_t *root, *canvas, *lbl, *help, *over;
    lv_color_t *buf;
    lv_timer_t *tick;
    float bx, by, vx, vy;
    float py, ay; /* player / AI paddle center y */
    u8 ps, as;    /* scores */
    u8 st;
    u8 dragging;
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

static void serve(int to_right)
{
    G.bx = GW / 2.0f;
    G.by = GH / 2.0f;
    G.vx = to_right ? 110.0f : -110.0f;
    G.vy = ((rand() % 2) ? 1.0f : -1.0f) * (40.0f + rand() % 50);
}

static void soft_reset(void)
{
    G.ps = G.as = 0;
    G.py = G.ay = GH / 2.0f;
    serve(1);
    if (G.lbl) {
        lv_label_set_text_fmt(G.lbl, "%d : %d", G.as, G.ps);
    }
    if (G.over) {
        lv_obj_add_flag(G.over, LV_OBJ_FLAG_HIDDEN);
    }
}

static void render(void)
{
    lv_color_t *fb = G.buf;
    s16 y;

    if (!fb) {
        return;
    }
    fill_rect(fb, 0, 0, GW, GH, C(0x000000));
    /* 中线虚线 */
    for (y = 0; y < GH; y += 10) {
        fill_rect(fb, GW / 2 - 1, y, 2, 5, C(0x444444));
    }
    fill_rect(fb, 8, (s16)(G.ay - PAD_H / 2), PAD_W, PAD_H, C(0xFFFFFF));
    fill_rect(fb, (s16)(GW - 8 - PAD_W), (s16)(G.py - PAD_H / 2), PAD_W, PAD_H, C(0xFFFFFF));
    fill_circle(fb, (s16)G.bx, (s16)G.by, BALL_R, C(0xFFFFFF));
    lv_obj_invalidate(G.canvas);
}

static void step(void)
{
    float dt = TICK_MS / 1000.0f;
    float target;
    float ptop, pbot, atop, abot;

    if (G.st != ST_PLAY) {
        return;
    }

    G.bx += G.vx * dt;
    G.by += G.vy * dt;

    if (G.by < BALL_R) {
        G.by = BALL_R;
        G.vy = -G.vy;
    }
    if (G.by > GH - 1 - BALL_R) {
        G.by = GH - 1 - BALL_R;
        G.vy = -G.vy;
    }

    /* AI 略慢跟随 */
    target = G.by;
    if (G.ay < target - 2) {
        G.ay += 95.0f * dt;
    } else if (G.ay > target + 2) {
        G.ay -= 95.0f * dt;
    }
    if (G.ay < PAD_H / 2) {
        G.ay = PAD_H / 2;
    }
    if (G.ay > GH - PAD_H / 2) {
        G.ay = GH - PAD_H / 2;
    }
    if (G.py < PAD_H / 2) {
        G.py = PAD_H / 2;
    }
    if (G.py > GH - PAD_H / 2) {
        G.py = GH - PAD_H / 2;
    }

    ptop = G.py - PAD_H / 2;
    pbot = G.py + PAD_H / 2;
    atop = G.ay - PAD_H / 2;
    abot = G.ay + PAD_H / 2;

    /* 右挡板(玩家) */
    if (G.vx > 0 && G.bx + BALL_R >= GW - 8 - PAD_W && G.bx < GW - 8) {
        if (G.by >= ptop && G.by <= pbot) {
            G.bx = GW - 8 - PAD_W - BALL_R;
            G.vx = -G.vx * 1.05f;
            G.vy += (G.by - G.py) * 2.0f;
            if (G.vx < -200) {
                G.vx = -200;
            }
        }
    }
    /* 左挡板(AI) */
    if (G.vx < 0 && G.bx - BALL_R <= 8 + PAD_W && G.bx > 8) {
        if (G.by >= atop && G.by <= abot) {
            G.bx = 8 + PAD_W + BALL_R;
            G.vx = -G.vx * 1.05f;
            G.vy += (G.by - G.ay) * 2.0f;
            if (G.vx > 200) {
                G.vx = 200;
            }
        }
    }

    if (G.bx < -10) {
        G.ps++;
        if (G.lbl) {
            lv_label_set_text_fmt(G.lbl, "%d : %d", G.as, G.ps);
        }
        if (G.ps >= WIN_SCORE) {
            G.st = ST_OVER;
            if (G.over) {
                lv_obj_clear_flag(G.over, LV_OBJ_FLAG_HIDDEN);
            }
        } else {
            serve(1);
        }
    } else if (G.bx > GW + 10) {
        G.as++;
        if (G.lbl) {
            lv_label_set_text_fmt(G.lbl, "%d : %d", G.as, G.ps);
        }
        if (G.as >= WIN_SCORE) {
            G.st = ST_OVER;
            if (G.over) {
                lv_obj_clear_flag(G.over, LV_OBJ_FLAG_HIDDEN);
            }
        } else {
            serve(0);
        }
    }
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    if (!watch_power_input_enabled() || G.st == ST_HELP) {
        return;
    }
    step();
    render();
}

static void canvas_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_get_act();
    lv_point_t p;

    if (!indev || G.st != ST_PLAY) {
        return;
    }
    lv_indev_get_point(indev, &p);
    p.y = (lv_coord_t)(p.y - TOP_H);
    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        G.py = (float)p.y;
        G.dragging = 1;
    } else if (code == LV_EVENT_RELEASED) {
        G.dragging = 0;
    }
}

static void help_ok(lv_event_t *e)
{
    (void)e;
    if (G.help) {
        lv_obj_add_flag(G.help, LV_OBJ_FLAG_HIDDEN);
    }
    G.st = ST_PLAY;
    soft_reset();
    watch_game_bgm_start(WATCH_BGM_ACTIVE);
    render();
}

static void over_ok(lv_event_t *e)
{
    (void)e;
    G.st = ST_PLAY;
    soft_reset();
    render();
}

static void cleanup(void)
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

lv_obj_t *app_pong_create(lv_obj_t *parent)
{
    lv_obj_t *root, *bar, *back, *bl, *card, *lab, *ok, *ocard, *ol, *ob;

    memset(&G, 0, sizeof(G));
    root = lv_obj_create(parent);
    lv_obj_set_size(root, LCD_W, LCD_H);
    lv_obj_set_style_bg_color(root, C(0x000000), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_GESTURE_BUBBLE);
    G.root = root;

    bar = lv_obj_create(root);
    lv_obj_set_size(bar, LCD_W, TOP_H);
    lv_obj_set_style_bg_color(bar, C(0x111111), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_hor(bar, 4, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    back = lv_btn_create(bar);
    lv_obj_set_size(back, 44, 22);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(back, C(0x333333), 0);
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
    lv_obj_set_style_text_color(G.lbl, C(0xFFFFFF), 0);
    lv_label_set_text(G.lbl, "0 : 0");
    lv_obj_align(G.lbl, LV_ALIGN_CENTER, 0, 0);

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
    lv_obj_set_style_bg_color(card, C(0x222222), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, C(0xFFFFFF), 0);
    lab = lv_label_create(card);
    lv_obj_set_style_text_font(lab, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(lab, C(0xFFFFFF), 0);
    lv_obj_set_width(lab, 226);
    lv_label_set_long_mode(lab, LV_LABEL_LONG_WRAP);
    lv_label_set_text(lab, "玩法提示\n拖动右侧挡板接球\n先得 5 分通关\n左侧是电脑");
    lv_obj_align(lab, LV_ALIGN_TOP_MID, 0, 8);
    ok = lv_btn_create(card);
    lv_obj_set_size(ok, 120, 32);
    lv_obj_align(ok, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_bg_color(ok, C(0xFFFFFF), 0);
    lv_obj_set_style_radius(ok, 12, 0);
    lv_obj_set_style_shadow_width(ok, 0, 0);
    lv_obj_add_event_cb(ok, help_ok, LV_EVENT_CLICKED, NULL);
    {
        lv_obj_t *t = lv_label_create(ok);
        lv_obj_set_style_text_font(t, &lv_font_watch_16_cn, 0);
        lv_obj_set_style_text_color(t, C(0x000000), 0);
        lv_label_set_text(t, "知道了");
        lv_obj_center(t);
    }

    G.over = lv_obj_create(root);
    lv_obj_set_size(G.over, LCD_W, LCD_H);
    lv_obj_set_style_bg_color(G.over, C(0), 0);
    lv_obj_set_style_bg_opa(G.over, LV_OPA_50, 0);
    lv_obj_set_style_border_width(G.over, 0, 0);
    lv_obj_set_style_pad_all(G.over, 0, 0);
    lv_obj_add_flag(G.over, LV_OBJ_FLAG_HIDDEN);
    ocard = lv_obj_create(G.over);
    lv_obj_set_size(ocard, 180, 100);
    lv_obj_center(ocard);
    lv_obj_set_style_radius(ocard, 14, 0);
    lv_obj_set_style_bg_color(ocard, C(0x222222), 0);
    lv_obj_set_style_border_width(ocard, 2, 0);
    lv_obj_set_style_border_color(ocard, C(0xFFFFFF), 0);
    ol = lv_label_create(ocard);
    lv_obj_set_style_text_font(ol, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(ol, C(0xFFFFFF), 0);
    lv_label_set_text(ol, "再来一局");
    lv_obj_align(ol, LV_ALIGN_TOP_MID, 0, 14);
    ob = lv_btn_create(ocard);
    lv_obj_set_size(ob, 100, 30);
    lv_obj_align(ob, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(ob, C(0xFFFFFF), 0);
    lv_obj_set_style_radius(ob, 10, 0);
    lv_obj_set_style_shadow_width(ob, 0, 0);
    lv_obj_add_event_cb(ob, over_ok, LV_EVENT_CLICKED, NULL);
    {
        lv_obj_t *t = lv_label_create(ob);
        lv_obj_set_style_text_font(t, &lv_font_watch_16_cn, 0);
        lv_obj_set_style_text_color(t, C(0x000000), 0);
        lv_label_set_text(t, "再来");
        lv_obj_center(t);
    }

    G.st = ST_HELP;
    soft_reset();
    render();
    G.tick = lv_timer_create(tick_cb, TICK_MS, NULL);
    lv_obj_add_event_cb(root, del_cb, LV_EVENT_DELETE, NULL);
    lv_obj_move_foreground(G.help);
    return root;
}

void app_pong_exit(void)
{
    cleanup();
}
