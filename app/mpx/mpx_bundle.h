/* app/mpx/mpx_bundle.h - MicroPixel App Bundle 加载器(杰理侧精简实现)
 *
 * 校验规则对齐 micropixel firmware runtime/bundle/bundle_reader.c:
 * FNV1a32 段哈希 / 64 字节对齐 / TOC 无重叠 / 段 kind 白名单。
 * v1 只取 WASM 字节码段(kind 5);ASSET/FONT 段留到 Graphics 资源接入。
 */
#ifndef _MPX_BUNDLE_H_
#define _MPX_BUNDLE_H_

#include <stdint.h>

typedef struct mpx_bundle {
    const char *app_id;      /* 指向 bundle 头内存储, NUL 结尾保证 */
    uint32_t app_id_length;
    const uint8_t *wasm;     /* 已通过 FNV 哈希校验的 wasm 字节码(源缓冲内) */
    uint32_t wasm_size;
} mpx_bundle_t;

/* data 在整个宿主生命周期内必须保持有效(固件资源区/内嵌数组)。 */
int32_t mpx_bundle_open(const uint8_t *data, uint32_t size, mpx_bundle_t *out);

#endif /* _MPX_BUNDLE_H_ */
