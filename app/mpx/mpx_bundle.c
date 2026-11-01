/* app/mpx/mpx_bundle.c - 见 mpx_bundle.h
 *
 * 头布局与 third/micropixel-abi/bundle_format.h 的 packed 结构一致,
 * 也与官方 tools/build_app_bundle.py 写出的 128 字节头一致。
 */
#include "mpx_bundle.h"

#include <string.h>

#include "bundle_format.h"

uint32_t mpx_fnv1a32_update(uint32_t hash, const uint8_t *data, uint32_t size)
{
    uint32_t i;
    for (i = 0; i < size; i++) {
        hash ^= data[i];
        hash *= 16777619U;
    }
    return hash;
}

uint32_t mpx_fnv1a32(const uint8_t *data, uint32_t size)
{
    return mpx_fnv1a32_update(2166136261U, data, size);
}

static int mpx_app_id_ok(const micropixel_bundle_header_t *header)
{
    uint32_t i;
    if (header->app_id_length == 0 ||
        header->app_id_length > MICROPIXEL_BUNDLE_APP_ID_MAX_LENGTH) {
        return 0;
    }
    for (i = 0; i < header->app_id_length; i++) {
        uint8_t c = header->app_id[i];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        if (!ok) {
            return 0;
        }
    }
    for (i = header->app_id_length; i < MICROPIXEL_BUNDLE_APP_ID_MAX_LENGTH; i++) {
        if (header->app_id[i] != 0) {
            return 0;
        }
    }
    return 1;
}

int32_t mpx_bundle_locate(const uint8_t *prefix, uint32_t prefix_len, uint32_t bundle_size,
                          mpx_bundle_loc_t *out)
{
    micropixel_bundle_header_t header;
    micropixel_bundle_header_t hashed;
    uint32_t toc_bytes, toc_end, cursor, i;
    const uint8_t *toc;

    if (prefix == NULL || out == NULL || prefix_len < sizeof(header) ||
        bundle_size < sizeof(header)) {
        return -1;
    }
    memcpy(&header, prefix, sizeof(header));
    if (memcmp(header.magic, MICROPIXEL_BUNDLE_MAGIC, sizeof(header.magic)) != 0) {
        return -2;
    }
    if (header.version != MICROPIXEL_BUNDLE_VERSION ||
        header.header_size != sizeof(header) ||
        header.framework_abi_version != MICROPIXEL_BUNDLE_FRAMEWORK_ABI_VERSION) {
        return -3;
    }
    if (header.bundle_size != bundle_size || header.section_count == 0 ||
        header.section_count > MICROPIXEL_BUNDLE_MAX_SECTIONS ||
        header.toc_offset != sizeof(header) ||
        header.reserved0 != 0 || header.reserved1 != 0 || header.reserved2 != 0 ||
        header.reserved3 != 0 || header.reserved4 != 0 || !mpx_app_id_ok(&header)) {
        return -5;
    }
    toc_bytes = header.section_count * (uint32_t)sizeof(micropixel_bundle_section_t);
    toc_end = header.toc_offset + toc_bytes;
    if (toc_end > prefix_len || toc_end > bundle_size) {
        return -5;
    }
    hashed = header;
    hashed.header_hash = 0;
    if (mpx_fnv1a32((const uint8_t *)&hashed, sizeof(hashed)) != header.header_hash) {
        return -4;
    }

    memset(out, 0, sizeof(*out));
    memcpy(out->app_id, header.app_id, header.app_id_length);
    out->app_id[header.app_id_length] = '\0';
    out->app_id_length = header.app_id_length;

    toc = prefix + header.toc_offset;
    cursor = toc_end;
    for (i = 0; i < header.section_count; i++) {
        micropixel_bundle_section_t section;
        memcpy(&section, toc + i * sizeof(section), sizeof(section));
        if (section.size == 0 || (section.offset & 63U) != 0 || section.offset < cursor ||
            section.offset > bundle_size || section.size > bundle_size - section.offset ||
            section.reserved1 != 0) {
            return -6;
        }
        cursor = section.offset + section.size;
        if (section.kind == MICROPIXEL_BUNDLE_SECTION_WASM) {
            if (out->wasm_size != 0 || section.id != 0 ||
                section.format != MICROPIXEL_BUNDLE_FORMAT_WASM_BYTECODE ||
                section.flags != 0 ||
                section.reserved0 != MICROPIXEL_BUNDLE_AOT_TARGET_MASK_WASM_INTERP) {
                return -7;
            }
            out->wasm_offset = section.offset;
            out->wasm_size = section.size;
            out->wasm_hash = section.hash;
        } else if (section.kind != MICROPIXEL_BUNDLE_SECTION_AOT &&
                   section.kind != MICROPIXEL_BUNDLE_SECTION_ASSET &&
                   section.kind != MICROPIXEL_BUNDLE_SECTION_APP_METADATA &&
                   section.kind != MICROPIXEL_BUNDLE_SECTION_FONT) {
            return -7;
        }
    }
    if (out->wasm_size == 0) {
        return -9;
    }
    return 0;
}

int32_t mpx_bundle_launch_png(const uint8_t *bundle, uint32_t bundle_size, const uint8_t **data,
                              uint32_t *size, uint32_t *width, uint32_t *height)
{
    micropixel_bundle_header_t header;

    if (bundle == NULL || bundle_size < sizeof(header)) {
        return -1;
    }
    memcpy(&header, bundle, sizeof(header));
    return mpx_bundle_png(bundle, bundle_size, header.launch_asset_id, data, size, width, height);
}

int32_t mpx_bundle_png(const uint8_t *bundle, uint32_t bundle_size, uint32_t asset_id,
                       const uint8_t **data, uint32_t *size, uint32_t *width, uint32_t *height)
{
    micropixel_bundle_header_t header;
    uint32_t toc_bytes;
    uint32_t toc_end;
    uint32_t i;
    int found = 0;
    micropixel_bundle_section_t launch;

    if (bundle == NULL || data == NULL || size == NULL || width == NULL || height == NULL ||
        asset_id == 0 || bundle_size < sizeof(header)) {
        return -1;
    }
    memcpy(&header, bundle, sizeof(header));
    if (memcmp(header.magic, MICROPIXEL_BUNDLE_MAGIC, sizeof(header.magic)) != 0 ||
        header.section_count == 0 || header.section_count > MICROPIXEL_BUNDLE_MAX_SECTIONS ||
        header.toc_offset != sizeof(header) || header.bundle_size != bundle_size) {
        return -1;
    }
    toc_bytes = header.section_count * (uint32_t)sizeof(micropixel_bundle_section_t);
    toc_end = header.toc_offset + toc_bytes;
    if (toc_end > bundle_size) {
        return -1;
    }
    memset(&launch, 0, sizeof(launch));
    for (i = 0; i < header.section_count; i++) {
        micropixel_bundle_section_t section;
        memcpy(&section, bundle + header.toc_offset + i * sizeof(section), sizeof(section));
        if (section.kind != MICROPIXEL_BUNDLE_SECTION_ASSET || section.id != asset_id) {
            continue;
        }
        if (found || section.size == 0 || section.width == 0 || section.height == 0 ||
            section.format != MICROPIXEL_BUNDLE_FORMAT_PNG || section.stride != 0 ||
            section.flags != 0 || section.reserved0 != 0 || section.reserved1 != 0 ||
            (section.offset & 63U) != 0 || section.offset < toc_end ||
            section.offset > bundle_size || section.size > bundle_size - section.offset) {
            return -1;
        }
        launch = section;
        found = 1;
    }
    if (!found || mpx_fnv1a32(bundle + launch.offset, launch.size) != launch.hash) {
        return -1;
    }
    *data = bundle + launch.offset;
    *size = launch.size;
    *width = launch.width;
    *height = launch.height;
    return 0;
}

int32_t mpx_bundle_open(const uint8_t *data, uint32_t size, mpx_bundle_t *out)
{
    mpx_bundle_loc_t loc;
    int32_t status;

    if (out == NULL) {
        return -1;
    }
    status = mpx_bundle_locate(data, size, size, &loc);
    if (status != 0) {
        return status;
    }
    if (mpx_fnv1a32(data + loc.wasm_offset, loc.wasm_size) != loc.wasm_hash) {
        return -8;
    }
    out->app_id = (const char *)(data + 24);
    out->app_id_length = loc.app_id_length;
    out->wasm = data + loc.wasm_offset;
    out->wasm_size = loc.wasm_size;
    return 0;
}
