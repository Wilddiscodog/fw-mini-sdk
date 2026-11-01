/* app/mpx/mpx_services.h - MicroPixel ABI 服务层(杰理侧 C 实现)
 *
 * 对齐 micropixel firmware runtime/abi/service_registry.* 的线上语义:
 * 固定 16 slot,handle = index+1;open 校验接口主/次版本;
 * call/submit 按 descriptor 上限与 flag 校验。
 */
#ifndef _MPX_SERVICES_H_
#define _MPX_SERVICES_H_

#include <stdint.h>

#include "mpx_compat.h"

/* 应用时钟(10ms tick 粒度,M4 换硬件 µs 定时器) */
uint64_t mpx_app_time_now(void);

/* 事件投递(宿主内部用:定时器/触摸/图形释放 → guest event_wait) */
int32_t mpx_event_post(uint16_t event_id, uint32_t service_id, uint32_t source,
                       const void *payload, uint32_t payload_size);
/* 返回 MICROPIXEL_STATUS_OK / TIMEOUT / CLOSED */
int32_t mpx_event_wait(micropixel_event_t *event_out, uint64_t timeout_us);

void mpx_services_init(void);
void mpx_services_deinit(void);
/* 绑定当前 guest 模块实例(纹理/调色板上传按 Guest 偏移取数) */
void mpx_services_set_guest_inst(void *module_inst);

/* 由 mpx_host.c 的 native 调用(已按 ABI 语义校验过指针) */
int32_t mpx_service_open(uint32_t service_id, uint32_t required_interface_version,
                         micropixel_service_info_t *info_out, uint32_t info_capacity);
int32_t mpx_service_call(uint32_t service_handle, uint32_t method_id, const uint8_t *request,
                         uint32_t request_size, uint8_t *response, uint32_t response_capacity,
                         uint32_t *response_size_out);
int32_t mpx_service_submit(uint32_t service_handle, uint32_t channel_id, const uint8_t *bytes,
                           uint32_t length);

/* mpx_display.c:Graphics 服务的硬件侧(surface 缓冲 + 上屏) */
int32_t mpx_display_init(void);
void mpx_display_deinit(void);
int32_t mpx_display_get_info(micropixel_graphics_info_t *info);
int32_t mpx_display_surface_create(uint32_t width, uint32_t height, uint32_t pixel_format,
                                   uint32_t buffer_count, uint32_t *surface_handle_out);
int32_t mpx_display_surface_present(uint32_t surface_handle, uint32_t buffer_index);
void mpx_display_surface_destroy(uint32_t surface_handle);
/* Raster 目标缓冲(面板字节序 RGB565);无效句柄返回 INVALID_ARGUMENT */
int32_t mpx_display_surface_buffer(uint32_t surface_handle, uint32_t buffer_index,
                                   uint32_t *width_out, uint32_t *height_out,
                                   uint32_t *pitch_out, uint8_t **data_out);

/* Raster 槽位与 'MPRS' 执行(mpx_raster.c) */
int32_t mpx_raster_texture_upload(uint8_t slot, uint16_t width, uint16_t height,
                                  uint16_t layout, const uint8_t *texels, uint32_t length);
int32_t mpx_raster_palette_upload(uint8_t slot, uint16_t light_levels,
                                  const uint8_t *entries, uint32_t length);
int32_t mpx_raster_submit(const uint8_t *bytes, uint32_t length);
void mpx_raster_init(void);
void mpx_raster_deinit(void);

#endif /* _MPX_SERVICES_H_ */
