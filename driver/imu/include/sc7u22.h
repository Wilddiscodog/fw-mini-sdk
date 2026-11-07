#ifndef SC7U22_H
#define SC7U22_H

#include "generic/typedef.h"

struct sc7u22_raw {
    s16 ax, ay, az;
    s16 gx, gy, gz;
    s16 temp;
};

struct sc7u22_si {
    float ax, ay, az;   /* g */
    float gx, gy, gz;   /* dps */
    float temp_c;
};

/* 与 ICM APEX 共用布局, 供 imu_pedo_read 统一输出 */
struct imu_pedo {
    u32 step_count;     /* 相对开启时刻的累计步数(含 16bit 溢出续计) */
    u16 step_raw;       /* 芯片原始步数 */
    u8  cadence;        /* 步频原始值; SC7U22 无此字段时为 0 */
    u8  activity;       /* 0=未知/静坐 1=走 2=跑 */
};

int sc7u22_init(void);
u8 sc7u22_ready(void);
u8 sc7u22_whoami(void);
int sc7u22_read_raw(struct sc7u22_raw *out);
int sc7u22_read_si(struct sc7u22_si *out);

/* 片内可编程计步 */
int sc7u22_pedo_enable(void);
int sc7u22_pedo_disable(void);
u8 sc7u22_pedo_enabled(void);
int sc7u22_pedo_read(struct imu_pedo *out);

#endif
