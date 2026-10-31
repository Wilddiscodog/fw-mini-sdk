/*
 * 平衡球游戏 —— 陀螺仪控制小球撞水果(六轴 SC7U22)
 *
 * 玩法: 进入 app 以当前姿态为零点, 倾斜设备控制小球; 撞水果得分
 * (苹果+1/橙子+2/西瓜+5), 撞炸弹 -3; 静态障碍柱反弹挡路。单局 60s,
 * 得分升难度(Lv1~Lv4), 5s 内连吃 3 个触发双倍连击。TOP5 成绩入 VM。
 * 界面: LVGL canvas 整帧绘制(33ms 步进), 无图片资源, 全矢量。
 */
#include "app_config.h"
#include "system/includes.h"
#include "syscfg/syscfg_id.h"
#include "lvgl.h"
#include "lcd_config.h"
#include "watch_ball.h"
#include "watch_style.h"
#include "watch_ui.h"
#include "watch_power.h"
#include "sc7u22.h"

#if (defined TCFG_SC7U22_ENABLE) && TCFG_SC7U22_ENABLE

#define BALL_MS          33      /* 物理步进/重绘周期 */
#define BALL_COUNTDOWN_S 60      /* 单局时长 */
#define BALL_COMBO_MS    5000    /* 连击窗口 */

#define GW LCD_W
#define GH LCD_H

/* ---- 物理参数(按难度缩放) ----
 * 首轮试玩反馈"球特别慢"(2026-10-31): 加速 900→2600, 上限 130→320,
 * 摩擦 0.985→0.992(更滑)。仍慢/太灵敏再微调 BALL_ACC 与 MAX_V */
#define BALL_R0          10      /* 球半径 Lv1 */
#define BALL_ACC         2600    /* dps → px/s^2 系数 */
#define BALL_MAX_V0      320     /* Lv1 最大速度 px/s */
#define BALL_FRICTION0   0.992f  /* Lv1 每拍速度保留系数 */

/* ---- 物体 ---- */
enum fruit_kind {
    FRUIT_APPLE = 0,             /* +1 */
    FRUIT_ORANGE,                /* +2 */
    FRUIT_MELON,                 /* +5 */
    FRUIT_BOMB,                  /* -3 */
    FRUIT_KIND_NUM
};

struct fruit {
    u8 used;
    u8 kind;
    s16 x, y;                    /* 圆心 */
    u16 r;
    u16 arm_ms;                  /* 炸弹激活倒计时(0=已激活) */
};

struct obstacle {
    s16 x, y, w, h;              /* 柱形 */
};

/* TOP5 排行榜(VM): {magic, score, secs} x5 */
struct ball_rank {
    u32 magic;
    u16 score[5];
    u16 secs[5];
};

#define BALL_RANK_MAGIC   0x4C4C4231u   /* "BLL1" */
#define BALL_RANK_CFG_ID  55            /* 用户自定义区 1~59 内空闲 */

static struct {
    lv_obj_t *root;
    lv_obj_t *canvas;
    lv_obj_t *s_lbl_score;
    lv_obj_t *s_lbl_time;
    lv_obj_t *s_lbl_lv;
    lv_timer_t *tick;
    lv_timer_t *end_tm;
    lv_color_t *buf;             /* 整帧 296*240*2 ≈ 138KB(SDRAM 堆) */

    u8 playing;                  /* 1=局中 */
    u16 score;
    u16 remain_s;
    u8 lv;                       /* 1..4 */
    u8 combo_n;
    u32 combo_last_ms;

    float bx, by;                /* 球心 */
    float vx, vy;
    u8 ball_r;

    float zero_gx, zero_gy;      /* 进入 app 时的姿态零点 */
    u8 zeroed;

    struct fruit fr;             /* 场上活动目标(单目标制) */
    struct obstacle obs[3];
    u8 obs_n;

    u8 trail_x[8], trail_y[8];   /* 拖尾 */
    u8 trail_i;
} G;

/* ============================ 排行榜(VM) ============================ */
static int ball_rank_load(struct ball_rank *rk)
{
    memset(rk, 0, sizeof(*rk));
    if (syscfg_read(BALL_RANK_CFG_ID, rk, sizeof(*rk)) != sizeof(*rk) ||
        rk->magic != BALL_RANK_MAGIC) {
        return -1;
    }
    return 0;
}

static void ball_rank_save(struct ball_rank *rk)
{
    syscfg_write(BALL_RANK_CFG_ID, rk, sizeof(*rk));
}

static u8 ball_rank_push(u16 score, u16 secs)
{
    struct ball_rank rk;
    int i, j;

    if (ball_rank_load(&rk)) {
        memset(&rk, 0, sizeof(rk));
        rk.magic = BALL_RANK_MAGIC;
    }
    for (i = 0; i < 5; i++) {
        if (score > rk.score[i]) {
            break;
        }
    }
    if (i >= 5) {
        return 0;                        /* 未进榜 */
    }
    for (j = 4; j > i; j--) {
        rk.score[j] = rk.score[j - 1];
        rk.secs[j] = rk.secs[j - 1];
    }
    rk.score[i] = score;
    rk.secs[i] = secs;
    ball_rank_save(&rk);
    return (u8)(i + 1);                  /* 名次 1..5 */
}

/* ============================ 绘制 ============================ */
static void draw_circle(lv_color_t *fb, s16 cx, s16 cy, u16 r, lv_color_t c)
{
    s16 dx, dy;
    s16 y0, y1;

    for (dy = -(s16)r; dy <= (s16)r; dy++) {
        s16 yy = cy + dy;
        if (yy < 0 || yy >= GH) {
            continue;
        }
        dx = (s16)(sqrtf((float)(r * r - dy * dy)) + 0.5f);
        y0 = cx - dx;
        y1 = cx + dx;
        if (y0 < 0) {
            y0 = 0;
        }
        if (y1 >= GW) {
            y1 = GW - 1;
        }
        for (s16 xx = y0; xx <= y1; xx++) {
            fb[yy * GW + xx] = c;
        }
    }
}

static void draw_rect(lv_color_t *fb, s16 x, s16 y, s16 w, s16 h, lv_color_t c)
{
    s16 x1 = x + w, y1 = y + h;
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    if (x1 > GW) {
        x1 = GW;
    }
    if (y1 > GH) {
        y1 = GH;
    }
    for (s16 yy = y; yy < y1; yy++) {
        for (s16 xx = x; xx < x1; xx++) {
            fb[yy * GW + xx] = c;
        }
    }
}

static lv_color_t col_hex(u32 v)
{
    return lv_color_hex(v);
}

/* 前置声明(定义在后) */
static void app_ball_cleanup(void);
static void box_click_cb(lv_event_t *e);
static void ball_del_cb(lv_event_t *e);

/* 水果/炸弹: 圆 + 简笔画特征(梗/条纹/引线) */
static void draw_fruit(lv_color_t *fb, const struct fruit *f)
{
    switch (f->kind) {
    case FRUIT_APPLE:
        draw_circle(fb, f->x, f->y, f->r, col_hex(0xE53935));
        draw_rect(fb, f->x - 1, f->y - f->r - 4, 3, 5, col_hex(0x6D4C41));
        break;
    case FRUIT_ORANGE:
        draw_circle(fb, f->x, f->y, f->r, col_hex(0xFB8C00));
        draw_circle(fb, f->x - f->r / 3, f->y - f->r / 3, f->r / 4,
                    col_hex(0xFFD180));
        break;
    case FRUIT_MELON:
        draw_circle(fb, f->x, f->y, f->r, col_hex(0x43A047));
        for (s16 d = -f->r; d <= (s16)f->r; d += 6) {
            draw_rect(fb, f->x + d, f->y - f->r + 2, 2, f->r * 2 - 4,
                      col_hex(0x1B5E20));
        }
        break;
    case FRUIT_BOMB:
        if (f->arm_ms) {
            /* 未激活: 灰色闪烁提示 */
            lv_color_t c = ((lv_tick_get() / 200) & 1) ? col_hex(0x616161)
                           : col_hex(0xFF5252);
            draw_circle(fb, f->x, f->y, f->r, c);
        } else {
            draw_circle(fb, f->x, f->y, f->r, col_hex(0x212121));
            draw_circle(fb, f->x - f->r / 3, f->y - f->r / 3, f->r / 5,
                        col_hex(0x757575));
            draw_rect(fb, f->x - 1, f->y - f->r - 5, 2, 6, col_hex(0xFFC107));
        }
        break;
    }
}

static void render(void)
{
    lv_color_t *fb = G.buf;
    u8 i;

    /* 背景深色 + 网格淡纹 */
    draw_rect(fb, 0, 0, GW, GH, col_hex(0x101018));
    for (s16 y = 0; y < GH; y += 24) {
        draw_rect(fb, 0, y, GW, 1, col_hex(0x1A1A26));
    }

    /* 障碍柱 */
    for (i = 0; i < G.obs_n; i++) {
        draw_rect(fb, G.obs[i].x, G.obs[i].y, G.obs[i].w, G.obs[i].h,
                  col_hex(0x546E7A));
        draw_rect(fb, G.obs[i].x, G.obs[i].y, G.obs[i].w, 3, col_hex(0x90A4AE));
    }

    /* 拖尾 */
    for (i = 0; i < 8; i++) {
        u8 idx = (u8)((G.trail_i + i) & 7u);
        if (G.trail_x[idx] == 0) {
            continue;
        }
        lv_color_t c = lv_color_mix(col_hex(0xFFCA28), col_hex(0x101018),
                                    255 - i * 28);
        draw_circle(fb, G.trail_x[idx], G.trail_y[idx], 3 + i / 3, c);
    }

    if (G.fr.used) {
        draw_fruit(fb, &G.fr);
    }

    /* 球: 白心黄环 */
    if (G.playing || G.end_tm) {
        draw_circle(fb, (s16)G.bx, (s16)G.by, G.ball_r, col_hex(0xFFD54F));
        draw_circle(fb, (s16)G.bx - G.ball_r / 3, (s16)G.by - G.ball_r / 3,
                    G.ball_r / 3, col_hex(0xFFFFFF));
    }

    lv_obj_invalidate(G.canvas);
}

/* ============================ 生成 ============================ */
static u8 far_enough(s16 x, s16 y, u16 r)
{
    u8 i;
    float dx = (float)x - G.bx;
    float dy = (float)y - G.by;

    if (sqrtf(dx * dx + dy * dy) < 60.0f + r + G.ball_r) {
        return 0;
    }
    for (i = 0; i < G.obs_n; i++) {
        float ox = (float)x - (G.obs[i].x + G.obs[i].w / 2);
        float oy = (float)y - (G.obs[i].y + G.obs[i].h / 2);
        if (fabsf(ox) < G.obs[i].w / 2 + r + 8 &&
            fabsf(oy) < G.obs[i].h / 2 + r + 8) {
            return 0;
        }
    }
    return 1;
}

static void fruit_spawn(void)
{
    u8 tries;
    u16 roll = rand() % 100;

    memset(&G.fr, 0, sizeof(G.fr));
    /* 难度越高炸弹概率越大: 0/12/22/32% */
    if (roll < (G.lv - 1) * 10u + (G.lv >= 2 ? 2 : 0)) {
        G.fr.kind = FRUIT_BOMB;
        G.fr.r = 12;
        G.fr.arm_ms = 1500;              /* 1.5s 后才可撞 */
    } else {
        G.fr.kind = FRUIT_APPLE;         /* 60/30/10 加权 */
        roll = rand() % 100;
        if (roll < 30) {
            G.fr.kind = FRUIT_ORANGE;
        } else if (roll < 40) {
            G.fr.kind = FRUIT_MELON;
        }
        G.fr.r = (G.fr.kind == FRUIT_MELON) ? 16 : 12;
    }
    for (tries = 0; tries < 32; tries++) {
        s16 x = (s16)(G.fr.r + 8 + rand() % (GW - 2 * (G.fr.r + 8)));
        s16 y = (s16)(G.fr.r + 40 + rand() % (GH - 2 * (G.fr.r + 8) - 32));
        if (far_enough(x, y, G.fr.r)) {
            G.fr.x = x;
            G.fr.y = y;
            G.fr.used = 1;
            return;
        }
    }
    G.fr.x = GW / 2;
    G.fr.y = GH / 2;
    G.fr.used = 1;
}

static void ball_set_level(u8 lv)
{
    G.lv = lv;
    G.ball_r = BALL_R0 - (lv - 1);       /* 越难球越小 */
    if (lv >= 3) {
        /* 障碍柱: 固定随机位置, Lv3 两根 Lv4 三根 */
        G.obs_n = lv - 1;
        for (u8 i = 0; i < G.obs_n; i++) {
            G.obs[i].w = 14;
            G.obs[i].h = 44 + rand() % 30;
            G.obs[i].x = (s16)(40 + (i + 1) * (GW - 80) / (G.obs_n + 1));
            G.obs[i].y = (s16)(50 + rand() % (GH - 150));
        }
    }
    lv_label_set_text_fmt(G.s_lbl_lv, "LV%d", G.lv);
}

static void fruit_eat(u8 hit_bomb)
{
    s16 pts;
    u8 combo = 0;

    if (hit_bomb) {
        pts = -3;
        G.combo_n = 0;
    } else {
        u32 now = lv_tick_get();
        if (G.combo_n && now - G.combo_last_ms < BALL_COMBO_MS &&
            ++G.combo_n >= 3) {
            combo = 1;                   /* 5s 内 3 连吃 */
        } else if (!G.combo_n || now - G.combo_last_ms >= BALL_COMBO_MS) {
            G.combo_n = 1;
        }
        G.combo_last_ms = now;
        switch (G.fr.kind) {
        case FRUIT_APPLE:
            pts = 1;
            break;
        case FRUIT_ORANGE:
            pts = 2;
            break;
        default:
            pts = 5;
            break;
        }
        if (combo) {
            pts *= 2;
            watch_ui_toast("COMBO x2!");
        }
    }

    if ((s16)G.score + pts < 0) {
        G.score = 0;
    } else {
        G.score = (u16)(G.score + pts);
    }
    lv_label_set_text_fmt(G.s_lbl_score, "%d", G.score);

    /* 难度推进 + 续命 */
    u8 old = G.lv;
    if (G.score >= 24 && G.lv < 4) {
        ball_set_level(4);
        G.remain_s += 8;
        watch_ui_toast("LV4! +8s");
    } else if (G.score >= 16 && G.lv < 3) {
        ball_set_level(3);
        watch_ui_toast("LV3!");
    } else if (G.score >= 8 && G.lv < 2) {
        ball_set_level(2);
        watch_ui_toast("LV2!");
    }
    if (!old || G.lv != old) {
        /* noop, set_level 已刷 LV 标签 */
    }
    fruit_spawn();
}

/* ============================ 物理步进 ============================ */
static void ball_step(void)
{
    struct sc7u22_si si;
    float ax, ay;
    float maxv = BALL_MAX_V0 + (G.lv - 1) * 30.0f;
    float fric = BALL_FRICTION0 - (G.lv - 1) * 0.012f;
    float dx, dy, dist, minv;
    u8 i;

    if (sc7u22_read_si(&si) == 0) {
        if (!G.zeroed) {
            G.zero_gx = si.gx;
            G.zero_gy = si.gy;
            G.zeroed = 1;
        }
        /* EMA 平滑 → 加速度
         * 轴映射(2026-10-31 现场校准): x←gx 取负(左右反, 实测), y←gy 直连 */
        ax = -(si.gx - G.zero_gx) * BALL_ACC / 1000.0f;
        ay = (si.gy - G.zero_gy) * BALL_ACC / 1000.0f;
        G.vx = G.vx * fric + ax * BALL_MS / 1000.0f;
        G.vy = G.vy * fric + ay * BALL_MS / 1000.0f;
        if (G.vx > maxv) {
            G.vx = maxv;
        }
        if (G.vx < -maxv) {
            G.vx = -maxv;
        }
        if (G.vy > maxv) {
            G.vy = maxv;
        }
        if (G.vy < -maxv) {
            G.vy = -maxv;
        }
    }
    G.bx += G.vx * BALL_MS / 1000.0f;
    G.by += G.vy * BALL_MS / 1000.0f;

    /* 墙壁反弹 */
    if (G.bx < G.ball_r) {
        G.bx = G.ball_r;
        G.vx = -G.vx * 0.7f;
    }
    if (G.bx > GW - 1 - G.ball_r) {
        G.bx = GW - 1 - G.ball_r;
        G.vx = -G.vx * 0.7f;
    }
    if (G.by < G.ball_r + 30) {
        G.by = G.ball_r + 30;
        G.vy = -G.vy * 0.7f;
    }
    if (G.by > GH - 1 - G.ball_r) {
        G.by = GH - 1 - G.ball_r;
        G.vy = -G.vy * 0.7f;
    }

    /* 障碍柱 AABB 反弹 */
    for (i = 0; i < G.obs_n; i++) {
        float cx = G.bx, cy = G.by;
        s16 lx = G.obs[i].x, ly = G.obs[i].y;
        s16 rx = lx + G.obs[i].w, byy = ly + G.obs[i].h;
        if (cx + G.ball_r > lx && cx - G.ball_r < rx &&
            cy + G.ball_r > ly && cy - G.ball_r < byy) {
            /* 找最小穿透轴推开 */
            float pl = cx + G.ball_r - lx;
            float pr = rx - (cx - G.ball_r);
            float pt = cy + G.ball_r - ly;
            float pb = byy - (cy - G.ball_r);
            float m = pl;
            u8 side = 0;
            if (pr < m) {
                m = pr;
                side = 1;
            }
            if (pt < m) {
                m = pt;
                side = 2;
            }
            if (pb < m) {
                m = pb;
                side = 3;
            }
            if (side == 0) {
                G.bx = lx - G.ball_r;
                G.vx = -G.vx * 0.7f;
            } else if (side == 1) {
                G.bx = rx + G.ball_r;
                G.vx = -G.vx * 0.7f;
            } else if (side == 2) {
                G.by = ly - G.ball_r;
                G.vy = -G.vy * 0.7f;
            } else {
                G.by = byy + G.ball_r;
                G.vy = -G.vy * 0.7f;
            }
        }
    }

    /* 目标碰撞 */
    if (G.fr.used) {
        if (G.fr.arm_ms) {
            G.fr.arm_ms = (u16)((G.fr.arm_ms > BALL_MS) ?
                                (G.fr.arm_ms - BALL_MS) : 0);
        } else {
            dx = (float)G.bx - G.fr.x;
            dy = (float)G.by - G.fr.y;
            dist = sqrtf(dx * dx + dy * dy);
            minv = G.ball_r + G.fr.r;
            if (dist < minv) {
                fruit_eat(G.fr.kind == FRUIT_BOMB);
            }
        }
    }

    /* 拖尾 */
    G.trail_x[G.trail_i] = (u8)G.bx;
    G.trail_y[G.trail_i] = (u8)G.by;
    G.trail_i = (u8)((G.trail_i + 1) & 7u);
}

/* ============================ 回合控制 ============================ */
static void game_end(void);

static void ball_tick(lv_timer_t *t)
{
    static u16 sub = 0;
    char buf[24];
    (void)t;

    if (!watch_power_input_enabled()) {
        return;
    }
    if (!G.playing) {
        return;
    }
    ball_step();
    render();

    sub = (u16)(sub + BALL_MS);
    if (sub >= 1000) {
        sub = 0;
        if (G.remain_s > 0) {
            G.remain_s--;
        }
        lv_snprintf(buf, sizeof(buf), "%ds", G.remain_s);
        lv_label_set_text(G.s_lbl_time, buf);
        if (G.remain_s == 0) {
            game_end();
        }
    }
}

/* 结算页: 本局得分 + TOP5(VM) */
static void show_result(void)
{
    lv_color_t *fb = G.buf;
    struct ball_rank rk;
    u8 rank;
    u8 i;

    draw_rect(fb, 0, 0, GW, GH, col_hex(0x101018));
    draw_rect(fb, 20, 30, GW - 40, GH - 60, col_hex(0x1E1E2E));
    lv_obj_invalidate(G.canvas);

    lv_obj_t *box = lv_obj_create(G.root);
    lv_obj_set_size(box, GW - 60, GH - 70);
    lv_obj_align(box, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x1E1E2E), 0);
    lv_obj_set_style_border_color(box, lv_color_hex(WATCH_COL_ACCENT), 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_radius(box, 10, 0);
    lv_obj_set_style_pad_all(box, 8, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *tt = lv_label_create(box);
    lv_obj_set_style_text_font(tt, &lv_font_simsun_16_cjk, 0);
    lv_obj_set_style_text_color(tt, lv_color_hex(0xFFD54F), 0);
    lv_label_set_text_fmt(tt, "本局: %d 分  (LV%d)", G.score, G.lv);
    lv_obj_align(tt, LV_ALIGN_TOP_MID, 0, 2);

    rank = ball_rank_push(G.score, BALL_COUNTDOWN_S - G.remain_s);
    if (rank == 1) {
        lv_obj_t *nr = lv_label_create(box);
        lv_obj_set_style_text_font(nr, &lv_font_simsun_16_cjk, 0);
        lv_obj_set_style_text_color(nr, lv_color_hex(0xFF1744), 0);
        lv_label_set_text(nr, "新纪录!");
        lv_obj_align_to(nr, tt, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);
    }

    lv_obj_t *lb = lv_label_create(box);
    lv_obj_set_style_text_font(lb, &lv_font_simsun_16_cjk, 0);
    lv_obj_set_style_text_color(lb, lv_color_hex(WATCH_COL_TEXT), 0);
    lv_label_set_text(lb, "排行榜 TOP5");
    lv_obj_align(lb, LV_ALIGN_TOP_MID, 0, 26);

    if (ball_rank_load(&rk) == 0) {
        lv_obj_t *rows[5];
        for (i = 0; i < 5; i++) {
            rows[i] = lv_label_create(box);
            lv_obj_set_style_text_font(rows[i], &lv_font_simsun_16_cjk, 0);
            lv_obj_set_style_text_color(rows[i],
                                        lv_color_hex(WATCH_COL_TEXT_DIM), 0);
            lv_label_set_text_fmt(rows[i], "%d. %d 分", i + 1, rk.score[i]);
            lv_obj_align(rows[i], LV_ALIGN_TOP_LEFT, 14, 48 + i * 18);
        }
    }

    lv_obj_t *hint = lv_label_create(box);
    lv_obj_set_style_text_font(hint, &lv_font_simsun_16_cjk, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(WATCH_COL_ACCENT2), 0);
    lv_label_set_text(hint, "点按任意处再来一局");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -2);

    /* 点按重开 */
    lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(box, box_click_cb, LV_EVENT_CLICKED, NULL);
}

static void game_end(void)
{
    G.playing = 0;
    show_result();
}

static void ball_reset(lv_obj_t *canvas)
{
    G.score = 0;
    G.remain_s = BALL_COUNTDOWN_S;
    G.combo_n = 0;
    G.zeroed = 0;
    memset(G.trail_x, 0, sizeof(G.trail_x));
    memset(G.trail_y, 0, sizeof(G.trail_y));
    G.bx = GW / 2.0f;
    G.by = GH / 2.0f;
    G.vx = G.vy = 0;
    memset(G.obs, 0, sizeof(G.obs));
    G.obs_n = 0;
    ball_set_level(1);
    fruit_spawn();
    lv_label_set_text_fmt(G.s_lbl_score, "0");
    lv_label_set_text_fmt(G.s_lbl_time, "%ds", G.remain_s);
    (void)canvas;
}

/* 重开按钮回调(结算是 LVGL 对象, 点击即回调) */
static void box_click_cb(lv_event_t *e)
{
    lv_obj_t *box = lv_event_get_target(e);

    if (!watch_power_input_enabled()) {
        return;
    }
    lv_obj_del(box);
    ball_reset(G.canvas);
    G.playing = 1;
    watch_ui_toast("开始! 倾斜设备移动小球");
}

/* ============================ 入口 ============================ */
lv_obj_t *app_ball_create(lv_obj_t *parent)
{
    lv_obj_t *root;
    lv_obj_t *chrome;
    u8 i;

    memset(&G, 0, sizeof(G));

    /* app 根(与 watch_apps.c make_app_root 同款) */
    root = lv_obj_create(parent);
    lv_obj_set_size(root, LCD_W, LCD_H);
    lv_obj_set_style_bg_color(root, lv_color_hex(WATCH_COL_BG), 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    G.root = root;
    chrome = watch_ui_create_app_chrome(root, "平衡球");

    /* 状态行: 分数 | 倒计时 | 难度 */
    G.s_lbl_score = lv_label_create(chrome);
    lv_obj_set_style_text_font(G.s_lbl_score, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(G.s_lbl_score, lv_color_hex(0xFFD54F), 0);
    lv_label_set_text(G.s_lbl_score, "0");
    lv_obj_align(G.s_lbl_score, LV_ALIGN_TOP_LEFT, 8, 2);

    G.s_lbl_time = lv_label_create(chrome);
    lv_obj_set_style_text_font(G.s_lbl_time, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(G.s_lbl_time, lv_color_hex(WATCH_COL_TEXT), 0);
    lv_label_set_text(G.s_lbl_time, "60s");
    lv_obj_align(G.s_lbl_time, LV_ALIGN_TOP_MID, 0, 2);

    G.s_lbl_lv = lv_label_create(chrome);
    lv_obj_set_style_text_font(G.s_lbl_lv, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(G.s_lbl_lv, lv_color_hex(0x00E676), 0);
    lv_label_set_text(G.s_lbl_lv, "LV1");
    lv_obj_align(G.s_lbl_lv, LV_ALIGN_TOP_RIGHT, -8, 2);

    /* canvas 整帧缓冲(SDRAM 堆 ~138KB), 挂 root(全屏), chrome 只是顶栏 */
    G.buf = (lv_color_t *)malloc((u32)GW * GH * sizeof(lv_color_t));
    if (!G.buf) {
        watch_ui_toast("内存不足");
        return root;
    }
    memset(G.buf, 0, (u32)GW * GH * sizeof(lv_color_t));
    G.canvas = lv_canvas_create(root);
    lv_canvas_set_buffer(G.canvas, G.buf, GW, GH, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(G.canvas, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(G.canvas, LV_OBJ_FLAG_SCROLLABLE);

    for (i = 0; i < 8; i++) {
        G.trail_x[i] = 0;
        G.trail_y[i] = 0;
    }

    if (sc7u22_init()) {
        watch_ui_toast("陀螺仪未就绪, 无法控制小球");
    }

    ball_reset(G.canvas);
    G.playing = 1;
    G.tick = lv_timer_create(ball_tick, BALL_MS, NULL);

    /* 退出清理: app 层被 watch_ui_close_app 删除时释放堆资源 */
    lv_obj_add_event_cb(root, ball_del_cb, LV_EVENT_DELETE, NULL);

    return root;
}

void app_ball_exit(void)
{
    app_ball_cleanup();
}

static void app_ball_cleanup(void)
{
    if (G.tick) {
        lv_timer_del(G.tick);
        G.tick = NULL;
    }
    if (G.end_tm) {
        lv_timer_del(G.end_tm);
        G.end_tm = NULL;
    }
    if (G.buf) {
        free(G.buf);
        G.buf = NULL;
    }
}

/* app 根删除时释放 lv_timer 与堆缓冲(游戏退出/关机路径) */
static void ball_del_cb(lv_event_t *e)
{
    (void)e;
    app_ball_cleanup();
}

#else   /* !TCFG_SC7U22_ENABLE */

lv_obj_t *app_ball_create(lv_obj_t *parent)
{
    lv_obj_t *lb = lv_label_create(parent);
    lv_label_set_text(lb, "no IMU");
    lv_obj_center(lb);
    return lb;
}
void app_ball_exit(void)
{
}

#endif
