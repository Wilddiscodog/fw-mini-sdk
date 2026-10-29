#ifndef ICM42607_H
#define ICM42607_H

#include "generic/typedef.h"

struct icm42607_raw {
    s16 ax, ay, az;
    s16 gx, gy, gz;
    s16 temp;
};

struct icm42607_si {
    float ax, ay, az;   /* g */
    float gx, gy, gz;   /* dps */
    float temp_c;
};

int icm42607_init(void);
u8 icm42607_ready(void);
u8 icm42607_whoami(void);
int icm42607_read_raw(struct icm42607_raw *out);
int icm42607_read_si(struct icm42607_si *out);

#endif
