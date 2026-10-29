/*
 * ICM-42607 六轴 IMU — 与 CST816 同挂 PH0/PH1 软件 I2C(iic0)
 * I2C 7bit: AD0=0 → 0x68, AD0=1 → 0x69; WHO_AM_I(0x75)=0x61
 */
#include "system/includes.h"
#include "device/device.h"
#include "device/iic.h"
#include "asm/wdt.h"
#include "app_config.h"
#include "icm42607.h"

#if (defined TCFG_ICM42607_ENABLE) && TCFG_ICM42607_ENABLE

#define REG_MCLK_RDY            0x00
#define REG_SIGNAL_PATH_RESET   0x02
#define REG_TEMP_DATA1          0x09
#define REG_ACCEL_DATA_X1       0x0B
#define REG_GYRO_DATA_X1        0x11
#define REG_PWR_MGMT0           0x1F
#define REG_GYRO_CONFIG0        0x20
#define REG_ACCEL_CONFIG0       0x21
#define REG_WHO_AM_I            0x75

#define WHO_AM_I_VAL            0x61
#define ADDR7_AD0_L             0x68
#define ADDR7_AD0_H             0x69
/* SC7U22TR(国产六轴, 寄存器兼容 ICM-42607-C)地址段: 实测 ACK @0x18(AD0 接地) */
#define ADDR7_SC7U_L            0x18
#define ADDR7_SC7U_H            0x19

/* ±16g → 2048 LSB/g; ±2000dps → 16.4 LSB/dps */
#define ACCEL_SENS_16G          2048.0f
#define GYRO_SENS_2000DPS       16.4f

static void *s_iic;
static u8 s_addr_w;
static u8 s_addr_r;
static u8 s_ready;
static u8 s_whoami;

static int icm_write_reg(u8 reg, u8 dat)
{
    int err = 0;
    err |= dev_ioctl(s_iic, IIC_IOCTL_START, 0);
    err |= dev_ioctl(s_iic, IIC_IOCTL_TX_WITH_START_BIT, s_addr_w);
    err |= dev_ioctl(s_iic, IIC_IOCTL_TX, reg);
    err |= dev_ioctl(s_iic, IIC_IOCTL_TX_WITH_STOP_BIT, dat);
    dev_ioctl(s_iic, IIC_IOCTL_STOP, 0);
    return err;
}

static int icm_read_regs(u8 reg, u8 *buf, u8 len)
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
    err = icm_read_regs(REG_WHO_AM_I, &id, 1);
    if (err) {
        printf("[icm42607] probe 0x%02x: NACK/IO err=%d\n", addr7, err);
        return -1;
    }
    if (id != WHO_AM_I_VAL) {
        /* SC7U22TR 等兼容料 ID 值可能与英飞凌原厂不同: ACK 即接受, 打印供核对 */
        printf("[icm42607] probe 0x%02x: WHO_AM_I=0x%02x (原厂 0x61, 兼容料可异)\n",
               addr7, id);
        if (id == 0x00 || id == 0xFF) {
            return -2;                       /* 全 0/全 1 视为无效 */
        }
    }
    s_whoami = id;
    return 0;
}

/* 总线扫描: 探测 0x03~0x77 全部 7bit 地址的 ACK(写地址+STOP 最小事务)。
 * 诊断用: 确认陀螺仪实际地址, 同时验证总线本身(0x15=CST816 应应答) */
static void icm_scan_bus(void)
{
    u8 found = 0;
    printf("[icm42607] i2c0 scan 0x03-0x77:\n");
    for (u8 a = 0x03; a <= 0x77; a++) {
        int err;
        s_addr_w = (u8)(a << 1);
        s_addr_r = (u8)((a << 1) | 1);
        err = dev_ioctl(s_iic, IIC_IOCTL_START, 0);
        err |= dev_ioctl(s_iic, IIC_IOCTL_TX_WITH_START_BIT, s_addr_w);
        dev_ioctl(s_iic, IIC_IOCTL_STOP, 0);
        if (err == 0) {
            printf("[icm42607]   ACK @0x%02x\n", a);
            found++;
        }
        wdt_clear();
    }
    if (!found) {
        printf("[icm42607]   no device acked (bus dead or all wiring off)\n");
    }
}

/* 寄存器空间 dump(诊断): 0x18 应答但 WHO_AM_I(0x75)=0x00 —— SC7U22TR 布局
 * 与英飞凌原版有出入, 从 dump 数据分布反推真实布局(BMI160 系 CHIP_ID@0x00=
 * 0xD1, ST 系@0x0F, Invensense 系@0x75)。16B 分块读, 单块 NACK 不拖垮整轮 */
static void icm_dump_regs(u8 addr7)
{
    u8 buf[16];

    s_addr_w = (u8)(addr7 << 1);
    s_addr_r = (u8)((addr7 << 1) | 1);
    printf("[icm42607] dump 0x%02x regs 0x00-0x7f:\n", addr7);
    for (u8 base = 0x00; base < 0x80; base += 16) {
        if (icm_read_regs(base, buf, 16) == 0) {
            printf("[icm42607] %02x: %02x %02x %02x %02x %02x %02x %02x %02x "
                   "%02x %02x %02x %02x %02x %02x %02x %02x\n",
                   base, buf[0], buf[1], buf[2], buf[3],
                   buf[4], buf[5], buf[6], buf[7],
                   buf[8], buf[9], buf[10], buf[11],
                   buf[12], buf[13], buf[14], buf[15]);
        } else {
            printf("[icm42607] %02x: read err\n", base);
        }
        wdt_clear();
    }
    {
        u8 id;
        if (!icm_read_regs(0x00, &id, 1)) {
            printf("[icm42607] cand reg0x00=%02x (BMI160 系 CHIP_ID=0xD1)\n", id);
        }
        if (!icm_read_regs(0x0F, &id, 1)) {
            printf("[icm42607] cand reg0x0F=%02x (ST 系 WHO_AM_I)\n", id);
        }
        if (!icm_read_regs(0x75, &id, 1)) {
            printf("[icm42607] cand reg0x75=%02x (Invensense 系 WHO_AM_I)\n", id);
        }
    }
}

int icm42607_init(void)
{
    u8 mclk = 0;

    if (s_ready) {
        return 0;
    }
    s_iic = dev_open("iic0", NULL);
    if (!s_iic) {
        printf("[icm42607] iic0 open fail\n");
        return -1;
    }

    icm_scan_bus();      /* 先扫总线确认实际地址(用户要求; 0x15=CST816 应在列) */

    /* SC7U22TR 兼容 ICM-42607-C 寄存器, 但器件地址在 0x18/0x19(实测 ACK @0x18) */
    if (probe_addr(ADDR7_SC7U_L) && probe_addr(ADDR7_SC7U_H) &&
        probe_addr(ADDR7_AD0_L) && probe_addr(ADDR7_AD0_H)) {
        printf("[icm42607] WHO_AM_I probe fail (try 0x18/0x19/0x68/0x69)\n");
        icm_dump_regs(ADDR7_SC7U_L);     /* 0x18 应答: dump 全寄存器反推布局 */
        return -2;
    }
    printf("[icm42607] WHO_AM_I=0x%02x addr=0x%02x\n", s_whoami, s_addr_w >> 1);

    /* soft reset */
    icm_write_reg(REG_SIGNAL_PATH_RESET, 0x10);
    os_time_dly(10);    /* ~100ms */

    /* gyro LN + accel LN */
    icm_write_reg(REG_PWR_MGMT0, 0x0F);
    os_time_dly(5);

    /* gyro ±2000dps, ODR~100Hz; accel ±16g, ODR~100Hz */
    icm_write_reg(REG_GYRO_CONFIG0, 0x06);
    icm_write_reg(REG_ACCEL_CONFIG0, 0x06);
    os_time_dly(5);

    icm_read_regs(REG_MCLK_RDY, &mclk, 1);
    printf("[icm42607] ready mclk=0x%02x\n", mclk);
    s_ready = 1;
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

int icm42607_read_raw(struct icm42607_raw *out)
{
    u8 b[14];

    if (!s_ready || !out) {
        return -1;
    }
    if (icm_read_regs(REG_TEMP_DATA1, b, 14)) {
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

int icm42607_read_si(struct icm42607_si *out)
{
    struct icm42607_raw r;

    if (icm42607_read_raw(&r)) {
        return -1;
    }
    out->ax = (float)r.ax / ACCEL_SENS_16G;
    out->ay = (float)r.ay / ACCEL_SENS_16G;
    out->az = (float)r.az / ACCEL_SENS_16G;
    out->gx = (float)r.gx / GYRO_SENS_2000DPS;
    out->gy = (float)r.gy / GYRO_SENS_2000DPS;
    out->gz = (float)r.gz / GYRO_SENS_2000DPS;
    /* datasheet: Temp_degC = (TEMP_DATA / 128) + 25 */
    out->temp_c = ((float)r.temp / 128.0f) + 25.0f;
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
int icm42607_read_raw(struct icm42607_raw *out)
{
    (void)out;
    return -1;
}
int icm42607_read_si(struct icm42607_si *out)
{
    (void)out;
    return -1;
}

#endif
