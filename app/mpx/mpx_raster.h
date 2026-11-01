/* app/mpx/mpx_raster.h - 'MPRS' RasterDrawList 执行器(杰理侧 C 重写)
 *
 * 语义对齐 micropixel firmware runtime/graphics/raster_kernels.cpp 的核心子集:
 * 两段式(先整表校验后逐条执行)、缓冲为面板字节序 RGB565、调色板在上传时
 * 一次性从 canonical(小端)转到面板字节序。v1 支持 RECT/SPRITE;其余记录
 * 校验即拒(整表拒绝, 未写任何像素)。
 */
#ifndef _MPX_RASTER_H_
#define _MPX_RASTER_H_

#include <stdint.h>

/* 纹理/调色板槽位上传(mpx_services.c 的 GRAPHICS 方法回调) */
int32_t mpx_raster_texture_upload(uint8_t slot, uint16_t width, uint16_t height,
                                  uint16_t layout, const uint8_t *texels, uint32_t length);
int32_t mpx_raster_palette_upload(uint8_t slot, uint16_t light_levels,
                                  const uint8_t *entries, uint32_t length);

/* 'MPRS' 提交:校验+执行,失败返回 MICROPIXEL_STATUS_* 且不写任何像素 */
int32_t mpx_raster_submit(const uint8_t *bytes, uint32_t length);

void mpx_raster_init(void);
void mpx_raster_deinit(void);

#endif /* _MPX_RASTER_H_ */
