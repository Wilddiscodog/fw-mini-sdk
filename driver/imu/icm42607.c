/*
 * ICM-42607-C 六轴 IMU — 与 CST816 同挂 PH0/PH1 软件 I2C(iic0)
 * 7bit 地址: AP_AD0=0 → 0x68, AP_AD0=1 → 0x69
 * WHO_AM_I(0x75)=0x61
 */
#include "system/includes.h"
#include "device/device.h"
#include "device/iic.h"
#include "app_config.h"
#include "icm42607.h"

#if (defined TCFG_SC7U22_ENABLE) && TCFG_SC7U22_ENABLE

#define REG_MCLK_RDY            0x00
#define REG_DEVICE_CONFIG       0x01
#define REG_SIGNAL_PATH_RESET   0x02
#define REG_TEMP_DATA1          0x09
#define REG_PWR_MGMT0           0x1F
#define REG_GYRO_CONFIG0        0x20
#define REG_ACCEL_CONFIG0       0x21
#define REG_APEX_CONFIG0        0x25
#define REG_APEX_CONFIG1        0x26
#define REG_APEX_DATA0          0x31
#define REG_WHO_AM_I            0x75
#define REG_BLK_SEL_W           0x79
#define REG_MADDR_W             0x7A
#define REG_M_W                 0x7B
#define REG_BLK_SEL_R           0x7C
#define REG_MADDR_R             0x7D
#define REG_M_R                 0x7E

#define WHO_AM_I_VAL            0x61
#define ADDR7_AD0_L             0x68
#define ADDR7_AD0_H             0x69

/* soft reset: SIGNAL_PATH_RESET[4]=1 */
#define SOFT_RESET_BIT          0x10

/* GYRO_CONFIG0: FS=±2000dps(00) + ODR=100Hz(1001) → 0x09 */
#define GYRO_CFG_2000_100HZ     0x09
/* ACCEL_CONFIG0: FS=±8g(01) + ODR=100Hz(1001) → 0x29 */
#define ACCEL_CFG_8G_100HZ      0x29
/* ACCEL_CONFIG0: FS=±8g(01) + ODR=50Hz(1010) → 0x2A */
#define ACCEL_CFG_8G_50HZ       0x2A
/* PWR_MGMT0: GYRO LN(11) + ACCEL LN(11) → 0x0F; bit4=IDLE 保 MCLK */
#define PWR_LN_BOTH             0x0F
#define PWR_IDLE_BIT            0x10

/* APEX_CONFIG0 */
#define APEX_DMP_POWER_SAVE_EN  0x08
#define APEX_DMP_INIT_EN        0x04
#define APEX_DMP_MEM_RESET_EN   0x01
/* APEX_CONFIG1: PED_ENABLE bit3; DMP_ODR[1:0] 10=50Hz */
#define APEX_PED_ENABLE         0x08
#define APEX_DMP_ODR_50HZ       0x02

/* ±8g → 4096 LSB/g; ±2000dps → 16.4 LSB/dps */
#define ACC_LSB_PER_G           4096.0f
#define GYR_LSB_PER_DPS         16.4f

static void *s_iic;
static u8 s_addr_w;
static u8 s_addr_r;
static u8 s_ready;
static u8 s_whoami;
static u8 s_pedo_on;
static u8 s_dmp_on;
static u16 s_pedo_base;     /* 开启时 STEP_CNT, UI 显示相对步数 */
static u16 s_pedo_last;     /* 上次读到的原始值, 用于溢出续计 */
static u32 s_pedo_hi;       /* 溢出累加的高位 */

static int ic_write_reg(u8 reg, u8 dat)
{
    int err = 0;
    err |= dev_ioctl(s_iic, IIC_IOCTL_START, 0);
    err |= dev_ioctl(s_iic, IIC_IOCTL_TX_WITH_START_BIT, s_addr_w);
    err |= dev_ioctl(s_iic, IIC_IOCTL_TX, reg);
    err |= dev_ioctl(s_iic, IIC_IOCTL_TX_WITH_STOP_BIT, dat);
    dev_ioctl(s_iic, IIC_IOCTL_STOP, 0);
    return err;
}

static int ic_read_regs(u8 reg, u8 *buf, u8 len)
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
    err = ic_read_regs(REG_WHO_AM_I, &id, 1);
    if (err) {
        return -1;
    }
    if (id != WHO_AM_I_VAL) {
        return -2;
    }
    s_whoami = id;
    return 0;
}

int icm42607_init(void)
{
    if (s_ready) {
        return 0;
    }
    s_iic = dev_open("iic0", NULL);
    if (!s_iic) {
        return -1;
    }

    if (probe_addr(ADDR7_AD0_L) && probe_addr(ADDR7_AD0_H)) {
        return -2;
    }

    /* soft reset */
    ic_write_reg(REG_SIGNAL_PATH_RESET, SOFT_RESET_BIT);
    os_time_dly(2); /* ~20ms */

    if (probe_addr(s_addr_w >> 1)) {
        if (probe_addr(ADDR7_AD0_L) && probe_addr(ADDR7_AD0_H)) {
            return -3;
        }
    }

    /* 先配量程/ODR, 再开 LN 模式 */
    ic_write_reg(REG_GYRO_CONFIG0, GYRO_CFG_2000_100HZ);
    ic_write_reg(REG_ACCEL_CONFIG0, ACCEL_CFG_8G_100HZ);
    ic_write_reg(REG_PWR_MGMT0, PWR_LN_BOTH);
    /* 从 OFF→LN 后至少 200us 不写寄存器; 陀螺稳定约 45ms */
    os_time_dly(5);

    s_ready = 1;
    printf("[imu] ICM-42607-C ready addr=0x%02X who=0x%02X\r\n",
           (u8)(s_addr_w >> 1), s_whoami);
    return 0;
}

u8 icm42607_ready(void)
{
    return s_ready;
}

u8 icm42607_whoami(void)
{
    return s_whoami;
}

static s16 be16(u8 hi, u8 lo)
{
    return (s16)(((u16)hi << 8) | lo);
}

int icm42607_read_raw(struct sc7u22_raw *out)
{
    u8 b[14];

    if (!s_ready || !out) {
        return -1;
    }
    /* TEMP(2) + ACC(6) + GYR(6) 自 0x09 连续读 */
    if (ic_read_regs(REG_TEMP_DATA1, b, 14)) {
        return -2;
    }
    out->temp = be16(b[0], b[1]);
    out->ax = be16(b[2], b[3]);
    out->ay = be16(b[4], b[5]);
    out->az = be16(b[6], b[7]);
    out->gx = be16(b[8], b[9]);
    out->gy = be16(b[10], b[11]);
    out->gz = be16(b[12], b[13]);
    return 0;
}

int icm42607_read_si(struct sc7u22_si *out)
{
    struct sc7u22_raw r;

    if (icm42607_read_raw(&r)) {
        return -1;
    }
    out->ax = (float)r.ax / ACC_LSB_PER_G;
    out->ay = (float)r.ay / ACC_LSB_PER_G;
    out->az = (float)r.az / ACC_LSB_PER_G;
    out->gx = (float)r.gx / GYR_LSB_PER_DPS;
    out->gy = (float)r.gy / GYR_LSB_PER_DPS;
    out->gz = (float)r.gz / GYR_LSB_PER_DPS;
    /* T = TEMP/128 + 25℃ */
    out->temp_c = ((float)r.temp / 128.0f) + 25.0f;
    return 0;
}

static void ic_delay_us(u32 us)
{
    /* 粗略忙等, 供 MREG/DMP 时序; 无需精确 */
    volatile u32 n = us * 40u + 1u;
    while (n--) {
        ;
    }
}

static int wait_mclk_rdy(void)
{
    u8 v = 0;
    int i;

    for (i = 0; i < 50; i++) {
        if (ic_read_regs(REG_MCLK_RDY, &v, 1) == 0 && (v & 0x01)) {
            return 0;
        }
        ic_delay_us(100);
    }
    return -1;
}

static int dmp_mem_reset(void)
{
    u8 v = 0;
    int i;

    if (ic_read_regs(REG_APEX_CONFIG0, &v, 1)) {
        return -1;
    }
    v = (u8)((v & (u8)~APEX_DMP_POWER_SAVE_EN) | APEX_DMP_MEM_RESET_EN);
    if (ic_write_reg(REG_APEX_CONFIG0, v)) {
        return -2;
    }
    ic_delay_us(1000);
    for (i = 0; i < 500; i++) {
        if (ic_read_regs(REG_APEX_CONFIG0, &v, 1)) {
            return -3;
        }
        if ((v & APEX_DMP_MEM_RESET_EN) == 0) {
            return 0;
        }
        ic_delay_us(100);
    }
    return -4;
}

static int dmp_init_en(void)
{
    u8 v = 0;
    int i;

    if (ic_read_regs(REG_APEX_CONFIG0, &v, 1)) {
        return -1;
    }
    v = (u8)((v & (u8)~APEX_DMP_POWER_SAVE_EN) | APEX_DMP_INIT_EN);
    if (ic_write_reg(REG_APEX_CONFIG0, v)) {
        return -2;
    }
    for (i = 0; i < 500; i++) {
        if (ic_read_regs(REG_APEX_CONFIG0, &v, 1)) {
            return -3;
        }
        if ((v & APEX_DMP_INIT_EN) == 0) {
            return 0;
        }
        ic_delay_us(100);
    }
    return -4;
}

static int read_step_raw(u16 *step, u8 *cadence, u8 *activity)
{
    u8 b[4];

    if (ic_read_regs(REG_APEX_DATA0, b, 4)) {
        return -1;
    }
    if (step) {
        *step = (u16)(((u16)b[1] << 8) | b[0]);
    }
    if (cadence) {
        *cadence = b[2];
    }
    if (activity) {
        *activity = (u8)(b[3] & 0x03);
    }
    return 0;
}

int icm42607_pedo_enable(void)
{
    u8 v = 0;
    u16 step = 0;

    if (!s_ready) {
        return -1;
    }
    if (s_pedo_on) {
        return 0;
    }

    /* 保 RC/MCLK: IDLE=1 + 双 LN (计步只需 accel, 保持现状兼容其它读数) */
    ic_write_reg(REG_PWR_MGMT0, (u8)(PWR_LN_BOTH | PWR_IDLE_BIT));
    ic_delay_us(200);
    if (wait_mclk_rdy()) {
        printf("[imu] pedo: MCLK not ready\r\n");
        return -2;
    }

    /* Accel/DMP 50Hz (官方 APEX pedometer 要求) */
    ic_write_reg(REG_ACCEL_CONFIG0, ACCEL_CFG_8G_50HZ);

    if (ic_read_regs(REG_APEX_CONFIG1, &v, 1)) {
        return -3;
    }
    v = (u8)((v & (u8)~0x03) | APEX_DMP_ODR_50HZ);
    v = (u8)(v & (u8)~APEX_PED_ENABLE); /* 配置前先关 */
    ic_write_reg(REG_APEX_CONFIG1, v);

    /* 关 DMP power-save */
    if (ic_read_regs(REG_APEX_CONFIG0, &v, 1)) {
        return -4;
    }
    v = (u8)(v & (u8)~APEX_DMP_POWER_SAVE_EN);
    ic_write_reg(REG_APEX_CONFIG0, v);

    if (!s_dmp_on) {
        if (dmp_mem_reset()) {
            printf("[imu] pedo: DMP mem reset fail\r\n");
            return -5;
        }
        s_dmp_on = 1;
    }
    if (dmp_init_en()) {
        printf("[imu] pedo: DMP init fail\r\n");
        return -6;
    }

    if (ic_read_regs(REG_APEX_CONFIG1, &v, 1)) {
        return -7;
    }
    v = (u8)((v & (u8)~0x03) | APEX_DMP_ODR_50HZ | APEX_PED_ENABLE);
    if (ic_write_reg(REG_APEX_CONFIG1, v)) {
        return -8;
    }

    os_time_dly(5);
    read_step_raw(&step, NULL, NULL);
    s_pedo_base = step;
    s_pedo_last = step;
    s_pedo_hi = 0;
    s_pedo_on = 1;
    printf("[imu] pedo ON base=%u\r\n", step);
    return 0;
}

int icm42607_pedo_disable(void)
{
    u8 v = 0;

    if (!s_ready) {
        return -1;
    }
    if (!s_pedo_on) {
        return 0;
    }

    if (ic_read_regs(REG_APEX_CONFIG1, &v, 1) == 0) {
        v = (u8)(v & (u8)~APEX_PED_ENABLE);
        ic_write_reg(REG_APEX_CONFIG1, v);
    }

    /* 恢复默认 Accel 100Hz */
    ic_write_reg(REG_ACCEL_CONFIG0, ACCEL_CFG_8G_100HZ);
    ic_write_reg(REG_PWR_MGMT0, PWR_LN_BOTH);

    s_pedo_on = 0;
    printf("[imu] pedo OFF\r\n");
    return 0;
}

u8 icm42607_pedo_enabled(void)
{
    return s_pedo_on;
}

int icm42607_pedo_read(struct imu_pedo *out)
{
    u16 step = 0;
    u8 cadence = 0;
    u8 activity = 0;
    u32 total;

    if (!s_ready || !out || !s_pedo_on) {
        return -1;
    }
    if (read_step_raw(&step, &cadence, &activity)) {
        return -2;
    }
    if (step < s_pedo_last) {
        /* 16bit 溢出 */
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
    out->cadence = cadence;
    out->activity = activity;
    return 0;
}

#else

int icm42607_init(void)
{
    return -1;
}
u8 icm42607_ready(void)
{
    return 0;
}
u8 icm42607_whoami(void)
{
    return 0;
}
int icm42607_read_raw(struct sc7u22_raw *out)
{
    (void)out;
    return -1;
}
int icm42607_read_si(struct sc7u22_si *out)
{
    (void)out;
    return -1;
}
int icm42607_pedo_enable(void)
{
    return -1;
}
int icm42607_pedo_disable(void)
{
    return -1;
}
u8 icm42607_pedo_enabled(void)
{
    return 0;
}
int icm42607_pedo_read(struct imu_pedo *out)
{
    (void)out;
    return -1;
}

#endif
