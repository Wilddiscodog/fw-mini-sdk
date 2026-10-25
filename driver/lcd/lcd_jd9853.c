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
 *   PC1~PC8=D0~D7, PC10=WR, PC9=CS(以上 EMI 硬件复用);
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

#define LCD_W   240    /* 竖屏宽; 横屏对调并改 MADCTL */
#define LCD_H   320

#define ROTATE_DEGREE_0    0
#define ROTATE_DEGREE_90   1
#define ROTATE_DEGREE_180  2
#define ROTATE_DEGREE_270  3

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
    lcd_cs_pinstate(1);   /* cs_pin=-1 时为空操作(CS 由 EMI 硬件选通) */
}

/* ================= 面板调优段(公开参考, 异常时替换) =================
 * JD9853 360x360: 电源/Gamma/驱动能力。该段与批次相关, 保留原样即可点亮,
 * 色偏/发暗再回头调。 */
static const u8 jd9853_tuning[][8] = {
    /* {cmd, n, d0..d5, delay_ms} */
    {0xC0, 3, 0x10, 0x0B, 0x07, 0, 0, 0},     /* 电源: GVDD/VRH */
    {0xC1, 2, 0x40 + 0x04, 0x08, 0, 0, 0, 0}, /* VCOM */
    {0xC2, 2, 0x07, 0x12, 0, 0, 0, 0},        /* VCOM offset */
    {0xC5, 1, 0x28, 0, 0, 0, 0, 0},           /* FRM 60Hz */
    {0xE4, 1, 0x25, 0, 0, 0, 0, 0},           /* MIPI/DPI 时序 */
    {0xB0, 1, 0x19, 0, 0, 0, 0, 0},           /* 振铃抑制 */
};

static void JD9853_reg_cfg(void)
{
    /* 复位时序(对齐 zc 日志: LCD init_start 在触摸 id 读取后) */
    lcd_rst_pinstate(1);
    lcd_delay(10);
    lcd_rst_pinstate(0);
    lcd_delay(20);
    lcd_rst_pinstate(1);
    lcd_delay(120);

    /* DCS 标准流 */
    WriteCOM(0x11);            /* Sleep Out */
    lcd_delay(120);

    WriteCOM(0x3A);            /* COLMOD: RGB565 */
    WriteDAT_8(0x55);

    /* 调优段 */
    for (u32 i = 0; i < sizeof(jd9853_tuning) / sizeof(jd9853_tuning[0]); i++) {
        const u8 *t = jd9853_tuning[i];
        WriteCOM(t[0]);
        for (u8 j = 0; j < t[1]; j++) {
            WriteDAT_8(t[2 + j]);
        }
        if (t[7]) {
            lcd_delay(t[7]);
        }
    }

    WriteCOM(0x21);            /* 反色关(如屏色反了删/加此行) */
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
    WriteCOM(0x36);            /* 240x320 竖屏 */
    if (dir == ROTATE_DEGREE_0) {
        WriteDAT_8(0x00);
    } else if (dir == ROTATE_DEGREE_90) {
        WriteDAT_8(0x60);
    } else if (dir == ROTATE_DEGREE_180) {
        WriteDAT_8(0xC0);
    } else {
        WriteDAT_8(0xA0);
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
    printf("[lcd] LCD_JD9853 init_start\n");
    JD9853_reg_cfg();
    JD9853_set_direction(ROTATE_DEGREE_0);
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
