#ifndef ICM42607_H
#define ICM42607_H

#include "generic/typedef.h"
#include "sc7u22.h" /* 复用 sc7u22_raw / sc7u22_si 数据布局 */

int icm42607_init(void);
u8 icm42607_ready(void);
u8 icm42607_whoami(void);
int icm42607_read_raw(struct sc7u22_raw *out);
int icm42607_read_si(struct sc7u22_si *out);

#endif
