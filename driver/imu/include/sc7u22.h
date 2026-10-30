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

int sc7u22_init(void);
u8 sc7u22_ready(void);
u8 sc7u22_whoami(void);
int sc7u22_read_raw(struct sc7u22_raw *out);
int sc7u22_read_si(struct sc7u22_si *out);

#endif
