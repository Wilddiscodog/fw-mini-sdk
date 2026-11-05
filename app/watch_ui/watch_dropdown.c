#include "app_config.h"
#include "system/includes.h"
#include "lvgl.h"
#include "watch_dropdown.h"

/* 下拉亮度/音量栏已移除, 调节入口改到设置界面; 保留空实现以兼容旧调用 */

lv_obj_t *watch_dropdown_create(lv_obj_t *parent)
{
    (void)parent;
    return NULL;
}

void watch_dropdown_show(void)
{
}

void watch_dropdown_hide(void)
{
}

u8 watch_dropdown_is_open(void)
{
    return 0;
}
