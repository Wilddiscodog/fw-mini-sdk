#ifndef _MPX_HALL_H_
#define _MPX_HALL_H_

#include "app_config.h"

#if (defined MINI_WASM_SUPPORT) && MINI_WASM_SUPPORT && \
    (defined MINI_LVGL_SUPPORT) && MINI_LVGL_SUPPORT

/* 上电进入应用大厅。点卡片后由 mpx_task 启动对应 guest。 */
void mpx_hall_start(void);

#endif

#endif
