/*
 * 平衡球游戏 —— 陀螺仪控制小球撞水果(六轴 SC7U22)
 *
 * 玩法: 绝对水平为零点(ax/ay≈0g), 相对水平倾斜控制小球; 撞水果得分
 * (苹果+1 / 香蕉+2+5s / 西瓜+5), 撞炸弹 -3 且 -10s; 静态障碍柱反弹。
 * 单局初始 30s, 得分升难度(Lv1~Lv4), 5s 内连吃 3 个双倍连击。
 * 炸弹闪烁仅为预警, 全程可撞(不可穿模)。TOP5 入 VM。
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
#include "watch_game_bgm.h"
#include "imu.h"

#if (defined TCFG_IMU_ENABLE) && TCFG_IMU_ENABLE

#define BALL_MS          33      /* 物理步进/重绘周期 */
#define BALL_COUNTDOWN_S 30      /* 单局初始时长 */
#define BALL_TIME_CAP_S  99      /* 加时上限, 避免无限拖 */
#define BALL_COMBO_MS    5000    /* 连击窗口 */
#define BALL_BANANA_BONUS_S 5
#define BALL_BOMB_PENALTY_S 10

#define BALL_TOP_H       36      /* 顶栏高度, canvas 在其下 */
#define GW               LCD_W
#define GH               (LCD_H - BALL_TOP_H)

/* 玩法区配色 */
#define BALL_COL_BG      0x0B1220
#define BALL_COL_GRID    0x152033
#define BALL_COL_BAR     0x121A2A
#define BALL_COL_SCORE   0xFFCC66
#define BALL_COL_TIME    0xE8F1FF
#define BALL_COL_LV      0x5CDB95

/* ---- 物理参数 ----
 * 加速度计相对零点; 加大阻尼避免过飘 */
#define BALL_R0          10
#define BALL_ACC         1100.0f /* g → px/s^2 */
#define BALL_MAX_V0      200     /* Lv1 最大速度 px/s */
#define BALL_FRICTION0   0.968f  /* 每拍速度保留, 越小越「粘」 */

/* ---- 物体 ---- */
enum fruit_kind {
    FRUIT_APPLE = 0,             /* +1 分 */
    FRUIT_BANANA,                /* +2 分, +5s */
    FRUIT_MELON,                 /* +5 分 */
    FRUIT_BOMB,                  /* -3 分, -10s */
    FRUIT_KIND_NUM
};

struct fruit {
    u8 used;
    u8 kind;
    s16 x, y;                    /* 圆心 */
    u16 r;
    u16 arm_ms;                  /* 炸弹预警闪烁剩余 ms(仅视觉, 可撞) */
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
    u16 elapsed_s;               /* 已玩秒数(加时后仍正确记排行) */
    u8 lv;                       /* 1..4 */
    u8 combo_n;
    u32 combo_last_ms;

    float bx, by;                /* 球心 */
    float vx, vy;
    u8 ball_r;

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
static void ball_back_cb(lv_event_t *e);

/* 水果/炸弹: 圆 + 简笔画特征(梗/条纹/引线) */
static void draw_fruit(lv_color_t *fb, const struct fruit *f)
{
    switch (f->kind) {
    case FRUIT_APPLE:
        draw_circle(fb, f->x, f->y, f->r, col_hex(0xE53935));
        draw_rect(fb, f->x - 1, f->y - f->r - 4, 3, 5, col_hex(0x6D4C41));
        break;
    case FRUIT_BANANA:
        /* 香蕉: 黄椭圆体 + 两端深色 */
        draw_circle(fb, f->x, f->y, f->r, col_hex(0xFDD835));
        draw_circle(fb, f->x - f->r / 2, f->y + f->r / 4, f->r / 3,
                    col_hex(0xF9A825));
        draw_circle(fb, f->x + f->r / 2, f->y - f->r / 4, f->r / 4,
                    col_hex(0xF9A825));
        draw_rect(fb, f->x + f->r / 2, f->y - f->r - 2, 3, 4,
                  col_hex(0x6D4C41));
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
            /* 预警闪烁(仍可碰撞) */
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

    /* 玩法区背景 */
    draw_rect(fb, 0, 0, GW, GH, col_hex(BALL_COL_BG));
    for (s16 y = 0; y < GH; y += 24) {
        draw_rect(fb, 0, y, GW, 1, col_hex(BALL_COL_GRID));
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
        G.fr.arm_ms = 1500;              /* 1.5s 预警闪烁, 全程可撞 */
    } else {
        G.fr.kind = FRUIT_APPLE;         /* 苹果55 / 香蕉30 / 西瓜15 */
        roll = rand() % 100;
        if (roll < 30) {
            G.fr.kind = FRUIT_BANANA;
        } else if (roll < 45) {
            G.fr.kind = FRUIT_MELON;
        }
        G.fr.r = (G.fr.kind == FRUIT_MELON) ? 16 : 12;
    }
    for (tries = 0; tries < 32; tries++) {
        s16 x = (s16)(G.fr.r + 8 + rand() % (GW - 2 * (G.fr.r + 8)));
        s16 y = (s16)(G.fr.r + 8 + rand() % (GH - 2 * (G.fr.r + 8) - 8));
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

static void fruit_add_time(u16 sec)
{
    u32 t = (u32)G.remain_s + sec;

    if (t > BALL_TIME_CAP_S) {
        t = BALL_TIME_CAP_S;
    }
    G.remain_s = (u16)t;
    lv_label_set_text_fmt(G.s_lbl_time, "%ds", G.remain_s);
}

static void fruit_sub_time(u16 sec)
{
    if (G.remain_s > sec) {
        G.remain_s = (u16)(G.remain_s - sec);
    } else {
        G.remain_s = 0;
    }
    lv_label_set_text_fmt(G.s_lbl_time, "%ds", G.remain_s);
}

static void fruit_eat(u8 hit_bomb)
{
    s16 pts;
    u8 combo = 0;
    u16 bonus_s = 0;

    if (hit_bomb) {
        pts = -3;
        G.combo_n = 0;
        fruit_sub_time(BALL_BOMB_PENALTY_S);
        watch_ui_toast("-10s");
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
        case FRUIT_BANANA:
            pts = 2;
            bonus_s = BALL_BANANA_BONUS_S;
            break;
        default:
            pts = 5;
            break;
        }
        if (combo) {
            pts *= 2;
            watch_ui_toast("COMBO x2!");
        } else if (bonus_s) {
            watch_ui_toast("+5s");
        }
    }

    if ((s16)G.score + pts < 0) {
        G.score = 0;
    } else {
        G.score = (u16)(G.score + pts);
    }
    lv_label_set_text_fmt(G.s_lbl_score, "%d", G.score);

    if (bonus_s) {
        fruit_add_time(bonus_s);
    }

    /* 难度推进 */
    if (G.score >= 24 && G.lv < 4) {
        ball_set_level(4);
        fruit_add_time(8);
        watch_ui_toast("LV4! +8s");
    } else if (G.score >= 16 && G.lv < 3) {
        ball_set_level(3);
        watch_ui_toast("LV3!");
    } else if (G.score >= 8 && G.lv < 2) {
        ball_set_level(2);
        watch_ui_toast("LV2!");
    }
    fruit_spawn();
}

/* ============================ 物理步进 ============================ */
static void ball_step(void)
{
    struct sc7u22_si si;
    float ax, ay;
    float maxv = BALL_MAX_V0 + (G.lv - 1) * 30.0f;
    float fric = BALL_FRICTION0 - (G.lv - 1) * 0.003f;
    float dx, dy, dist, minv;
    u8 i;

    if (imu_read_si(&si) == 0) {
        /* 绝对水平: 平放时 ax/ay≈0g 即为零点, 不再采样进场姿态.
         * 轴对调后左右仍反, 再取反 X; 上下已正确保持 */
        ax = -si.ay * BALL_ACC;
        ay = -si.ax * BALL_ACC;
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
    /* 玩法 canvas 已在顶栏下方, 不再预留 +30 空气墙 */
    if (G.by < G.ball_r) {
        G.by = G.ball_r;
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

    /* 目标碰撞: 炸弹预警闪烁期也可撞, 不可穿模 */
    if (G.fr.used) {
        if (G.fr.arm_ms) {
            G.fr.arm_ms = (u16)((G.fr.arm_ms > BALL_MS) ?
                                (G.fr.arm_ms - BALL_MS) : 0);
        }
        dx = (float)G.bx - G.fr.x;
        dy = (float)G.by - G.fr.y;
        dist = sqrtf(dx * dx + dy * dy);
        minv = G.ball_r + G.fr.r;
        if (dist < minv) {
            fruit_eat(G.fr.kind == FRUIT_BOMB);
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
        if (G.elapsed_s < 0xFFFFu) {
            G.elapsed_s++;
        }
        lv_snprintf(buf, sizeof(buf), "%ds", G.remain_s);
        lv_label_set_text(G.s_lbl_time, buf);
        if (G.remain_s == 0) {
            game_end();
        }
    }
}

/* 结算页: 纵向 flex, 避免固定 Y 叠字 */
static void show_result(void)
{
    lv_color_t *fb = G.buf;
    struct ball_rank rk;
    u8 rank;
    u8 i;
    lv_obj_t *box;
    lv_obj_t *tt;
    lv_obj_t *lb;
    lv_obj_t *row;
    lv_obj_t *hint;

    draw_rect(fb, 0, 0, GW, GH, col_hex(BALL_COL_BG));
    lv_obj_invalidate(G.canvas);

    /* 顶栏状态文字暂隐, 避免与结算分数重叠 */
    if (G.s_lbl_score) {
        lv_obj_add_flag(G.s_lbl_score, LV_OBJ_FLAG_HIDDEN);
    }
    if (G.s_lbl_time) {
        lv_obj_add_flag(G.s_lbl_time, LV_OBJ_FLAG_HIDDEN);
    }
    if (G.s_lbl_lv) {
        lv_obj_add_flag(G.s_lbl_lv, LV_OBJ_FLAG_HIDDEN);
    }

    box = lv_obj_create(G.root);
    lv_obj_set_size(box, GW - 24, GH - 16);
    lv_obj_align(box, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_bg_color(box, lv_color_hex(BALL_COL_BAR), 0);
    lv_obj_set_style_border_color(box, lv_color_hex(0x2A3A55), 0);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_radius(box, 12, 0);
    lv_obj_set_style_pad_all(box, 8, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(box, 4, 0);

    rank = ball_rank_push(G.score, G.elapsed_s);

    tt = lv_label_create(box);
    lv_obj_set_style_text_font(tt, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(tt, lv_color_hex(BALL_COL_SCORE), 0);
    if (rank == 1) {
        lv_label_set_text_fmt(tt, "本局 %d 分  新纪录", G.score);
    } else {
        lv_label_set_text_fmt(tt, "本局 %d 分", G.score);
    }

    lb = lv_label_create(box);
    lv_obj_set_style_text_font(lb, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(lb, lv_color_hex(BALL_COL_TIME), 0);
    lv_label_set_text(lb, "TOP3");

    if (ball_rank_load(&rk) == 0) {
        for (i = 0; i < 3; i++) {
            row = lv_label_create(box);
            lv_obj_set_style_text_font(row, &lv_font_watch_16_cn, 0);
            lv_obj_set_style_text_color(row, lv_color_hex(0x9BB0C9), 0);
            lv_label_set_text_fmt(row, "%d.  %d", i + 1, rk.score[i]);
        }
    }

    hint = lv_label_create(box);
    lv_obj_set_style_text_font(hint, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(BALL_COL_LV), 0);
    lv_label_set_text(hint, "点按再来一局");
    lv_obj_set_style_pad_top(hint, 6, 0);

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
    G.elapsed_s = 0;
    G.combo_n = 0;
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
    if (G.s_lbl_score) {
        lv_obj_clear_flag(G.s_lbl_score, LV_OBJ_FLAG_HIDDEN);
    }
    if (G.s_lbl_time) {
        lv_obj_clear_flag(G.s_lbl_time, LV_OBJ_FLAG_HIDDEN);
    }
    if (G.s_lbl_lv) {
        lv_obj_clear_flag(G.s_lbl_lv, LV_OBJ_FLAG_HIDDEN);
    }
    ball_reset(G.canvas);
    G.playing = 1;
    watch_game_bgm_start(WATCH_BGM_ACTIVE);
    watch_ui_toast("开始! 倾斜设备移动小球");
}


/* ============================ 入口 ============================ */
lv_obj_t *app_ball_create(lv_obj_t *parent)
{
    lv_obj_t *root;
    lv_obj_t *bar;
    lv_obj_t *back;
    lv_obj_t *bl;
    u8 i;

    memset(&G, 0, sizeof(G));

    /* 全屏深色根: 避免顶栏空隙透出主页浅色脏区 */
    root = lv_obj_create(parent);
    lv_obj_set_size(root, LCD_W, LCD_H);
    lv_obj_set_style_bg_color(root, lv_color_hex(BALL_COL_BG), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_GESTURE_BUBBLE);
    G.root = root;

    /* 顶栏: 通栏深色, 仅 返回 | 分数 | 时间 | LV, 无标题避免重叠 */
    bar = lv_obj_create(root);
    lv_obj_set_size(bar, LCD_W, BALL_TOP_H);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(BALL_COL_BAR), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(0x243148), 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_hor(bar, 6, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    back = lv_btn_create(bar);
    lv_obj_set_size(back, 52, 26);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x1C2A40), 0);
    lv_obj_set_style_radius(back, 8, 0);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_add_event_cb(back, ball_back_cb, LV_EVENT_CLICKED, NULL);
    bl = lv_label_create(back);
    lv_obj_set_style_text_font(bl, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(bl, lv_color_hex(0x8EB6FF), 0);
    lv_label_set_text(bl, "返回");
    lv_obj_center(bl);

    G.s_lbl_score = lv_label_create(bar);
    lv_obj_set_style_text_font(G.s_lbl_score, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(G.s_lbl_score, lv_color_hex(BALL_COL_SCORE), 0);
    lv_label_set_text(G.s_lbl_score, "0");
    lv_obj_align(G.s_lbl_score, LV_ALIGN_LEFT_MID, 58, 0);

    G.s_lbl_time = lv_label_create(bar);
    lv_obj_set_style_text_font(G.s_lbl_time, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(G.s_lbl_time, lv_color_hex(BALL_COL_TIME), 0);
    lv_label_set_text(G.s_lbl_time, "30s");
    lv_obj_align(G.s_lbl_time, LV_ALIGN_CENTER, 0, 0);

    G.s_lbl_lv = lv_label_create(bar);
    lv_obj_set_style_text_font(G.s_lbl_lv, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(G.s_lbl_lv, lv_color_hex(BALL_COL_LV), 0);
    lv_label_set_text(G.s_lbl_lv, "LV1");
    lv_obj_align(G.s_lbl_lv, LV_ALIGN_RIGHT_MID, -4, 0);

    /* 玩法 canvas 仅占顶栏以下, 与顶栏分区刷新 */
    G.buf = (lv_color_t *)malloc((u32)GW * GH * sizeof(lv_color_t));
    if (!G.buf) {
        watch_ui_toast("内存不足");
        return root;
    }
    memset(G.buf, 0, (u32)GW * GH * sizeof(lv_color_t));
    G.canvas = lv_canvas_create(root);
    lv_canvas_set_buffer(G.canvas, G.buf, GW, GH, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_size(G.canvas, GW, GH);
    lv_obj_align(G.canvas, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_clear_flag(G.canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(G.canvas, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_move_foreground(bar);

    for (i = 0; i < 8; i++) {
        G.trail_x[i] = 0;
        G.trail_y[i] = 0;
    }

    if (imu_init()) {
        watch_ui_toast("陀螺仪未就绪, 无法控制小球");
    }

    ball_reset(G.canvas);
    G.playing = 1;
    watch_game_bgm_start(WATCH_BGM_ACTIVE);
    G.tick = lv_timer_create(ball_tick, BALL_MS, NULL);
    lv_obj_add_event_cb(root, ball_del_cb, LV_EVENT_DELETE, NULL);
    lv_obj_invalidate(root);
    return root;
}

void app_ball_exit(void)
{
    app_ball_cleanup();
}

static void app_ball_cleanup(void)
{
    watch_game_bgm_stop();
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

static void ball_back_cb(lv_event_t *e)
{
    (void)e;
    watch_ui_close_app();
}

/* app 根删除时释放 lv_timer 与堆缓冲(游戏退出/关机路径) */
static void ball_del_cb(lv_event_t *e)
{
    (void)e;
    app_ball_cleanup();
}

#else   /* !TCFG_IMU_ENABLE */

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
