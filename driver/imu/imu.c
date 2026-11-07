/*
 * 统一 IMU 入口: 开机探测 SC7U22 / ICM-42607-C, 再加载对应驱动
 * 总线同 CST816: PH0/PH1 iic0
 */
#include "system/includes.h"
#include "app_config.h"
#include "imu.h"
#include "sc7u22.h"
#include "icm42607.h"

#if (defined TCFG_SC7U22_ENABLE) && TCFG_SC7U22_ENABLE

static enum imu_chip s_chip = IMU_CHIP_NONE;
static u8 s_inited;

int imu_init(void)
{
    if (s_inited && s_chip != IMU_CHIP_NONE) {
        return 0;
    }

    /* 先探 SC7U22(0x18/0x19, WHO@0x01=0x6A) */
    if (sc7u22_init() == 0) {
        s_chip = IMU_CHIP_SC7U22;
        s_inited = 1;
        printf("[imu] chip=SC7U22 who=0x%02X\r\n", sc7u22_whoami());
        return 0;
    }

    /* 再探 ICM-42607-C(0x68/0x69, WHO@0x75=0x61) */
    if (icm42607_init() == 0) {
        s_chip = IMU_CHIP_ICM42607;
        s_inited = 1;
        printf("[imu] chip=ICM-42607-C who=0x%02X\r\n", icm42607_whoami());
        return 0;
    }

    s_chip = IMU_CHIP_NONE;
    s_inited = 1;
    printf("[imu] no IMU found on iic0\r\n");
    return -1;
}

u8 imu_ready(void)
{
    if (s_chip == IMU_CHIP_SC7U22) {
        return sc7u22_ready();
    }
    if (s_chip == IMU_CHIP_ICM42607) {
        return icm42607_ready();
    }
    return 0;
}

enum imu_chip imu_chip(void)
{
    return s_chip;
}

const char *imu_chip_name(void)
{
    switch (s_chip) {
    case IMU_CHIP_SC7U22:
        return "SC7U22";
    case IMU_CHIP_ICM42607:
        return "ICM-42607-C";
    default:
        return "NONE";
    }
}

u8 imu_whoami(void)
{
    if (s_chip == IMU_CHIP_SC7U22) {
        return sc7u22_whoami();
    }
    if (s_chip == IMU_CHIP_ICM42607) {
        return icm42607_whoami();
    }
    return 0;
}

int imu_read_raw(struct sc7u22_raw *out)
{
    if (s_chip == IMU_CHIP_SC7U22) {
        return sc7u22_read_raw(out);
    }
    if (s_chip == IMU_CHIP_ICM42607) {
        return icm42607_read_raw(out);
    }
    return -1;
}

int imu_read_si(struct sc7u22_si *out)
{
    if (s_chip == IMU_CHIP_SC7U22) {
        return sc7u22_read_si(out);
    }
    if (s_chip == IMU_CHIP_ICM42607) {
        return icm42607_read_si(out);
    }
    return -1;
}

u8 imu_pedo_supported(void)
{
    return (s_chip == IMU_CHIP_ICM42607 || s_chip == IMU_CHIP_SC7U22) ? 1 : 0;
}

int imu_pedo_enable(void)
{
    if (s_chip == IMU_CHIP_ICM42607) {
        return icm42607_pedo_enable();
    }
    if (s_chip == IMU_CHIP_SC7U22) {
        return sc7u22_pedo_enable();
    }
    return -1;
}

int imu_pedo_disable(void)
{
    if (s_chip == IMU_CHIP_ICM42607) {
        return icm42607_pedo_disable();
    }
    if (s_chip == IMU_CHIP_SC7U22) {
        return sc7u22_pedo_disable();
    }
    return -1;
}

u8 imu_pedo_enabled(void)
{
    if (s_chip == IMU_CHIP_ICM42607) {
        return icm42607_pedo_enabled();
    }
    if (s_chip == IMU_CHIP_SC7U22) {
        return sc7u22_pedo_enabled();
    }
    return 0;
}

int imu_pedo_read(struct imu_pedo *out)
{
    if (s_chip == IMU_CHIP_ICM42607) {
        return icm42607_pedo_read(out);
    }
    if (s_chip == IMU_CHIP_SC7U22) {
        return sc7u22_pedo_read(out);
    }
    return -1;
}

#else

int imu_init(void)
{
    return -1;
}
u8 imu_ready(void)
{
    return 0;
}
enum imu_chip imu_chip(void)
{
    return IMU_CHIP_NONE;
}
const char *imu_chip_name(void)
{
    return "OFF";
}
u8 imu_whoami(void)
{
    return 0;
}
int imu_read_raw(struct sc7u22_raw *out)
{
    (void)out;
    return -1;
}
int imu_read_si(struct sc7u22_si *out)
{
    (void)out;
    return -1;
}
u8 imu_pedo_supported(void)
{
    return 0;
}
int imu_pedo_enable(void)
{
    return -1;
}
int imu_pedo_disable(void)
{
    return -1;
}
u8 imu_pedo_enabled(void)
{
    return 0;
}
int imu_pedo_read(struct imu_pedo *out)
{
    (void)out;
    return -1;
}

#endif
