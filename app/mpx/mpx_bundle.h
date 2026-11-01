/* app/mpx/mpx_bundle.h - MicroPixel App Bundle 加载器(杰理侧精简实现)
 *
 * 校验规则对齐 micropixel firmware runtime/bundle/bundle_reader.c:
 * FNV1a32 段哈希 / 64 字节对齐 / TOC 无重叠 / 段 kind 白名单。
 * v1 只取 WASM 字节码段(kind 5);ASSET/FONT 段留到 Graphics 资源接入。
 */
#ifndef _MPX_BUNDLE_H_
#define _MPX_BUNDLE_H_

#include <stdint.h>

#include "bundle_format.h"

typedef struct mpx_bundle {
    const char *app_id;      /* 指向 bundle 头内存储, NUL 结尾保证 */
    uint32_t app_id_length;
    const uint8_t *wasm;     /* 已通过 FNV 哈希校验的 wasm 字节码(源缓冲内) */
    uint32_t wasm_size;
} mpx_bundle_t;

/* 只解析前缀(头 + TOC)。prefix_len 必须盖住 TOC, bundle_size 是整包长度。 */
typedef struct mpx_bundle_loc {
    char app_id[MICROPIXEL_BUNDLE_APP_ID_MAX_LENGTH + 1];
    uint32_t app_id_length;
    uint32_t wasm_offset;
    uint32_t wasm_size;
    uint32_t wasm_hash;
} mpx_bundle_loc_t;

uint32_t mpx_fnv1a32(const uint8_t *data, uint32_t size);
uint32_t mpx_fnv1a32_update(uint32_t hash, const uint8_t *data, uint32_t size);
int32_t mpx_bundle_locate(const uint8_t *prefix, uint32_t prefix_len, uint32_t bundle_size,
                          mpx_bundle_loc_t *out);

/* 取出 launch_asset_id 指向的 PNG。data 指向 bundle 内部，调用方不要 free。 */
int32_t mpx_bundle_launch_png(const uint8_t *bundle, uint32_t bundle_size, const uint8_t **data,
                              uint32_t *size, uint32_t *width, uint32_t *height);

/* 按 section id 取 PNG 资源。data 指向 bundle 内部，调用方不要 free。 */
int32_t mpx_bundle_png(const uint8_t *bundle, uint32_t bundle_size, uint32_t asset_id,
                       const uint8_t **data, uint32_t *size, uint32_t *width, uint32_t *height);

/* data 在整个宿主生命周期内必须保持有效(固件资源区/内嵌数组)。 */
int32_t mpx_bundle_open(const uint8_t *data, uint32_t size, mpx_bundle_t *out);

#endif /* _MPX_BUNDLE_H_ */
