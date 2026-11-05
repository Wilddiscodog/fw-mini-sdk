/*
 * 滑雪大冒险 —— 素材取自安卓 Ski Safari APK
 * 操作(教程 gui_tut_*): 点按跳跃; 空中按住后空翻; 落地姿态不对则摔倒
 *
 * 动画: player_skier_default 8x6 精灵表
 *   run 0-5 / flip 16-21 / air 22,28,29 / land 24-26 / crash 32-34
 */
#include "app_config.h"
#include "system/includes.h"
#include "syscfg/syscfg_id.h"
#include "lvgl.h"
#include "lcd_config.h"
#include "watch_ski.h"
#include "watch_style.h"
#include "watch_ui.h"
#include "watch_power.h"
#include "assets/ski_sprites.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define SKI_MS        33
#define SKI_TOP_H     32
#define GW            LCD_W
#define GH            (LCD_H - SKI_TOP_H)

#define COL_SKY0      0x2E6FA8
#define COL_SKY1      0x9AD0EC
#define COL_SNOW      0xF8FCFF
#define COL_SNOW2     0xCFE3F2
#define COL_BAR       0x123044
#define COL_GOLD      0xFFD54F
#define COL_WARN      0xFF5252

#define PLAYER_SX     92
#define MAX_OBJ       16
#define MAX_FX        24
#define JUMP_V        (-10.5f)
#define GRAVITY       20.0f
#define SPIN_RATE     520.0f   /* deg/s while holding */
#define LAND_OK_DEG   55.0f
#define CHROMA        0xE007u
#define RANK_MAGIC    0x494B5333u
#define RANK_CFG      56

enum ski_state {
    ST_RUN = 0,
    ST_JUMP,
    ST_FLIP,
    ST_LAND,
    ST_CRASH,
    ST_DEAD,
};

enum obj_kind {
    OBJ_ROCK = 0,
    OBJ_TREE,
    OBJ_COIN,
    OBJ_RAMP,
    OBJ_PENG,
};

struct obj {
    u8 used;
    u8 kind;
    u8 var;
    float x;
    float yoff;   /* 相对地面抬高(金币) */
    s16 hit_w;
    s16 hit_h;
};

struct fx {
    u8 used;
    float x, y, vx, vy;
    u8 life;
    u32 col;
};

static struct {
    lv_obj_t *root, *canvas, *lbl_score, *lbl_dist, *lbl_tip, *lbl_combo;
    lv_color_t *buf;
    lv_timer_t *tick;

    enum ski_state st;
    u8 touch;
    u8 mount;          /* 企鹅护体 */
    u8 boost;          /* 0..3 */
    u16 boost_ms;
    u16 land_ms;
    u16 crash_ms;
    u8 anim;
    u8 coin_fr;

    float world;
    float speed;
    float py;          /* 离地高度 */
    float pvy;
    float ang;         /* 空翻角 0..360 */
    float spin;        /* 角速度 */
    float avalanche;   /* 与玩家距离, 小则危险 */
    float spawn_cd;
    float slope;       /* 当前坡度(显示用) */

    u32 score;
    u32 coins;
    u32 dist_m;
    u32 flips_total;
    u8 combo;          /* 连击倍率 1..9 */
    u16 combo_ms;
    u8 near_miss;

    struct obj objs[MAX_OBJ];
    struct fx fxs[MAX_FX];
} G;

/* ============================ draw helpers ============================ */
static lv_color_t C(u32 v)
{
    return lv_color_hex(v);
}

static u32 lerp(u32 a, u32 b, int t)
{
    int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255;
    int br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
    return ((u32)(ar + (((br - ar) * t) >> 8)) << 16) |
           ((u32)(ag + (((bg - ag) * t) >> 8)) << 8) |
           (u32)(ab + (((bb - ab) * t) >> 8));
}

static void fill(lv_color_t *fb, s16 x, s16 y, s16 w, s16 h, lv_color_t c)
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

static void blit(lv_color_t *fb, s16 dx, s16 dy, const lv_img_dsc_t *img)
{
    const u16 *src;
    s16 w, h, x, y, sx, sy;
    u16 p;

    if (!img || !img->data) {
        return;
    }
    w = (s16)img->header.w;
    h = (s16)img->header.h;
    src = (const u16 *)img->data;
    for (y = 0; y < h; y++) {
        sy = dy + y;
        if ((u16)sy >= (u16)GH) {
            continue;
        }
        for (x = 0; x < w; x++) {
            sx = dx + x;
            if ((u16)sx >= (u16)GW) {
                continue;
            }
            p = src[y * w + x];
            if (p == CHROMA) {
                continue;
            }
            fb[sy * GW + sx].full = p;
        }
    }
}

static float ground_y(float wx)
{
    /* 起伏雪道: 缓坡+急坡交替, 可玩性关键 */
    return GH * 0.68f
           + sinf(wx * 0.009f) * 18.0f
           + sinf(wx * 0.023f + 0.8f) * 10.0f
           + sinf(wx * 0.041f + 2.1f) * 5.0f;
}

static float ground_slope(float wx)
{
    float y0 = ground_y(wx - 4.0f);
    float y1 = ground_y(wx + 4.0f);
    return (y1 - y0) * 0.5f; /* 约像素斜率 */
}

static void fx_add(float x, float y, float vx, float vy, u32 col, u8 life)
{
    u8 i;
    for (i = 0; i < MAX_FX; i++) {
        if (!G.fxs[i].used) {
            G.fxs[i].used = 1;
            G.fxs[i].x = x;
            G.fxs[i].y = y;
            G.fxs[i].vx = vx;
            G.fxs[i].vy = vy;
            G.fxs[i].col = col;
            G.fxs[i].life = life;
            return;
        }
    }
}

static void rank_push(u32 score)
{
    struct {
        u32 magic;
        u32 score[5];
    } rk;
    int i, j;
    if (syscfg_read(RANK_CFG, &rk, sizeof(rk)) != (int)sizeof(rk) ||
        rk.magic != RANK_MAGIC) {
        memset(&rk, 0, sizeof(rk));
        rk.magic = RANK_MAGIC;
    }
    for (i = 0; i < 5; i++) {
        if (score > rk.score[i]) {
            break;
        }
    }
    if (i >= 5) {
        return;
    }
    for (j = 4; j > i; j--) {
        rk.score[j] = rk.score[j - 1];
    }
    rk.score[i] = score;
    syscfg_write(RANK_CFG, &rk, sizeof(rk));
}

/* ============================ spawn ============================ */
static void spawn_one(float x)
{
    u8 i, r, kind;
    for (i = 0; i < MAX_OBJ; i++) {
        if (!G.objs[i].used) {
            break;
        }
    }
    if (i >= MAX_OBJ) {
        return;
    }
    r = (u8)(rand() % 100);
    /* 随距离提高障碍密度, 金币始终常见 */
    if (r < 26) {
        kind = OBJ_ROCK;
    } else if (r < 46) {
        kind = OBJ_TREE;
    } else if (r < 78) {
        kind = OBJ_COIN;
    } else if (r < 90) {
        kind = OBJ_RAMP;
    } else {
        kind = OBJ_PENG;
    }

    G.objs[i].used = 1;
    G.objs[i].kind = kind;
    G.objs[i].x = x;
    G.objs[i].yoff = 0;
    G.objs[i].var = (u8)(rand() & 3);
    switch (kind) {
    case OBJ_ROCK:
        G.objs[i].hit_w = 28;
        G.objs[i].hit_h = 26;
        break;
    case OBJ_TREE:
        G.objs[i].hit_w = 30;
        G.objs[i].hit_h = 46;
        break;
    case OBJ_COIN:
        G.objs[i].hit_w = 16;
        G.objs[i].hit_h = 16;
        G.objs[i].yoff = 28 + (rand() % 50);
        break;
    case OBJ_RAMP:
        G.objs[i].hit_w = 34;
        G.objs[i].hit_h = 14;
        break;
    default:
        G.objs[i].hit_w = 24;
        G.objs[i].hit_h = 28;
        break;
    }
}

static const lv_img_dsc_t *pick_skier(void)
{
    int fi;

    switch (G.st) {
    case ST_FLIP:
        /* 按空翻角选帧: 0°→frame0 ... 300°→frame5 */
        fi = ((int)G.ang) / 60;
        if (fi < 0) {
            fi = 0;
        }
        if (fi >= SKI_FLIP_N) {
            fi = SKI_FLIP_N - 1;
        }
        return ski_flip_tab[fi];
    case ST_JUMP:
        if (G.pvy < -2.0f) {
            return ski_air_tab[1];      /* 起跳上扬 */
        }
        if (G.py > 40.0f) {
            return ski_air_tab[0];      /* 平飞 */
        }
        return ski_air_tab[2];          /* 下落 */
    case ST_LAND:
        fi = G.land_ms / 80;
        if (fi >= SKI_LAND_N) {
            fi = SKI_LAND_N - 1;
        }
        return ski_land_tab[fi];
    case ST_CRASH:
    case ST_DEAD:
        fi = (G.crash_ms / 100) % SKI_CRASH_N;
        return ski_crash_tab[fi];
    case ST_RUN:
    default:
        if (G.speed > 6.5f) {
            return ski_up_tab[(G.anim / 2) % SKI_UP_N];
        }
        return ski_run_tab[(G.anim / 2) % SKI_RUN_N];
    }
}

/* ============================ render ============================ */
static void render(void)
{
    lv_color_t *fb = G.buf;
    s16 x, y, gy, py_s;
    u8 i;
    const lv_img_dsc_t *spr;

    if (!fb) {
        return;
    }

    for (y = 0; y < GH; y++) {
        lv_color_t lc = C(lerp(COL_SKY0, COL_SKY1, (y * 256) / GH));
        for (x = 0; x < GW; x++) {
            fb[y * GW + x] = lc;
        }
    }

    /* 远景树 */
    for (i = 0; i < 6; i++) {
        float wx = G.world * 0.3f + i * 70.0f;
        s16 sx = (s16)fmodf(wx, (float)(GW + 70)) - 35;
        blit(fb, sx, (s16)(GH * 0.34f), ski_tree_tab[i & 1]);
    }

    /* 雪道 */
    for (x = 0; x < GW; x++) {
        float wx = G.world + (x - PLAYER_SX);
        gy = (s16)ground_y(wx);
        for (y = gy; y < GH; y++) {
            fb[y * GW + x] = C(y < gy + 4 ? COL_SNOW : COL_SNOW2);
        }
        if ((u16)gy < (u16)GH) {
            fb[gy * GW + x] = C(0xFFFFFF);
        }
    }

    /* 雪崩墙 */
    {
        s16 ax = (s16)(PLAYER_SX - G.avalanche);
        if (ax > -40) {
            s16 x0 = ax < 0 ? 0 : ax;
            for (x = 0; x < x0 + 26 && x < GW; x++) {
                for (y = 0; y < GH; y++) {
                    fb[y * GW + x] = C(((x + y + (int)G.world) & 5) ? 0xECEFF1 : 0x90CAF9);
                }
            }
            if (G.avalanche < 70.0f) {
                fill(fb, x0 + 22, 0, 3, GH, C(COL_WARN));
            }
        }
    }

    /* 障碍 */
    for (i = 0; i < MAX_OBJ; i++) {
        struct obj *o = &G.objs[i];
        s16 sx, by;
        if (!o->used) {
            continue;
        }
        sx = (s16)(o->x - G.world + PLAYER_SX);
        if (sx < -70 || sx > GW + 50) {
            continue;
        }
        by = (s16)ground_y(o->x);
        if (o->kind == OBJ_ROCK) {
            spr = ski_rock_tab[o->var % SKI_ROCK_N];
            blit(fb, sx - spr->header.w / 2, by - spr->header.h + 5, spr);
        } else if (o->kind == OBJ_TREE) {
            spr = ski_tree_tab[o->var % SKI_TREE_N];
            blit(fb, sx - spr->header.w / 2, by - spr->header.h + 6, spr);
        } else if (o->kind == OBJ_COIN) {
            spr = ski_coin_tab[(G.coin_fr + o->var) % SKI_COIN_N];
            blit(fb, sx - spr->header.w / 2, by - (s16)o->yoff - spr->header.h / 2, spr);
        } else if (o->kind == OBJ_RAMP) {
            fill(fb, sx - 20, by - 5, 40, 5, C(0x8D6E63));
            fill(fb, sx + 0, by - 24, 22, 20, C(0xA1887F));
            fill(fb, sx + 18, by - 28, 8, 8, C(0xBCAAA4));
        } else {
            spr = ski_peng_tab[(G.anim / 3) % SKI_PENG_N];
            blit(fb, sx - spr->header.w / 2, by - spr->header.h + 4, spr);
        }
    }

    /* 粒子 */
    for (i = 0; i < MAX_FX; i++) {
        if (!G.fxs[i].used) {
            continue;
        }
        fill(fb, (s16)G.fxs[i].x, (s16)G.fxs[i].y, 2, 2, C(G.fxs[i].col));
    }

    /* 玩家 */
    gy = (s16)ground_y(G.world);
    py_s = (s16)(gy - G.py);
    spr = pick_skier();
    blit(fb, PLAYER_SX - spr->header.w / 2, py_s - spr->header.h + 10, spr);
    if (G.mount) {
        blit(fb, PLAYER_SX - 30, py_s - 16, ski_peng_tab[0]);
    }

    /* 加速尾迹 */
    if (G.boost) {
        u32 fc = G.boost >= 3 ? 0x40C4FF : (G.boost >= 2 ? 0xFF5252 : 0xFFD600);
        fill(fb, PLAYER_SX - 34 - G.boost * 5, py_s - 6, 12 + G.boost * 4, 5, C(fc));
    }

    /* 雪屑 */
    if (G.st == ST_RUN && G.speed > 3.0f) {
        for (i = 0; i < 4; i++) {
            s16 px = PLAYER_SX - 8 - (rand() % 18);
            s16 py = py_s + 2 + (rand() % 5);
            if ((u16)px < (u16)GW && (u16)py < (u16)GH) {
                fb[py * GW + px] = C(0xFFFFFF);
            }
        }
    }

    /* 雪怪威胁 */
    if (SKI_YETI_N > 0) {
        blit(fb, GW - 32, 2, ski_yeti_tab[0]);
        if (G.avalanche < 60.0f) {
            fill(fb, GW - 34, 0, 30, 2, C(COL_WARN));
        }
    }

    lv_obj_invalidate(G.canvas);
}

/* ============================ logic ============================ */
static void set_tip(const char *s)
{
    if (G.lbl_tip) {
        lv_obj_clear_flag(G.lbl_tip, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(G.lbl_tip, s);
    }
}

static void game_over(const char *why)
{
    G.st = ST_DEAD;
    G.crash_ms = 0;
    rank_push(G.score);
    if (G.lbl_tip) {
        lv_obj_clear_flag(G.lbl_tip, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(G.lbl_tip, "%s  点按重开", why ? why : "游戏结束");
    }
}

static void crash(const char *why)
{
    if (G.mount) {
        G.mount = 0;
        G.st = ST_JUMP;
        G.py = 22;
        G.pvy = JUMP_V * 0.4f;
        G.ang = 0;
        G.spin = 0;
        watch_ui_toast("企鹅护体!");
        return;
    }
    G.st = ST_CRASH;
    G.crash_ms = 0;
    G.boost = 0;
    G.boost_ms = 0;
    G.combo = 1;
    set_tip(why);
}

static void ski_reset(void)
{
    u8 i;
    memset(G.objs, 0, sizeof(G.objs));
    memset(G.fxs, 0, sizeof(G.fxs));
    G.st = ST_RUN;
    G.touch = 0;
    G.mount = 0;
    G.boost = 0;
    G.boost_ms = 0;
    G.land_ms = 0;
    G.crash_ms = 0;
    G.anim = 0;
    G.coin_fr = 0;
    G.world = 0;
    G.speed = 3.8f;
    G.py = 0;
    G.pvy = 0;
    G.ang = 0;
    G.spin = 0;
    G.avalanche = 140.0f;
    G.spawn_cd = 40;
    G.score = 0;
    G.coins = 0;
    G.dist_m = 0;
    G.flips_total = 0;
    G.combo = 1;
    G.combo_ms = 0;
    for (i = 0; i < 5; i++) {
        spawn_one(G.world + 200 + i * 85 + (rand() % 30));
    }
    set_tip("点按跳跃  空中长按空翻");
}

static void do_land(void)
{
    float a = G.ang;
    while (a > 180.0f) {
        a -= 360.0f;
    }
    while (a < -180.0f) {
        a += 360.0f;
    }
    /* 落地姿态: 接近水平才算成功 */
    if (fabsf(a) > LAND_OK_DEG && G.st == ST_FLIP) {
        crash("空翻摔倒");
        G.py = 0;
        G.pvy = 0;
        G.ang = 0;
        G.spin = 0;
        return;
    }
    if (G.st == ST_FLIP && G.ang >= 300.0f) {
        /* 接近一整圈 */
        u8 laps = (u8)(G.ang / 360.0f);
        if (laps < 1) {
            laps = 1;
        }
        if (laps > 3) {
            laps = 3;
        }
        G.boost += laps;
        if (G.boost > 3) {
            G.boost = 3;
        }
        G.boost_ms = 1600;
        G.flips_total += laps;
        G.score += 80 * laps * G.combo;
        G.combo++;
        if (G.combo > 9) {
            G.combo = 9;
        }
        G.combo_ms = 2500;
        G.avalanche += 18.0f * laps;
        for (u8 k = 0; k < 10; k++) {
            fx_add(PLAYER_SX, ground_y(G.world) - 8,
                   (float)(rand() % 9 - 4), -(float)(rand() % 6 + 1),
                   0xFFD600, 22);
        }
    }
    G.py = 0;
    G.pvy = 0;
    G.ang = 0;
    G.spin = 0;
    G.st = ST_LAND;
    G.land_ms = 0;
}

static void ski_step(void)
{
    float dt = SKI_MS / 1000.0f;
    float target, chase;
    u8 i;

    if (G.st == ST_DEAD) {
        G.crash_ms += SKI_MS;
        return;
    }

    G.anim++;
    if ((G.anim & 3) == 0) {
        G.coin_fr++;
    }
    if (G.combo_ms) {
        G.combo_ms = G.combo_ms > SKI_MS ? G.combo_ms - SKI_MS : 0;
        if (!G.combo_ms) {
            G.combo = 1;
        }
    }

    /* 速度 / 距离 */
    target = 3.8f + G.dist_m * 0.006f + G.boost * 1.2f;
    if (target > 10.0f) {
        target = 10.0f;
    }
    if (G.st == ST_CRASH) {
        target *= 0.35f;
    }
    G.speed += (target - G.speed) * 0.12f;
    G.world += G.speed;
    G.dist_m = (u32)(G.world / 7.5f);
    G.score += (u32)((1 + G.boost) * G.combo);
    G.slope = ground_slope(G.world);

    if (G.boost_ms) {
        if (G.boost_ms > SKI_MS) {
            G.boost_ms -= SKI_MS;
        } else {
            G.boost_ms = 0;
            if (G.boost) {
                G.boost--;
                G.boost_ms = G.boost ? 1000 : 0;
            }
        }
    }

    /* 雪崩 */
    chase = 2.8f + G.dist_m * 0.004f;
    G.avalanche += (G.speed - chase) * 1.1f;
    if (G.avalanche > 180.0f) {
        G.avalanche = 180.0f;
    }
    if (G.avalanche < 18.0f) {
        game_over("雪崩吞没");
        return;
    }

    /* 状态机 */
    if (G.st == ST_CRASH) {
        G.crash_ms += SKI_MS;
        G.py = 0;
        if (G.crash_ms > 900) {
            game_over("摔倒了");
        }
        goto AFTER_PHYS;
    }

    if (G.st == ST_LAND) {
        G.land_ms += SKI_MS;
        G.py = 0;
        if (G.land_ms > 180) {
            G.st = ST_RUN;
        }
        goto AFTER_PHYS;
    }

    if (G.st == ST_RUN) {
        G.py = 0;
        G.pvy = 0;
        G.ang = 0;
        /* 下坡略加速已体现在 ground 起伏感 */
    } else if (G.st == ST_JUMP || G.st == ST_FLIP) {
        if (G.touch && G.st == ST_JUMP) {
            G.st = ST_FLIP;
            G.spin = SPIN_RATE;
        }
        if (G.st == ST_FLIP) {
            if (G.touch) {
                G.spin = SPIN_RATE;
            } else {
                G.spin *= 0.97f; /* 松开仍惯性旋转 */
            }
            G.ang += G.spin * dt;
            if (G.ang > 1080.0f) {
                G.ang = 1080.0f; /* 最多约 3 圈视觉 */
            }
        }
        G.pvy += GRAVITY * dt;
        G.py -= G.pvy;
        if (G.py <= 0.0f) {
            do_land();
        }
    }

AFTER_PHYS:
    /* 生成: 保证间距, 避免无解 */
    G.spawn_cd -= G.speed;
    if (G.spawn_cd <= 0 && G.st != ST_CRASH && G.st != ST_DEAD) {
        float gap = 70.0f + (rand() % 55) - G.boost * 4;
        if (gap < 55.0f) {
            gap = 55.0f;
        }
        G.spawn_cd = gap;
        spawn_one(G.world + GW + 40 + (rand() % 40));
    }

    /* 碰撞 */
    if (G.st != ST_CRASH && G.st != ST_DEAD && G.st != ST_LAND) {
        for (i = 0; i < MAX_OBJ; i++) {
            struct obj *o = &G.objs[i];
            float sx, dx;
            s16 clear_h;
            if (!o->used) {
                continue;
            }
            sx = o->x - G.world + PLAYER_SX;
            if (sx < -80) {
                o->used = 0;
                continue;
            }
            dx = sx - PLAYER_SX;
            if (fabsf(dx) > o->hit_w * 0.55f + 14.0f) {
                continue;
            }

            if (o->kind == OBJ_COIN) {
                if (fabsf(G.py - o->yoff) < 26.0f) {
                    o->used = 0;
                    G.coins++;
                    G.score += 20 * G.combo;
                    fx_add(sx, ground_y(o->x) - o->yoff, 0, -2.5f, COL_GOLD, 18);
                }
                continue;
            }
            if (o->kind == OBJ_PENG) {
                if (G.py < 18.0f) {
                    o->used = 0;
                    G.mount = 1;
                    G.score += 40;
                    watch_ui_toast("骑上企鹅!");
                }
                continue;
            }
            if (o->kind == OBJ_RAMP) {
                if (G.py < 12.0f && (G.st == ST_RUN || G.st == ST_JUMP)) {
                    G.st = ST_JUMP;
                    G.pvy = JUMP_V * 1.15f;
                    G.py = 10;
                    G.ang = 0;
                    G.spin = 0;
                    G.score += 15;
                    G.touch = 0; /* 需再按才能空翻 */
                }
                continue;
            }

            /* 障碍: 脚底高于障碍顶则越过 */
            clear_h = o->hit_h - 4;
            if (G.boost >= 2 && o->kind == OBJ_ROCK) {
                o->used = 0;
                G.score += 25;
                fx_add(sx, ground_y(o->x) - 10, -2, -3, 0x90A4AE, 16);
                continue;
            }
            if (G.py > (float)clear_h) {
                if (fabsf(dx) < 10.0f && !G.near_miss) {
                    G.near_miss = 1;
                    G.score += 5;
                }
                continue;
            }
            G.near_miss = 0;
            o->used = 0;
            crash(o->kind == OBJ_TREE ? "撞到树" : "撞到石头");
            return;
        }
    } else {
        G.near_miss = 0;
    }

    /* 粒子更新 */
    for (i = 0; i < MAX_FX; i++) {
        if (!G.fxs[i].used) {
            continue;
        }
        G.fxs[i].x += G.fxs[i].vx;
        G.fxs[i].y += G.fxs[i].vy;
        G.fxs[i].vy += 0.25f;
        if (!G.fxs[i].life--) {
            G.fxs[i].used = 0;
        }
    }

    if (G.lbl_score) {
        lv_label_set_text_fmt(G.lbl_score, "%u", G.score);
    }
    if (G.lbl_dist) {
        if (G.boost) {
            lv_label_set_text_fmt(G.lbl_dist, "%um x%d", G.dist_m, G.boost);
        } else {
            lv_label_set_text_fmt(G.lbl_dist, "%um", G.dist_m);
        }
    }
    if (G.lbl_combo) {
        if (G.combo > 1) {
            lv_obj_clear_flag(G.lbl_combo, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text_fmt(G.lbl_combo, "COMBO x%d", G.combo);
        } else {
            lv_obj_add_flag(G.lbl_combo, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (G.lbl_tip && G.dist_m > 8 && G.st != ST_DEAD && G.st != ST_CRASH) {
        lv_obj_add_flag(G.lbl_tip, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ski_tick(lv_timer_t *t)
{
    (void)t;
    if (!watch_power_input_enabled()) {
        return;
    }
    ski_step();
    render();
}

static void ski_touch_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (!watch_power_input_enabled()) {
        return;
    }
    if (G.st == ST_DEAD) {
        if (code == LV_EVENT_PRESSED || code == LV_EVENT_CLICKED) {
            ski_reset();
        }
        return;
    }
    if (code == LV_EVENT_PRESSED) {
        G.touch = 1;
        if (G.st == ST_RUN || G.st == ST_LAND) {
            G.st = ST_JUMP;
            G.pvy = JUMP_V;
            G.py = 5;
            G.ang = 0;
            G.spin = 0;
            fx_add(PLAYER_SX, ground_y(G.world), -1, -1, 0xFFFFFF, 10);
        } else if (G.st == ST_JUMP) {
            G.st = ST_FLIP;
            G.spin = SPIN_RATE;
        }
    } else if (code == LV_EVENT_PRESSING) {
        G.touch = 1;
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        G.touch = 0;
    }
}

static void ski_back_cb(lv_event_t *e)
{
    (void)e;
    watch_ui_close_app();
}

static void app_ski_cleanup(void);
static void ski_del_cb(lv_event_t *e)
{
    (void)e;
    app_ski_cleanup();
}

lv_obj_t *app_ski_create(lv_obj_t *parent)
{
    lv_obj_t *root, *bar, *back, *bl;

    memset(&G, 0, sizeof(G));
    root = lv_obj_create(parent);
    lv_obj_set_size(root, LCD_W, LCD_H);
    lv_obj_set_style_bg_color(root, C(COL_SKY0), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_GESTURE_BUBBLE);
    G.root = root;

    bar = lv_obj_create(root);
    lv_obj_set_size(bar, LCD_W, SKI_TOP_H);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(bar, C(COL_BAR), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_hor(bar, 4, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    back = lv_btn_create(bar);
    lv_obj_set_size(back, 48, 24);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(back, C(0x1E4A66), 0);
    lv_obj_set_style_radius(back, 8, 0);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_add_event_cb(back, ski_back_cb, LV_EVENT_CLICKED, NULL);
    bl = lv_label_create(back);
    lv_obj_set_style_text_font(bl, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(bl, C(0xB3E5FC), 0);
    lv_label_set_text(bl, "返回");
    lv_obj_center(bl);

    G.lbl_score = lv_label_create(bar);
    lv_obj_set_style_text_font(G.lbl_score, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(G.lbl_score, C(COL_GOLD), 0);
    lv_label_set_text(G.lbl_score, "0");
    lv_obj_align(G.lbl_score, LV_ALIGN_LEFT_MID, 54, 0);

    G.lbl_dist = lv_label_create(bar);
    lv_obj_set_style_text_font(G.lbl_dist, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(G.lbl_dist, C(0xE1F5FE), 0);
    lv_label_set_text(G.lbl_dist, "0m");
    lv_obj_align(G.lbl_dist, LV_ALIGN_CENTER, 8, 0);

    G.lbl_combo = lv_label_create(bar);
    lv_obj_set_style_text_font(G.lbl_combo, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(G.lbl_combo, C(0x69F0AE), 0);
    lv_label_set_text(G.lbl_combo, "");
    lv_obj_align(G.lbl_combo, LV_ALIGN_RIGHT_MID, -36, 0);
    lv_obj_add_flag(G.lbl_combo, LV_OBJ_FLAG_HIDDEN);

    G.lbl_tip = lv_label_create(root);
    lv_obj_set_style_text_font(G.lbl_tip, &lv_font_watch_16_cn, 0);
    lv_obj_set_style_text_color(G.lbl_tip, C(0x0D47A1), 0);
    lv_label_set_text(G.lbl_tip, "点按跳跃  空中长按空翻");
    lv_obj_align(G.lbl_tip, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_clear_flag(G.lbl_tip, LV_OBJ_FLAG_CLICKABLE);

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
    lv_obj_add_flag(G.canvas, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(G.canvas, ski_touch_cb, LV_EVENT_ALL, NULL);
    lv_obj_move_foreground(bar);
    lv_obj_move_foreground(G.lbl_tip);

    ski_reset();
    render();
    watch_ui_toast("逃离雪崩!");
    G.tick = lv_timer_create(ski_tick, SKI_MS, NULL);
    lv_obj_add_event_cb(root, ski_del_cb, LV_EVENT_DELETE, NULL);
    return root;
}

void app_ski_exit(void)
{
    app_ski_cleanup();
}

static void app_ski_cleanup(void)
{
    if (G.tick) {
        lv_timer_del(G.tick);
        G.tick = NULL;
    }
    if (G.buf) {
        free(G.buf);
        G.buf = NULL;
    }
}
