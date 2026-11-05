#ifndef IMU_H
#define IMU_H

#include "generic/typedef.h"
#include "sc7u22.h"

enum imu_chip {
    IMU_CHIP_NONE = 0,
    IMU_CHIP_SC7U22,
    IMU_CHIP_ICM42607,
};

/* 开机探测型号并加载对应驱动(可重复调用, 成功后幂等) */
int imu_init(void);
u8 imu_ready(void);
enum imu_chip imu_chip(void);
const char *imu_chip_name(void);
u8 imu_whoami(void);
int imu_read_raw(struct sc7u22_raw *out);
int imu_read_si(struct sc7u22_si *out);

#endif
