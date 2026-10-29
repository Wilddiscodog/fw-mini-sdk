/*
 * LCD 驱动 —— JD9853 8bit 8080 并口 (xiaolu-R2 设备屏, 方形 240x320 QVGA TFT)
 *
 * 来源与限制(重要):
 *  - 型号与流程来自 xiaolu-R2 原机串口日志: "LCD_JD9853 init_start, touch id[0xB6],
 *    mode id[0xA]" → "LCD_JD9853 config succes / LCD find JD9853"。
 *  - 原机 flash dump(16MB)代码段与资源段均为压缩/加密态, 无法静态提取字节级
 *    init 表; 原厂 AC79 SDK 也无 jd9853 驱动(仅 ili/st77xx 系列)。
 *  - 本文件: 基本命令(SLPOUT/COLMOD/MADCTL/CASET/RASET/RAMWR/DISPON)为 MIPI DCS
 *    标准, JD9853 全兼容; 【面板调优段】采用 JD9853 公开参考序列, 若点亮后出现
 *    色偏/亮度异常/首行错位, 替换 PANEL_TUNING 区(需抓原机 RAM 或厂商规格书)。
 *
 * 接口: EMI 8080-8bit, 引脚固定于 zc-p25136 原理图(见 board.c LCD 段注释):
 *   PC1~PC8=D0~D7, PC10=WR(以上 EMI 硬件复用);
 *   PC9=CS —— EMI 硬件不输出 CS, 由 lcd_cs_pinstate 软件驱动(board.c cs_pin);
 *   PB5=RS, PB7=RESET, PC0=TE, PA6=背光(GPIO, 网络名 LED_PWM);
 *   PB6=RD —— 芯片无 LCD_RD 复用功能(EMI 只写), 必须软件输出高,
 *   否则 RD 悬空可能被干扰导致面板误读、数据错乱。
 */
#include "system/includes.h"
#include "typedef.h"
#include "app_config.h"
#include "asm/gpio.h"
#include "lcd_drive.h"
#include "lcd_te_driver.h"
#include "lcd_config.h"

#if TCFG_LCD_JD9853_ENABLE

/* LCD_W/H 由 lcd_config.h 按 HORIZONTAL_SCREEN 给出:
 * 竖屏 240x320 / 横屏 320x240(逆时针 90°) */

#define ROTATE_DEGREE_0    0
#define ROTATE_DEGREE_90   1
#define ROTATE_DEGREE_180  2
#define ROTATE_DEGREE_270  3

#if HORIZONTAL_SCREEN
#define JD9853_INIT_ROTATE  ROTATE_DEGREE_270
#else
/* 上电实测基准方向反 180°, 故竖屏默认 180(用户 2026-10 现场校准) */
#define JD9853_INIT_ROTATE  ROTATE_DEGREE_180
#endif

void msleep(unsigned int ms);
#define lcd_delay(x) msleep(x)

/* ---- 复用 lcd_drive 的 8bit 并口原语(与 lcd_ili9341.c 同款) ---- */
static void WriteCOM(u8 cmd)
{
    lcd_cs_pinstate(0);
    lcd_rs_pinstate(0);
    lcd_send_byte(cmd);
    lcd_cs_pinstate(1);
}

static void WriteDAT_8(u8 dat)
{
    lcd_cs_pinstate(0);
    lcd_rs_pinstate(1);
    lcd_send_byte(dat);
    lcd_cs_pinstate(1);
}

static void WriteDAT_DMA(u8 *dat, u32 len)
{
    lcd_cs_pinstate(0);
    lcd_rs_pinstate(1);
    lcd_send_map(dat, len);
    lcd_cs_pinstate(1);
}

static void JD9853_io_init(void)
{
    gpio_direction_output(IO_PORTB_06, 1);  /* LCD_RD=PB6 拉高(只写接口) */
    lcd_rs_pinstate(0);
    lcd_cs_pinstate(1);   /* CS 空闲态拉高(board.c cs_pin=PC9, 写时自动拉低) */
}

/* ================= Jadard JD9853 厂商初始化段 =================
 * 旧"调优段"(C0=GVDD/C1=VCOM/C5=FRM...)是 ILI9341 风格命令, 对 JD9853 无效:
 * Jadard 的 C0/C1 是时序寄存器, 且厂商寄存器必须先 0xDF={0x98,0x53} 解锁。
 * 本段自 esp_lcd_jd9853(Apache-2.0, BOE 1.83" 参考)移植, 电源/Gamma 参数
 * 与面板批次相关, 点亮后色偏/发暗再微调。多字节命令用 WriteDAT_N 展开。 */
static void WriteDAT_N(const u8 *d, u8 n)
{
    while (n--) {
        WriteDAT_8(*d++);
    }
}

static void JD9853_reg_cfg(void)
{
    /* 复位时序(对齐 zc 日志: LCD init_start 在触摸 id 读取后) */
    lcd_rst_pinstate(1);
    lcd_delay(10);
    lcd_rst_pinstate(0);
    lcd_delay(20);
    lcd_rst_pinstate(1);
    lcd_delay(120);

    /* ---- 厂商段(sleep 态下写, Jadard 参考同序) ---- */
    WriteCOM(0xDF);
    {
        static const u8 d[] = {0x98, 0x53};          /* 解锁厂商寄存器 */
        WriteDAT_N(d, sizeof(d));
    }
    WriteCOM(0xDE);
    WriteDAT_8(0x00);                                /* page 0 */
    {
        static const u8 b2[] = {0x25};
        static const u8 b7[] = {0x00, 0x21, 0x00, 0x49};
        static const u8 bb[] = {0x1F, 0x9A, 0x55, 0x73, 0x63, 0xF0};
        static const u8 c0[] = {0x22, 0x22};
        static const u8 c1[] = {0x12};
        static const u8 c3[] = {0x7D, 0x07, 0x14, 0x06, 0xC8, 0x6A, 0x6C, 0x77};
        static const u8 c4[] = {0x00, 0x00, 0xA0, 0x6F, 0x1E, 0x1A, 0x16, 0x79, 0x1E, 0x1A, 0x16, 0x82};
        static const u8 c8[] = {
            0x3F, 0x2C, 0x26, 0x20, 0x25, 0x26, 0x21, 0x21,
            0x1F, 0x1F, 0x1F, 0x13, 0x11, 0x0B, 0x04, 0x00,
            0x3F, 0x2C, 0x26, 0x20, 0x25, 0x27, 0x21, 0x21,
            0x1F, 0x1F, 0x1F, 0x13, 0x11, 0x0B, 0x04, 0x00
        };
        static const u8 d0[] = {0x04, 0x06, 0x62, 0x0F, 0x00};
        static const u8 d7[] = {0x00, 0x30};
        WriteCOM(0xB2); WriteDAT_N(b2, sizeof(b2));
        WriteCOM(0xB7); WriteDAT_N(b7, sizeof(b7));
        WriteCOM(0xBB); WriteDAT_N(bb, sizeof(bb));
        WriteCOM(0xC0); WriteDAT_N(c0, sizeof(c0));
        WriteCOM(0xC1); WriteDAT_N(c1, sizeof(c1));
        WriteCOM(0xC3); WriteDAT_N(c3, sizeof(c3));
        WriteCOM(0xC4); WriteDAT_N(c4, sizeof(c4));
        WriteCOM(0xC8); WriteDAT_N(c8, sizeof(c8));  /* Gamma G2.2 */
        WriteCOM(0xD0); WriteDAT_N(d0, sizeof(d0));
        WriteCOM(0xD7); WriteDAT_N(d7, sizeof(d7));
        WriteCOM(0xE6); WriteDAT_8(0x14);
    }
    WriteCOM(0xDE);
    WriteDAT_8(0x01);                                /* page 1 */
    {
        static const u8 b7[] = {0x03, 0x13, 0xEF, 0x35, 0x35};
        static const u8 c1[] = {0x14, 0x15, 0xC0};
        static const u8 c2[] = {0x06, 0x3A, 0xC7};
        static const u8 c4[] = {0x72, 0x12};
        WriteCOM(0xB7); WriteDAT_N(b7, sizeof(b7));
        WriteCOM(0xC1); WriteDAT_N(c1, sizeof(c1));
        WriteCOM(0xC2); WriteDAT_N(c2, sizeof(c2));
        WriteCOM(0xC4); WriteDAT_N(c4, sizeof(c4));
        WriteCOM(0xBE); WriteDAT_8(0x00);
    }
    WriteCOM(0xDE);
    WriteDAT_8(0x00);                                /* 回 page 0 */

    /* ---- DCS 标准段 ---- */
    WriteCOM(0x11);            /* Sleep Out */
    lcd_delay(120);

    WriteCOM(0x3A);            /* COLMOD: RGB565 */
    WriteDAT_8(0x55);
    WriteCOM(0x36);            /* MADCTL: 竖屏 + BGR 位(bit3): 本屏 BGR 排列,
                                * 不置位则红蓝互换(实测 黑白红绿蓝→黑白蓝红绿) */
    WriteDAT_8(0x08);

    /* 不发 0x21(INVON): 本屏为 TN 型(常态白), 反相开会把整屏取反
     * (实测: 黑白红绿蓝→白黑黄青紫)。若换 IPS 屏色反了再加回。 */

    WriteCOM(0x29);            /* Display ON */
    lcd_delay(50);             /* zc 日志: init_start→config succes ~300ms, 吻合 */
}

static void JD9853_SetRange(u16 xs, u16 xe, u16 ys, u16 ye)
{
    WriteCOM(0x2A);
    WriteDAT_8(xs >> 8);
    WriteDAT_8(xs);
    WriteDAT_8(xe >> 8);
    WriteDAT_8(xe);
    WriteCOM(0x2B);
    WriteDAT_8(ys >> 8);
    WriteDAT_8(ys);
    WriteDAT_8(ye >> 8);
    WriteDAT_8(ye);
}

void JD9853_set_direction(u8 dir)
{
    /* MADCTL: bit3=BGR 必须保留; MV/MX/MY 控制旋转
     * 0°=0x08, 90°CCW=0x68(MX|MV|BGR), 180°=0xC8, 270°=0xA8 */
    WriteCOM(0x36);
    if (dir == ROTATE_DEGREE_0) {
        WriteDAT_8(0x08);
    } else if (dir == ROTATE_DEGREE_90) {
        WriteDAT_8(0x68);
    } else if (dir == ROTATE_DEGREE_180) {
        WriteDAT_8(0xC8);
    } else {
        WriteDAT_8(0xA8);
    }
    JD9853_SetRange(0, LCD_W - 1, 0, LCD_H - 1);
}

void JD9853_clear_screen(u16 color)
{
    WriteCOM(0x2C);
    u8 *buf = malloc(LCD_W * LCD_H * 2);
    if (!buf) {
        printf("no mem in %s\n", __func__);
        return;
    }
    for (u32 i = 0; i < LCD_W * LCD_H; i++) {
        buf[2 * i] = (color >> 8) & 0xff;
        buf[2 * i + 1] = color & 0xff;
    }
    WriteDAT_DMA(buf, LCD_W * LCD_H * 2);
    free(buf);
}

static void JD9853_Fill(u8 *img, u16 len)
{
    WriteCOM(0x2C);
    WriteDAT_DMA(img, len);
}

static void JD9853_lvgl_Fill(u16 xs, u16 xe, u16 ys, u16 ye, char *img)
{
    u32 len = (xe + 1 - xs) * (ye + 1 - ys) * 2;
    JD9853_SetRange(xs, xe, ys, ye);
    WriteCOM(0x2C);
    WriteDAT_DMA((u8 *)img, len);
}

static void JD9853_draw(u8 *map, u32 size)
{
    ui_send_data_ready(map, size);
}

static void JD9853_reset(void) {}
static void JD9853_led_ctrl(u8 status)
{
    lcd_bl_pinstate(status ? BL_ON : BL_OFF);
}

static int JD9853_init(void)
{
    /* 背光最先开(原厂 ili9481_init 同款): 只要本函数被调用, PA6 必拉高,
     * 作为"驱动选择+EMI open 成功"的可见判据; 面板序列异常也不影响背光 */
    lcd_bl_pinstate(BL_ON);

    JD9853_io_init();
    printf("[lcd] LCD_JD9853 init_start %dx%d rot=%d\n",
           LCD_W, LCD_H, JD9853_INIT_ROTATE);
    JD9853_reg_cfg();
    JD9853_set_direction(JD9853_INIT_ROTATE);
    init_TE(JD9853_Fill);
    printf("[lcd] LCD_JD9853 config succes\n");
    return 0;
}

// *INDENT-OFF*
REGISTER_LCD_DEV(LCD_JD9853) = {
    .name              = "JD9853",
    .lcd_width         = LCD_W,
    .lcd_height        = LCD_H,
    .color_format      = LCD_COLOR_RGB565,
    .column_addr_align = 1,
    .row_addr_align    = 1,
    .LCD_Init          = JD9853_init,
    .SetDrawArea       = JD9853_SetRange,
    .LCD_Draw          = JD9853_draw,
    .LCD_DrawToDev     = JD9853_Fill,
    .LCD_Lvgl_Full     = JD9853_lvgl_Fill,
    .LCD_ClearScreen   = JD9853_clear_screen,
    .Reset             = JD9853_reset,
    .BackLightCtrl     = JD9853_led_ctrl,
};
// *INDENT-ON*

#endif  /* TCFG_LCD_JD9853_ENABLE */
