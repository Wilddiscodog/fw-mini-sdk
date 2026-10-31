/*
 * 触摸驱动 —— CST816 系列 (xiaolu-R2 手表, IIC 电容触摸)
 *
 * 硬件(用户实测): PH0=SCL, PH1=SDA(板级 iic0, 与 mini-sdk sw_iic0 同引脚),
 *                 PH3=TP_INT, PH4=TP_RST。
 * 器件识别(原机日志): "touch_get_type: id=b6" → "touch id CST716/CST816/CST820",
 *   "touch_update: m_id=0xa" —— 即 CST816 常见标识(chip id 0xB6)。
 * 框架: 参照原厂 ft6236.c(同类 IIC+INT+RST)。
 *
 * CST816 要点:
 *  - 7bit 地址 0x15(写 0x2A/读 0x2B); 少数模组 0x14
 *  - 初始化免写寄存器: RST 时序(低 10ms → 高 60ms)后即可读
 *  - 寄存器: 0xA7=fw 版本(常见 0xB6 系), 触点数据 0x02 起 6 字节
 *    [0]=按下标志, [1]=xh(4bit), [3]=xl, [5]=yh, [7]=yl
 */
#include "system/includes.h"
#include "typedef.h"
#include "asm/gpio.h"
#include "device/device.h"
#include "device/iic.h"
#include "os/os_api.h"
#include "app_config.h"

#if TCFG_TOUCH_CST816_ENABLE
#include "lcd_config.h"          /* LCD_W/H: 横屏时触摸坐标需映射 */

#define CST816_ADDR_W    0x2A    /* 7bit 0x15 << 1 */
#define CST816_ADDR_R    0x2B

#define CST816_REG_DATA  0x02    /* 触点数据起始 */
#define CST816_REG_ID    0xA7    /* fw 版本 */

#define TP_INT_PIN       IO_PORTH_03
#define TP_RST_PIN       IO_PORTH_04

static void *iic = NULL;
static u8 touch_status = 0;
static u16 touch_x, touch_y;

/* LVGL 输入桥(lv_port_indev.c 调 get_touch_x_y 一次并缓存指针):
 * 结构体是常驻共享存储, 由 cst816_task 每次轮询直接写入 —— 旧实现
 * "调用时才拷贝"导致指针缓存后永不更新, LVGL 永远读到 (0,0,无按下)。
 * 字段布局须与 lv_port_indev.c 的 struct touch_hdl 一致(u16,u16,u8) */
static struct {
    u16 x;
    u16 y;
    u8 status;
} tp_hdl;

void *get_touch_x_y(void)
{
    return &tp_hdl;
}

/* 触摸事件回调(由 LCD/UI 层注册; 无注册时仅打印) */
static void (*cst816_event_cb)(u8 status, u16 x, u16 y) = NULL;
void cst816_register_event(void (*cb)(u8 status, u16 x, u16 y))
{
    cst816_event_cb = cb;
}

static int cst816_write_reg(u8 reg, u8 dat)
{
    int err = 0;
    err |= dev_ioctl(iic, IIC_IOCTL_START, 0);
    err |= dev_ioctl(iic, IIC_IOCTL_TX_WITH_START_BIT, CST816_ADDR_W);
    err |= dev_ioctl(iic, IIC_IOCTL_TX, reg);
    err |= dev_ioctl(iic, IIC_IOCTL_TX_WITH_STOP_BIT, dat);
    dev_ioctl(iic, IIC_IOCTL_STOP, 0);
    return err;
}

static int cst816_read_regs(u8 reg, u8 *buf, u8 len)
{
    int err = 0;
    /* IIC_IOCTL_START 只拿一次(mutex + gpio_port_lock); 中间 repeat-start
     * 只用 TX_WITH_START_BIT。再调一次 START 会自旋锁自锁 → wdt_timout_err */
    err |= dev_ioctl(iic, IIC_IOCTL_START, 0);
    err |= dev_ioctl(iic, IIC_IOCTL_TX_WITH_START_BIT, CST816_ADDR_W);
    err |= dev_ioctl(iic, IIC_IOCTL_TX, reg);
    err |= dev_ioctl(iic, IIC_IOCTL_TX_WITH_START_BIT, CST816_ADDR_R);
    for (u8 i = 0; i < len; i++) {
        if (i < len - 1) {
            err |= dev_ioctl(iic, IIC_IOCTL_RX, (u32)&buf[i]);               /* ACK 续读 */
        } else {
            err |= dev_ioctl(iic, IIC_IOCTL_RX_WITH_STOP_BIT, (u32)&buf[i]); /* NACK+STOP */
        }
    }
    dev_ioctl(iic, IIC_IOCTL_STOP, 0);  /* 释放 mutex / gpio_port_lock */
    return err;
}

/* RST 时序(对齐原机日志 touch_reset: 复位 → 读 id → 起任务) */
static void cst816_reset(void)
{
    gpio_set_direction(TP_RST_PIN, 0);
    gpio_set_direction(TP_INT_PIN, 1);   /* INT 输入 */
    gpio_direction_output(TP_RST_PIN, 0);
    os_time_dly(1);                      /* ~10ms */
    gpio_direction_output(TP_RST_PIN, 1);
    os_time_dly(6);                      /* ~60ms */
}

static int cst816_init(void)
{
    u8 id = 0;

    iic = dev_open("iic0", NULL);        /* PH0/PH1, board.c sw_iic0 */
    if (!iic) {
        return -1;
    }

    cst816_reset();
    (void)cst816_read_regs(CST816_REG_ID, &id, 1);
    (void)id;   /* 0xB6 等变体同数据格式, 全放行 */
    return 0;
}

/* 芯片 = CST816D(chip id 0xB6, koendv/cst816t 变体表)。寄存器(读起始 0x02):
 *   [0]=reg0x02 FingerNum(触下=1), [1]=reg0x03 XH, [2]=reg0x04 XL,
 *   [3]=reg0x05 YH, [4]=reg0x06 YL, [5]=reg0x07 Pressure
 * 关键实测: XH 出现过 0x01(X=258>239) 而 Y 全部 ≤204 —— 触摸 IC 原生坐标系
 * 为 320(X)×240(Y) 横向, 与竖屏显示(240x320)成 90° 关系(非镜像!)。
 * 映射: x_d 由 ty_raw(0..239) 来, y_d 由 tx_raw(0..319) 来;
 * 贴装方向 4 选 1 现场校准(摸四角对不上就换 TOUCH_CAL_MODE)。 */
#define TOUCH_CAL_NORMAL    0   /* x_d=ty,     y_d=tx     */
#define TOUCH_CAL_MIRROR_X  1   /* x_d=239-ty, y_d=tx     */
#define TOUCH_CAL_MIRROR_Y  2   /* x_d=ty,     y_d=319-tx */
#define TOUCH_CAL_ROT180    3   /* x_d=239-ty, y_d=319-tx */
#define TOUCH_CAL_MODE  TOUCH_CAL_NORMAL

static u8 cst816_read_point(u16 *x, u16 *y)
{
    u8 d[6] = {0};
    u16 tx, ty;
    u8 press;

    if (cst816_read_regs(CST816_REG_DATA, d, 6)) {
        return 0;
    }

    press = d[0] & 0x0f;                 /* FingerNum: 0=无触, 1=单指 */
    if (press) {
        tx = ((u16)(d[1] & 0x0f) << 8) | d[2];
        ty = ((u16)(d[3] & 0x0f) << 8) | d[4];
        if (tx > LCD_W - 1) {
            tx = LCD_W - 1;
        }
        if (ty > LCD_H - 1) {
            ty = LCD_H - 1;
        }
#if HORIZONTAL_SCREEN
        /* 横屏 296x240: 触控与显示坐标一致 */
#else
        /* 竖屏 240x296: 轴交换 + 贴装校准 */
        u16 ax = ty;
        u16 ay = tx;
#if TOUCH_CAL_MODE == TOUCH_CAL_NORMAL
        tx = ax;
        ty = ay;
#elif TOUCH_CAL_MODE == TOUCH_CAL_MIRROR_X
        tx = LCD_H - 1 - ax;
        ty = ay;
#elif TOUCH_CAL_MODE == TOUCH_CAL_MIRROR_Y
        tx = ax;
        ty = LCD_W - 1 - ay;
#else   /* TOUCH_CAL_ROT180 */
        tx = LCD_H - 1 - ax;
        ty = LCD_W - 1 - ay;
#endif
#endif
        *x = tx;
        *y = ty;
    }
    return press;
}

static void cst816_task(void *priv)
{
    u16 x, y;

    if (cst816_init()) {
        while (1) { os_time_dly(100); }
    }

    while (1) {
        os_time_dly(2);                  /* 20ms 轮询(INT 可改中断驱动) */
        if (cst816_read_point(&x, &y)) {
            if (!touch_status) {
                touch_status = 1;
                touch_x = x;
                touch_y = y;
                tp_hdl.x = x;            /* LVGL 桥: 常驻结构体, 每拍直写 */
                tp_hdl.y = y;
                tp_hdl.status = 1;
                if (cst816_event_cb) {
                    cst816_event_cb(1, x, y);
                }
            } else {
                touch_x = x;             /* 按住持续更新 */
                touch_y = y;
                tp_hdl.x = x;
                tp_hdl.y = y;
                if (cst816_event_cb) {
                    cst816_event_cb(2, x, y);
                }
            }
        } else if (touch_status) {
            touch_status = 0;
            tp_hdl.status = 0;
            if (cst816_event_cb) {
                cst816_event_cb(0, touch_x, touch_y);
            }
        }
    }
}

static int cst816_start(void)
{
    return os_task_create(cst816_task, NULL, 12, 1024, 0, "cst816");
}
late_initcall(cst816_start);

#endif  /* TCFG_TOUCH_CST816_ENABLE */
