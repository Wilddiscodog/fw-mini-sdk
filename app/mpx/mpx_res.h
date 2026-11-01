#ifndef _MPX_RES_H_
#define _MPX_RES_H_

#include <stdint.h>

void mpx_texture_reset(void);

int mpx_texture_sample(uint32_t handle, uint32_t x, uint32_t y, uint32_t *rgb, uint8_t *alpha);

void mpx_display_scale(uint32_t *num, uint32_t *den);

int32_t mpx_resource_call(uint32_t method_id, const uint8_t *request, uint32_t request_size,
                          uint8_t *response, uint32_t response_capacity, uint32_t *response_size_out);

int32_t mpx_measure_text(const uint8_t *request, uint32_t request_size, uint8_t *response,
                         uint32_t response_capacity, uint32_t *response_size_out);

#endif
