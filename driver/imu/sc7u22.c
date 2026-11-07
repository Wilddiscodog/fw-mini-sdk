/*
 * SC7U22TH 六轴 IMU — 与 CST816 同挂 PH0/PH1 软件 I2C(iic0)
 * 7bit 地址: SA0=0 → 0x18, SA0=1 → 0x19; WHO_AM_I(0x01)=0x6A
 * 寄存器布局见 sc7u22说明书.pdf（非 ICM-42607 兼容）
 */
#include "system/includes.h"
#include "device/device.h"
#include "device/iic.h"
#include "asm/wdt.h"
#include "app_config.h"
#include "sc7u22.h"

#if (defined TCFG_SC7U22_ENABLE) && TCFG_SC7U22_ENABLE

#define REG_WHO_AM_I        0x01
#define REG_COM_CONF        0x04
#define REG_DATA_STAT       0x0B
#define REG_ACC_XH          0x0C
#define REG_TEMP_H          0x22
#define REG_STEP_CFG        0x3A
#define REG_STEP_SRC        0x3B
#define REG_STEP_CNT_L      0x3C
#define REG_ACC_CONF        0x40
#define REG_ACC_RANGE       0x41
#define REG_GYR_CONF        0x42
#define REG_GYR_RANGE       0x43
#define REG_SOFT_RST        0x4A
#define REG_PWR_CTRL        0x7D

#define WHO_AM_I_VAL        0x6A
#define ADDR7_SA0_L         0x18
#define ADDR7_SA0_H         0x19

/* COM_CONF 默认 0x50: BDU=1, Addr_Auto=1 */
#define COM_CONF_DEFAULT    0x50

/* ACC_CONF: 高性能 + NORM_AVG4 + ODR 100Hz → 0xA8 */
#define ACC_CONF_100HZ      0xA8
/* ACC_RANGE FS=10 → ±8g */
#define ACC_RANGE_8G        0x02
/* GYR_CONF: 高性能 + NORM_AVG4 + ODR 100Hz → 0xA8 */
#define GYR_CONF_100HZ      0xA8
/* GYR_RANGE FS=000 → ±2000dps */
#define GYR_RANGE_2000DPS   0x00

/* PWR_CTRL: TEMP_EN|ACC_EN|GYR_EN */
#define PWR_TEMP_EN         (1u << 3)
#define PWR_ACC_EN          (1u << 2)
#define PWR_GYR_EN          (1u << 1)
#define PWR_ALL_ON          (PWR_TEMP_EN | PWR_ACC_EN | PWR_GYR_EN)

/* STEP_CFG: 默认 0x08=抗马达干扰; STEP_EN=bit0, STEP_CLR=bit2 */
#define STEP_CFG_MOTOR_REJ  0x08
#define STEP_CFG_CLR        0x04
#define STEP_CFG_EN         0x01

/* 灵敏度: ±8g → 0.244 mg/LSB; ±2000dps → 61.035 mdps/LSB */
#define ACC_MG_PER_LSB      0.244f
#define GYR_MDPS_PER_LSB    61.035f

static void *s_iic;
static u8 s_addr_w;
static u8 s_addr_r;
static u8 s_ready;
static u8 s_whoami;
static u8 s_pedo_on;
static u16 s_pedo_base;
static u16 s_pedo_last;
static u32 s_pedo_hi;

static int sc_write_reg(u8 reg, u8 dat)
{
    int err = 0;
    err |= dev_ioctl(s_iic, IIC_IOCTL_START, 0);
    err |= dev_ioctl(s_iic, IIC_IOCTL_TX_WITH_START_BIT, s_addr_w);
    err |= dev_ioctl(s_iic, IIC_IOCTL_TX, reg);
    err |= dev_ioctl(s_iic, IIC_IOCTL_TX_WITH_STOP_BIT, dat);
    dev_ioctl(s_iic, IIC_IOCTL_STOP, 0);
    return err;
}

static int sc_read_regs(u8 reg, u8 *buf, u8 len)
{
    int err = 0;
    err |= dev_ioctl(s_iic, IIC_IOCTL_START, 0);
    err |= dev_ioctl(s_iic, IIC_IOCTL_TX_WITH_START_BIT, s_addr_w);
    err |= dev_ioctl(s_iic, IIC_IOCTL_TX, reg);
    err |= dev_ioctl(s_iic, IIC_IOCTL_TX_WITH_START_BIT, s_addr_r);
    for (u8 i = 0; i < len; i++) {
        if (i < len - 1) {
            err |= dev_ioctl(s_iic, IIC_IOCTL_RX, (u32)&buf[i]);
        } else {
            err |= dev_ioctl(s_iic, IIC_IOCTL_RX_WITH_STOP_BIT, (u32)&buf[i]);
        }
    }
    dev_ioctl(s_iic, IIC_IOCTL_STOP, 0);
    return err;
}

static int probe_addr(u8 addr7)
{
    u8 id = 0;
    int err;

    s_addr_w = (u8)(addr7 << 1);
    s_addr_r = (u8)((addr7 << 1) | 1);
    err = sc_read_regs(REG_WHO_AM_I, &id, 1);
    if (err) {
        return -1;
    }
    if (id != WHO_AM_I_VAL) {
        return -2;
    }
    s_whoami = id;
    return 0;
}

int sc7u22_init(void)
{
    u8 stat = 0;

    if (s_ready) {
        return 0;
    }
    s_iic = dev_open("iic0", NULL);
    if (!s_iic) {
        return -1;
    }

    if (probe_addr(ADDR7_SA0_L) && probe_addr(ADDR7_SA0_H)) {
        return -2;
    }

    /* soft reset: Addr_Auto 开启后向 SOFT_RST 写两次 0xA5 */
    sc_write_reg(REG_COM_CONF, COM_CONF_DEFAULT);
    sc_write_reg(REG_SOFT_RST, 0xA5);
    sc_write_reg(REG_SOFT_RST, 0xA5);
    os_time_dly(10);    /* ~100ms, datasheet Ton≈60ms @100Hz */

    /* 重新选地址(复位后通信仍在同地址) */
    if (probe_addr(s_addr_w >> 1)) {
        if (probe_addr(ADDR7_SA0_L) && probe_addr(ADDR7_SA0_H)) {
            return -3;
        }
    }

    sc_write_reg(REG_COM_CONF, COM_CONF_DEFAULT);
    sc_write_reg(REG_ACC_CONF, ACC_CONF_100HZ);
    sc_write_reg(REG_ACC_RANGE, ACC_RANGE_8G);
    sc_write_reg(REG_GYR_CONF, GYR_CONF_100HZ);
    sc_write_reg(REG_GYR_RANGE, GYR_RANGE_2000DPS);
    os_time_dly(1);

    sc_write_reg(REG_PWR_CTRL, PWR_ALL_ON);
    os_time_dly(5);     /* 配置后建议延时再读 */

    sc_read_regs(REG_DATA_STAT, &stat, 1);
    (void)stat;
    s_ready = 1;
    printf("[imu] SC7U22 ready addr=0x%02X who=0x%02X\r\n",
           (u8)(s_addr_w >> 1), s_whoami);
    return 0;
}

u8 sc7u22_ready(void)
{
    return s_ready;
}

u8 sc7u22_whoami(void)
{
    return s_whoami;
}

static s16 be16(u8 hi, u8 lo)
{
    return (s16)(((u16)hi << 8) | lo);
}

int sc7u22_read_raw(struct sc7u22_raw *out)
{
    u8 b[12];
    u8 t[2];

    if (!s_ready || !out) {
        return -1;
    }
    /* ACC_XH..GYR_ZL 连续 12 字节(Addr_Auto) */
    if (sc_read_regs(REG_ACC_XH, b, 12)) {
        return -2;
    }
    if (sc_read_regs(REG_TEMP_H, t, 2)) {
        return -3;
    }
    out->ax = be16(b[0], b[1]);
    out->ay = be16(b[2], b[3]);
    out->az = be16(b[4], b[5]);
    out->gx = be16(b[6], b[7]);
    out->gy = be16(b[8], b[9]);
    out->gz = be16(b[10], b[11]);
    out->temp = be16(t[0], t[1]);
    return 0;
}

int sc7u22_read_si(struct sc7u22_si *out)
{
    struct sc7u22_raw r;

    if (sc7u22_read_raw(&r)) {
        return -1;
    }
    out->ax = (float)r.ax * ACC_MG_PER_LSB / 1000.0f;
    out->ay = (float)r.ay * ACC_MG_PER_LSB / 1000.0f;
    out->az = (float)r.az * ACC_MG_PER_LSB / 1000.0f;
    out->gx = (float)r.gx * GYR_MDPS_PER_LSB / 1000.0f;
    out->gy = (float)r.gy * GYR_MDPS_PER_LSB / 1000.0f;
    out->gz = (float)r.gz * GYR_MDPS_PER_LSB / 1000.0f;
    /* T = TEMP/512 + 23℃ */
    out->temp_c = ((float)r.temp / 512.0f) + 23.0f;
    return 0;
}

static int read_step_regs(u16 *step, u8 *m_state)
{
    u8 b[3];

    /* STEP_SRC(3B) + STEP_COUNTER_L/H(3C/3D); 先读 3C 连读 2 字节更稳 */
    if (sc_read_regs(REG_STEP_CNT_L, b, 2)) {
        return -1;
    }
    if (step) {
        *step = (u16)(((u16)b[1] << 8) | b[0]);
    }
    if (m_state) {
        u8 src = 0;
        if (sc_read_regs(REG_STEP_SRC, &src, 1)) {
            return -2;
        }
        /* M_STATE 在 bit6:5 */
        *m_state = (u8)((src >> 5) & 0x03);
    }
    return 0;
}

/* SC7U22: 00静坐 01慢走 10快走 11跑步 → 统一 0未知 1走 2跑 */
static u8 map_activity(u8 m_state)
{
    switch (m_state) {
    case 1: /* 慢走 */
    case 2: /* 快走 */
        return 1;
    case 3: /* 跑步 */
        return 2;
    default:
        return 0;
    }
}

int sc7u22_pedo_enable(void)
{
    u16 step = 0;

    if (!s_ready) {
        return -1;
    }
    if (s_pedo_on) {
        return 0;
    }

    /* 保持 accel 供电; 开计步并清零计数器 (PEDO_COEFF 用出厂默认) */
    if (sc_write_reg(REG_STEP_CFG,
                     (u8)(STEP_CFG_MOTOR_REJ | STEP_CFG_CLR | STEP_CFG_EN))) {
        return -2;
    }
    os_time_dly(2);

    /* 再写一次仅使能, 确保 CLR 自复位后仍保持 EN */
    if (sc_write_reg(REG_STEP_CFG, (u8)(STEP_CFG_MOTOR_REJ | STEP_CFG_EN))) {
        return -3;
    }
    os_time_dly(1);

    read_step_regs(&step, NULL);
    s_pedo_base = step;
    s_pedo_last = step;
    s_pedo_hi = 0;
    s_pedo_on = 1;
    printf("[imu] SC7U22 pedo ON base=%u\r\n", step);
    return 0;
}

int sc7u22_pedo_disable(void)
{
    if (!s_ready) {
        return -1;
    }
    if (!s_pedo_on) {
        return 0;
    }
    /* 关 EN, 保留抗干扰位 */
    sc_write_reg(REG_STEP_CFG, STEP_CFG_MOTOR_REJ);
    s_pedo_on = 0;
    printf("[imu] SC7U22 pedo OFF\r\n");
    return 0;
}

u8 sc7u22_pedo_enabled(void)
{
    return s_pedo_on;
}

int sc7u22_pedo_read(struct imu_pedo *out)
{
    u16 step = 0;
    u8 m_state = 0;
    u32 total;

    if (!s_ready || !out || !s_pedo_on) {
        return -1;
    }
    if (read_step_regs(&step, &m_state)) {
        return -2;
    }
    if (step < s_pedo_last) {
        s_pedo_hi += 65536u;
    }
    s_pedo_last = step;
    total = s_pedo_hi + (u32)step;
    if (total >= (u32)s_pedo_base) {
        out->step_count = total - (u32)s_pedo_base;
    } else {
        out->step_count = 0;
    }
    out->step_raw = step;
    out->cadence = 0;
    out->activity = map_activity(m_state);
    return 0;
}

#else

int sc7u22_init(void)
{
    return -1;
}
u8 sc7u22_ready(void)
{
    return 0;
}
u8 sc7u22_whoami(void)
{
    return 0;
}
int sc7u22_read_raw(struct sc7u22_raw *out)
{
    (void)out;
    return -1;
}
int sc7u22_read_si(struct sc7u22_si *out)
{
    (void)out;
    return -1;
}
int sc7u22_pedo_enable(void)
{
    return -1;
}
int sc7u22_pedo_disable(void)
{
    return -1;
}
u8 sc7u22_pedo_enabled(void)
{
    return 0;
}
int sc7u22_pedo_read(struct imu_pedo *out)
{
    (void)out;
    return -1;
}

#endif
