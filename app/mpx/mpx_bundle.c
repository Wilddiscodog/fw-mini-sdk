/* app/mpx/mpx_bundle.c - 见 mpx_bundle.h */
#include "mpx_bundle.h"

#include <string.h>

#include "bundle_format.h"

#define MPX_MAX_SECTIONS MICROPIXEL_BUNDLE_MAX_SECTIONS
#define MPX_HEADER_SIZE 128U
#define MPX_TOC_ENTRY_SIZE 48U

static uint32_t mpx_fnv1a32(const uint8_t *data, uint32_t size)
{
    uint32_t hash = 2166136261U;
    uint32_t i;
    for (i = 0; i < size; i++) {
        hash ^= data[i];
        hash *= 16777619U;
    }
    return hash;
}

static uint32_t mpx_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int32_t mpx_bundle_open(const uint8_t *data, uint32_t size, mpx_bundle_t *out)
{
    const uint8_t *toc;
    uint32_t section_count, toc_end, cursor;
    uint32_t wasm_offset = 0, wasm_size = 0, wasm_hash = 0;
    uint32_t i;

    if (data == NULL || out == NULL || size < MPX_HEADER_SIZE + MPX_TOC_ENTRY_SIZE) {
        return -1;
    }
    if (memcmp(data, MICROPIXEL_BUNDLE_MAGIC, sizeof(MICROPIXEL_BUNDLE_MAGIC)) != 0) {
        return -2;
    }
    if (mpx_le32(data + 12) != MICROPIXEL_BUNDLE_VERSION || /* version */
        mpx_le32(data + 16) != MPX_HEADER_SIZE ||           /* header_size */
        mpx_le32(data + 28) != MICROPIXEL_BUNDLE_FRAMEWORK_ABI_VERSION) {
        return -3;
    }
    if (mpx_le32(data + 96) != 0 || mpx_le32(data + 100) != 0 || /* reserved0/1 */
        mpx_le32(data + 104) != 0 || mpx_le32(data + 108) != 0 || mpx_le32(data + 112) != 0) {
        return -3;
    }
    /* header_hash: FNV over the header with the hash field zeroed */
    {
        uint8_t header[MPX_HEADER_SIZE];
        uint32_t stored_hash = mpx_le32(data + 88);
        memcpy(header, data, sizeof(header));
        memset(header + 88, 0, 4);
        if (mpx_fnv1a32(header, sizeof(header)) != stored_hash) {
            return -4;
        }
    }

    if (mpx_le32(data + 20) > size) { /* bundle_size */
        return -5;
    }
    section_count = mpx_le32(data + 44); /* section_count */
    toc_end = MPX_HEADER_SIZE + section_count * MPX_TOC_ENTRY_SIZE;
    if (section_count == 0 || section_count > MPX_MAX_SECTIONS || mpx_le32(data + 24) != MPX_HEADER_SIZE ||
        toc_end > size) {
        return -5;
    }

    toc = data + MPX_HEADER_SIZE;
    cursor = toc_end;
    for (i = 0; i < section_count; i++) {
        const uint8_t *s = toc + i * MPX_TOC_ENTRY_SIZE;
        uint32_t kind = mpx_le32(s + 0);
        uint32_t id = mpx_le32(s + 4);
        uint32_t offset = mpx_le32(s + 8);
        uint32_t sec_size = mpx_le32(s + 12);
        uint32_t hash = mpx_le32(s + 16);
        uint32_t format = mpx_le32(s + 20);
        uint32_t flags = mpx_le32(s + 36);
        uint32_t reserved0 = mpx_le32(s + 40);
        uint32_t reserved1 = mpx_le32(s + 44);
        uint32_t j;

        if (sec_size == 0 || (offset & 63U) != 0 || offset < toc_end || offset > size ||
            sec_size > size - offset || reserved1 != 0) {
            return -6;
        }
        if (offset < cursor) {
            return -6; /* 重叠(TOC 顺序即存放顺序, reader 同款规则) */
        }
        cursor = offset + sec_size;

        if (kind == MICROPIXEL_BUNDLE_SECTION_WASM) {
            if (wasm_size != 0 || id != 0 || format != MICROPIXEL_BUNDLE_FORMAT_WASM_BYTECODE || flags != 0 ||
                reserved0 != MICROPIXEL_BUNDLE_AOT_TARGET_MASK_WASM_INTERP) {
                return -7;
            }
            if (mpx_fnv1a32(data + offset, sec_size) != hash) {
                return -8;
            }
            wasm_offset = offset;
            wasm_size = sec_size;
            wasm_hash = hash;
        } else if (kind == MICROPIXEL_BUNDLE_SECTION_AOT || kind == MICROPIXEL_BUNDLE_SECTION_ASSET ||
                   kind == MICROPIXEL_BUNDLE_SECTION_APP_METADATA || kind == MICROPIXEL_BUNDLE_SECTION_FONT) {
            /* 已识别但本宿主暂不消费;AOT 段在解释宿主上无意义 */
        } else {
            return -7;
        }
        (void)id;
        (void)j;
    }

    if (wasm_size == 0) {
        return -9; /* 解释宿主必须有 WASM 段 */
    }
    (void)wasm_hash;

    out->app_id_length = mpx_le32(data + 40);
    if (out->app_id_length == 0 || out->app_id_length > MICROPIXEL_BUNDLE_APP_ID_MAX_LENGTH) {
        return -5;
    }
    out->app_id = (const char *)(data + 32);
    out->wasm = data + wasm_offset;
    out->wasm_size = wasm_size;
    return 0;
}
