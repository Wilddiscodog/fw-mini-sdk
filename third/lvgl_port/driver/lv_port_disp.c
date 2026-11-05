#if 1

#include "lcd_config.h"
#include "system/includes.h"
#include "lvgl.h"
#include "lcd_config.h"

extern void lcd_wait_te(void);
extern void lcd_lvgl_full(u16 xs, u16 xe, u16 ys, u16 ye, char *img);

static void disp_init(void);
static void disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p);

/* FPS / flush 统计 */
static u32 s_fps_frames;
static u32 s_flush_ms_acc;
static u32 s_flush_ms_max;
static u32 s_flush_cnt;
static u32 s_fps_last_ms;
static u8 s_frame_need_te = 1;

/* 整屏双缓冲: 296*240*2 ≈ 138KB ×2 ≈ 276KB, 走 SDRAM 堆 */
#define LCD_FB_PX   ((u32)LCD_W * (u32)LCD_H)

void lv_port_disp_init(void)
{
    static lv_disp_draw_buf_t draw_buf_dsc;
    static lv_disp_drv_t disp_drv;
    lv_color_t *buf1 = NULL;
    lv_color_t *buf2 = NULL;
    u8 full = 0;

    disp_init();

    buf1 = (lv_color_t *)malloc(LCD_FB_PX * sizeof(lv_color_t));
    buf2 = (lv_color_t *)malloc(LCD_FB_PX * sizeof(lv_color_t));
    if (buf1 && buf2) {
        lv_disp_draw_buf_init(&draw_buf_dsc, buf1, buf2, LCD_FB_PX);
        full = 1;
    } else {
        const u32 rows = 80;
        if (buf1) {
            free(buf1);
        }
        if (buf2) {
            free(buf2);
        }
        buf1 = (lv_color_t *)malloc(LCD_W * rows * sizeof(lv_color_t));
        buf2 = (lv_color_t *)malloc(LCD_W * rows * sizeof(lv_color_t));
        if (!buf1 || !buf2) {
            printf("[lcd] ERROR: no draw buffer\n");
            return;
        }
        lv_disp_draw_buf_init(&draw_buf_dsc, buf1, buf2, LCD_W * rows);
        printf("[lcd] fallback row buf %u lines\n", (unsigned)rows);
    }

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_W;
    disp_drv.ver_res = LCD_H;
    disp_drv.flush_cb = disp_flush;
    disp_drv.draw_buf = &draw_buf_dsc;
    /* 整屏缓冲: 每帧一次连续写, 配合帧首 TE, 显著减撕裂 */
    if (full) {
        disp_drv.full_refresh = 1;
    }

    lv_disp_drv_register(&disp_drv);
    s_fps_last_ms = timer_get_ms();
    s_frame_need_te = 1;
}

static void disp_init(void)
{
    extern void user_lcd_init(void);
    user_lcd_init();
}

volatile bool disp_flush_enabled = true;

void disp_enable_update(void)
{
    disp_flush_enabled = true;
}

void disp_disable_update(void)
{
    disp_flush_enabled = false;
}

static void disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    u32 t0, t1, ms;
    u32 now, dt;

    /* 息屏时跳过 TE 等待与推屏, 降低功耗 */
    if (!disp_flush_enabled) {
        lv_disp_flush_ready(disp_drv);
        return;
    }

    /* 每帧第一片对齐 TE 上升沿, 再开写 → 写入落在消隐后, 减撕裂 */
    if (s_frame_need_te) {
        lcd_wait_te();
        s_frame_need_te = 0;
    }

    t0 = timer_get_ms();
    if (disp_flush_enabled) {
        lcd_lvgl_full(area->x1, area->x2, area->y1, area->y2, (u16 *)color_p);
    }
    t1 = timer_get_ms();
    ms = (t1 >= t0) ? (t1 - t0) : 0;

    s_flush_ms_acc += ms;
    s_flush_cnt++;
    if (ms > s_flush_ms_max) {
        s_flush_ms_max = ms;
    }

    if (lv_disp_flush_is_last(disp_drv)) {
        s_fps_frames++;
        s_frame_need_te = 1; /* 下一帧再等 TE */
    }

    now = timer_get_ms();
    dt = now - s_fps_last_ms;
    if (dt >= 1000u) {
        s_fps_frames = 0;
        s_flush_ms_acc = 0;
        s_flush_ms_max = 0;
        s_flush_cnt = 0;
        s_fps_last_ms = now;
    }

    lv_disp_flush_ready(disp_drv);
}

#endif
