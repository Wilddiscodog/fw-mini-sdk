/* app/mpx/mpx_store.c - 官方 Bundle 的 flash 安装区与 /mpx HTTP。 */
#include "app_config.h"

#if (defined MINI_WASM_SUPPORT) && MINI_WASM_SUPPORT

#include "mpx_store.h"

#include <stdio.h>
#include <string.h>

#include "asm/sfc_norflash_api.h"
#include "asm/wdt.h"
#include "device/ioctl_cmds.h"
#include "os/os_api.h"

#include "mpx_bundle.h"

#define MPX_STORE_SECTOR 4096u
#define MPX_STORE_MAGIC "MPXAPPS"
#define MPX_STORE_VERSION 1u
#define MPX_STORE_SLOTS 8
#define MPX_STORE_CATALOG 0x00280000u
#define MPX_BANK_SPLIT 0x007fc000u
#define MPX_CODE_RESERVE 0x00270000u
#define MPX_VM_LIMIT 0x00ff4000u
#define MPX_PREFIX_MAX 8192u

typedef struct {
    char id[MPX_STORE_ID_MAX + 1];
    uint32_t offset;
    uint32_t size;
    uint32_t reserved;
} mpx_slot_t;

typedef struct {
    char magic[8];
    uint32_t version;
    uint32_t hash;
    uint32_t reserved;
    mpx_slot_t slots[MPX_STORE_SLOTS];
} mpx_catalog_t;

typedef struct {
    uint32_t begin;
    uint32_t end;
} mpx_region_t;

static const mpx_region_t mpx_regions[] = {
    {0x00281000u, 0x007f8000u},
    {0x00a7c000u, MPX_VM_LIMIT},
};

extern const uint8_t mpx_store_image[];
extern const uint8_t mpx_store_image_end[];

static mpx_catalog_t mpx_catalog;
static int mpx_catalog_ready;
static volatile int mpx_store_busy;
static volatile int mpx_launch_pending;
static const uint8_t *mpx_live_bundle;
static uint32_t mpx_live_bundle_size;
static char mpx_launch_id[MPX_STORE_ID_MAX + 1];
static uint8_t mpx_sector[MPX_STORE_SECTOR];
static uint8_t mpx_prefix[MPX_PREFIX_MAX];

void mpx_store_set_busy(int busy)
{
    mpx_store_busy = busy ? 1 : 0;
}

int mpx_store_is_busy(void)
{
    return mpx_store_busy;
}

static int mpx_id_ok(const char *id)
{
    unsigned n = 0;
    if (id == NULL || id[0] == '\0') {
        return 0;
    }
    while (id[n] != '\0') {
        char c = id[n];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        if (!ok || n >= MPX_STORE_ID_MAX) {
            return 0;
        }
        n++;
    }
    return 1;
}

static void mpx_copy_id(char *dst, const char *src)
{
    unsigned i = 0;
    while (src[i] != '\0' && i < MPX_STORE_ID_MAX) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int mpx_overlaps_firmware(uint32_t addr, uint32_t len)
{
    uint32_t banks[2];
    int i;
    if (len == 0 || addr + len < addr || addr + len > MPX_VM_LIMIT) {
        return 1;
    }
    banks[0] = 0;
    banks[1] = MPX_BANK_SPLIT;
    for (i = 0; i < 2; i++) {
        uint32_t code_end = banks[i] + MPX_CODE_RESERVE;
        if (addr < code_end && addr + len > banks[i]) {
            return 1;
        }
    }
    return 0;
}

/* mpx_sector 已经填好。擦一页再整页写。返回值以读回校验为准。 */
static int mpx_erase_write_sector(uint32_t addr)
{
    if ((addr & (MPX_STORE_SECTOR - 1u)) != 0 || mpx_overlaps_firmware(addr, MPX_STORE_SECTOR)) {
        return -1;
    }
    norflash_ioctl(NULL, IOCTL_ERASE_SECTOR, addr);
    norflash_write(NULL, mpx_sector, MPX_STORE_SECTOR, addr);
    wdt_clear();
    return 0;
}

static uint32_t mpx_catalog_hash(void)
{
    mpx_catalog_t tmp = mpx_catalog;
    tmp.hash = 0;
    return mpx_fnv1a32((const uint8_t *)&tmp, sizeof(tmp));
}

static const uint8_t *mpx_embedded_base(void)
{
    if (memcmp(mpx_store_image, MPX_STORE_MAGIC, 8) != 0) {
        return NULL;
    }
    if ((uint32_t)(mpx_store_image_end - mpx_store_image) < sizeof(mpx_catalog_t)) {
        return NULL;
    }
    return mpx_store_image;
}

static int mpx_embedded_catalog(mpx_catalog_t *out)
{
    mpx_catalog_t tmp;
    const uint8_t *base = mpx_embedded_base();
    uint32_t saved;
    if (base == NULL || out == NULL) {
        return -1;
    }
    memcpy(&tmp, base, sizeof(tmp));
    if (tmp.version != MPX_STORE_VERSION) {
        return -1;
    }
    saved = tmp.hash;
    tmp.hash = 0;
    if (mpx_fnv1a32((const uint8_t *)&tmp, sizeof(tmp)) != saved) {
        return -1;
    }
    tmp.hash = saved;
    *out = tmp;
    return 0;
}

static int mpx_embedded_find(const char *id, uint32_t *offset, uint32_t *size)
{
    mpx_catalog_t cat;
    int i;
    if (mpx_embedded_catalog(&cat) != 0) {
        return -1;
    }
    for (i = 0; i < MPX_STORE_SLOTS; i++) {
        if (cat.slots[i].id[0] != '\0' && strcmp(cat.slots[i].id, id) == 0) {
            *offset = cat.slots[i].offset;
            *size = cat.slots[i].size;
            return 0;
        }
    }
    return -1;
}

static void mpx_catalog_load(void)
{
    mpx_catalog_t tmp;
    if (mpx_catalog_ready) {
        return;
    }
    memset(&mpx_catalog, 0, sizeof(mpx_catalog));
    norflash_read(NULL, &tmp, sizeof(tmp), MPX_STORE_CATALOG);
    if (memcmp(tmp.magic, MPX_STORE_MAGIC, 8) == 0 && tmp.version == MPX_STORE_VERSION) {
        uint32_t saved = tmp.hash;
        tmp.hash = 0;
        if (mpx_fnv1a32((const uint8_t *)&tmp, sizeof(tmp)) == saved) {
            tmp.hash = saved;
            mpx_catalog = tmp;
        }
    }
    mpx_catalog_ready = 1;
}

static int mpx_catalog_commit(void)
{
    mpx_catalog.hash = 0;
    memcpy(mpx_catalog.magic, MPX_STORE_MAGIC, 8);
    mpx_catalog.version = MPX_STORE_VERSION;
    mpx_catalog.hash = mpx_catalog_hash();
    memset(mpx_sector, 0xff, sizeof(mpx_sector));
    memcpy(mpx_sector, &mpx_catalog, sizeof(mpx_catalog));
    if (mpx_erase_write_sector(MPX_STORE_CATALOG) != 0) {
        return -1;
    }
    return 0;
}

static int mpx_slot_index(const char *id)
{
    int i;
    for (i = 0; i < MPX_STORE_SLOTS; i++) {
        if (mpx_catalog.slots[i].id[0] != '\0' && strcmp(mpx_catalog.slots[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

static uint32_t mpx_slot_end(const mpx_slot_t *slot)
{
    uint32_t span = (slot->size + MPX_STORE_SECTOR - 1u) & ~(MPX_STORE_SECTOR - 1u);
    return slot->offset + span;
}

static int mpx_range_free(uint32_t begin, uint32_t end)
{
    int i;
    for (i = 0; i < MPX_STORE_SLOTS; i++) {
        const mpx_slot_t *slot = &mpx_catalog.slots[i];
        uint32_t slot_end;
        if (slot->id[0] == '\0' || slot->size == 0) {
            continue;
        }
        slot_end = mpx_slot_end(slot);
        if (begin < slot_end && slot->offset < end) {
            return 0;
        }
    }
    return 1;
}

static int mpx_alloc_extent(uint32_t size, uint32_t *offset_out)
{
    uint32_t span = (size + MPX_STORE_SECTOR - 1u) & ~(MPX_STORE_SECTOR - 1u);
    unsigned r;
    for (r = 0; r < sizeof(mpx_regions) / sizeof(mpx_regions[0]); r++) {
        uint32_t cursor = mpx_regions[r].begin;
        while (cursor + span <= mpx_regions[r].end) {
            if (mpx_range_free(cursor, cursor + span) && !mpx_overlaps_firmware(cursor, span)) {
                *offset_out = cursor;
                return 0;
            }
            cursor += MPX_STORE_SECTOR;
        }
    }
    return -1;
}

static int mpx_verify_bundle(uint32_t offset, uint32_t size, char *id_out)
{
    mpx_bundle_loc_t loc;
    uint32_t prefix_len = size < MPX_PREFIX_MAX ? size : MPX_PREFIX_MAX;
    uint32_t hash = 2166136261u;
    uint32_t left;
    uint32_t addr;
    int32_t status;

    norflash_read(NULL, mpx_prefix, prefix_len, offset);
    status = mpx_bundle_locate(mpx_prefix, prefix_len, size, &loc);
    if (status != 0) {
        return status;
    }
    left = loc.wasm_size;
    addr = offset + loc.wasm_offset;
    while (left) {
        uint32_t n = left > MPX_STORE_SECTOR ? MPX_STORE_SECTOR : left;
        norflash_read(NULL, mpx_sector, n, addr);
        hash = mpx_fnv1a32_update(hash, mpx_sector, n);
        addr += n;
        left -= n;
        wdt_clear();
    }
    if (hash != loc.wasm_hash) {
        return -8;
    }
    mpx_copy_id(id_out, loc.app_id);
    return 0;
}

int mpx_store_load_wasm(const char *id, uint8_t **out, uint32_t *size_out)
{
    int index;
    mpx_slot_t slot;
    mpx_bundle_loc_t loc;
    uint8_t *wasm;
    uint32_t left, addr, filled;
    uint32_t hash = 2166136261u;
    uint32_t prefix_len;
    int32_t status;

    if (out == NULL || size_out == NULL) {
        return -1;
    }
    *out = NULL;
    mpx_store_use_bundle(NULL, 0);
    {
        uint32_t embedded_off = 0;
        uint32_t embedded_size = 0;
        if (mpx_embedded_find(id, &embedded_off, &embedded_size) == 0) {
            mpx_bundle_t bundle;
            const uint8_t *base = mpx_store_image + embedded_off;
            int32_t st = mpx_bundle_open(base, embedded_size, &bundle);
            if (st != 0) {
                return st;
            }
            wasm = (uint8_t *)malloc(bundle.wasm_size + 4u);
            if (wasm == NULL) {
                return -3;
            }
            memcpy(wasm, bundle.wasm, bundle.wasm_size);
            *out = wasm;
            *size_out = bundle.wasm_size;
            mpx_store_use_bundle(base, embedded_size);
            return 0;
        }
    }
    mpx_catalog_load();
    index = mpx_slot_index(id);
    if (index < 0) {
        return -2;
    }
    slot = mpx_catalog.slots[index];
    prefix_len = slot.size < MPX_PREFIX_MAX ? slot.size : MPX_PREFIX_MAX;
    norflash_read(NULL, mpx_prefix, prefix_len, slot.offset);
    status = mpx_bundle_locate(mpx_prefix, prefix_len, slot.size, &loc);
    if (status != 0) {
        return status;
    }
    wasm = (uint8_t *)malloc(loc.wasm_size + 4u);
    if (wasm == NULL) {
        return -3;
    }
    left = loc.wasm_size;
    addr = slot.offset + loc.wasm_offset;
    filled = 0;
    while (left) {
        uint32_t n = left > MPX_STORE_SECTOR ? MPX_STORE_SECTOR : left;
        norflash_read(NULL, wasm + filled, n, addr);
        hash = mpx_fnv1a32_update(hash, wasm + filled, n);
        addr += n;
        filled += n;
        left -= n;
        wdt_clear();
    }
    if (hash != loc.wasm_hash) {
        free(wasm);
        return -8;
    }
    *out = wasm;
    *size_out = loc.wasm_size;
    return 0;
}

int mpx_store_list(char *buf, unsigned cap)
{
    int i;
    unsigned used = 0;
    mpx_catalog_load();
    if (buf == NULL || cap == 0) {
        return 0;
    }
    buf[0] = '\0';
    {
        mpx_catalog_t embedded;
        if (mpx_embedded_catalog(&embedded) == 0) {
            for (i = 0; i < MPX_STORE_SLOTS; i++) {
                const mpx_slot_t *slot = &embedded.slots[i];
                char line[80];
                int n;
                unsigned k;
                if (slot->id[0] == '\0') {
                    continue;
                }
                n = snprintf(line, sizeof(line), "%s %u\n", slot->id, (unsigned)slot->size);
                if (n < 0) {
                    break;
                }
                for (k = 0; k < (unsigned)n && used + 1 < cap; k++) {
                    buf[used++] = line[k];
                }
            }
        }
    }
    for (i = 0; i < MPX_STORE_SLOTS; i++) {
        const mpx_slot_t *slot = &mpx_catalog.slots[i];
        char line[80];
        int n;
        unsigned k;
        if (slot->id[0] == '\0') {
            continue;
        }
        n = snprintf(line, sizeof(line), "%s %u\n", slot->id, (unsigned)slot->size);
        if (n < 0) {
            break;
        }
        for (k = 0; k < (unsigned)n && used + 1 < cap; k++) {
            buf[used++] = line[k];
        }
    }
    if (used < cap) {
        buf[used] = '\0';
    }
    return (int)used;
}

int mpx_store_app_at(unsigned index, char *id, unsigned cap)
{
    mpx_catalog_t embedded;
    int have_embedded;
    unsigned seen = 0;
    int i;
    int j;

    if (id == NULL || cap < MPX_STORE_ID_MAX + 1) {
        return -1;
    }
    id[0] = '\0';
    have_embedded = mpx_embedded_catalog(&embedded) == 0;
    if (have_embedded) {
        for (i = 0; i < MPX_STORE_SLOTS; i++) {
            if (embedded.slots[i].id[0] == '\0') {
                continue;
            }
            if (seen == index) {
                mpx_copy_id(id, embedded.slots[i].id);
                return 0;
            }
            seen++;
        }
    }
    mpx_catalog_load();
    for (i = 0; i < MPX_STORE_SLOTS; i++) {
        int dup = 0;
        if (mpx_catalog.slots[i].id[0] == '\0') {
            continue;
        }
        if (have_embedded) {
            for (j = 0; j < MPX_STORE_SLOTS; j++) {
                if (strcmp(embedded.slots[j].id, mpx_catalog.slots[i].id) == 0) {
                    dup = 1;
                    break;
                }
            }
        }
        if (dup) {
            continue;
        }
        if (seen == index) {
            mpx_copy_id(id, mpx_catalog.slots[i].id);
            return 0;
        }
        seen++;
    }
    return -1;
}

int mpx_store_launch_png(const char *id, const uint8_t **data, uint32_t *size,
                         uint32_t *width, uint32_t *height)
{
    uint32_t offset;
    uint32_t bundle_size;
    const uint8_t *base;
    uint32_t image_size;

    if (mpx_embedded_find(id, &offset, &bundle_size) != 0) {
        return -1;
    }
    base = mpx_embedded_base();
    if (base == NULL) {
        return -1;
    }
    image_size = (uint32_t)(mpx_store_image_end - mpx_store_image);
    if (offset > image_size || bundle_size > image_size - offset) {
        return -1;
    }
    return mpx_bundle_launch_png(base + offset, bundle_size, data, size, width, height);
}

void mpx_store_use_bundle(const uint8_t *bundle, uint32_t size)
{
    mpx_live_bundle = bundle;
    mpx_live_bundle_size = size;
}

int mpx_store_live_png(uint32_t asset_id, const uint8_t **data, uint32_t *size,
                       uint32_t *width, uint32_t *height)
{
    if (mpx_live_bundle == NULL || mpx_live_bundle_size == 0) {
        return -1;
    }
    return mpx_bundle_png(mpx_live_bundle, mpx_live_bundle_size, asset_id, data, size, width, height);
}

int mpx_store_request_launch(const char *id)
{
    if (!mpx_id_ok(id) || mpx_store_busy || mpx_launch_pending) {
        return -1;
    }
    {
        uint32_t ignore_off, ignore_size;
        int in_image = mpx_embedded_find(id, &ignore_off, &ignore_size) == 0;
        if (!in_image) {
            mpx_catalog_load();
            if (mpx_slot_index(id) < 0) {
                return -2;
            }
        }
    }
    mpx_copy_id(mpx_launch_id, id);
    mpx_launch_pending = 1;
    return 0;
}

int mpx_store_take_launch(char *out, unsigned cap)
{
    unsigned i;
    if (!mpx_launch_pending || out == NULL || cap == 0) {
        return 0;
    }
    for (i = 0; i + 1 < cap && mpx_launch_id[i] != '\0'; i++) {
        out[i] = mpx_launch_id[i];
    }
    out[i] = '\0';
    mpx_launch_pending = 0;
    return 1;
}

#if (defined MINI_WIFI_SUPPORT) && MINI_WIFI_SUPPORT
#include "lwip/sockets.h"
#include "update/net_update.h"

static void mpx_http_send(int conn, const char *body)
{
    char hdr[128];
    int n = snprintf(hdr, sizeof(hdr),
                     "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n"
                     "Connection: close\r\nContent-Length: %u\r\n\r\n",
                     (unsigned)strlen(body));
    if (n > 0) {
        send(conn, hdr, n, 0);
    }
    send(conn, body, (int)strlen(body), 0);
}

static int mpx_header_length(const char *req, int hdr_end, const char *key)
{
    const char *p = req;
    const char *end = req + hdr_end;
    unsigned key_len = 0;
    while (key[key_len] != '\0') {
        key_len++;
    }
    while (p < end && *p != '\0') {
        unsigned i = 0;
        while (i < key_len) {
            char a = p[i];
            char b = key[i];
            if (a >= 'A' && a <= 'Z') {
                a = (char)(a - 'A' + 'a');
            }
            if (b >= 'A' && b <= 'Z') {
                b = (char)(b - 'A' + 'a');
            }
            if (a != b) {
                break;
            }
            i++;
        }
        if (i == key_len) {
            int value = 0;
            p += key_len;
            while (*p == ' ') {
                p++;
            }
            if (*p < '0' || *p > '9') {
                return -1;
            }
            while (*p >= '0' && *p <= '9') {
                value = value * 10 + (*p - '0');
                p++;
            }
            return value;
        }
        while (*p != '\0' && *p != '\n') {
            p++;
        }
        if (*p == '\n') {
            p++;
        }
    }
    return -1;
}

static int mpx_query_id(const char *req, const char *key, char *out)
{
    const char *line = req;
    const char *q;
    const char *line_end;
    unsigned key_len = 0;
    unsigned n = 0;
    while (key[key_len] != '\0') {
        key_len++;
    }
    line_end = strchr(line, '\r');
    q = strstr(line, key);
    if (q == NULL || line_end == NULL || q > line_end) {
        return -1;
    }
    q += key_len;
    while (q[n] != '\0' && q[n] != ' ' && q[n] != '&' && q[n] != '\r' && n < MPX_STORE_ID_MAX) {
        out[n] = q[n];
        n++;
    }
    out[n] = '\0';
    return mpx_id_ok(out) ? 0 : -1;
}

static int mpx_install_body(int conn, const uint8_t *already, int already_len, uint32_t total,
                            uint32_t offset)
{
    uint32_t got = 0;
    while (got < total) {
        uint32_t chunk = MPX_STORE_SECTOR;
        uint32_t filled = 0;
        if (chunk > total - got) {
            chunk = total - got;
        }
        memset(mpx_sector, 0xff, sizeof(mpx_sector));
        while (filled < chunk) {
            if (already_len > 0) {
                uint32_t n = (uint32_t)already_len;
                if (n > chunk - filled) {
                    n = chunk - filled;
                }
                memcpy(mpx_sector + filled, already, n);
                already += n;
                already_len -= (int)n;
                filled += n;
                continue;
            }
            {
                int r = recv(conn, (char *)mpx_sector + filled, (int)(chunk - filled), 0);
                if (r <= 0) {
                    return -1;
                }
                filled += (uint32_t)r;
            }
        }
        if (mpx_erase_write_sector(offset + got) != 0) {
            return -2;
        }
        got += chunk;
        if ((got & 0xffffu) == 0) {
            os_time_dly(1);
        }
    }
    return 0;
}

int mpx_http_handle(int conn, char *req, int hdr_end, int got)
{
    extern char mpx_status_line[160];
    extern volatile unsigned mpx_step_count;

    if (!strncmp(req, "GET /mpx/apps", 13)) {
        char body[512];
        int n = mpx_store_list(body, sizeof(body));
        if (n <= 0) {
            mpx_http_send(conn, "apps 0\n");
        } else {
            mpx_http_send(conn, body);
        }
        return 0;
    }
    if (!strncmp(req, "GET /mpx", 8)) {
        char body[220];
        snprintf(body, sizeof(body), "mpx: %s steps=%u\n", mpx_status_line, mpx_step_count);
        mpx_http_send(conn, body);
        return 0;
    }
    if (!strncmp(req, "POST /mpx/launch", 16)) {
        char id[MPX_STORE_ID_MAX + 1];
        int st;
        if (mpx_query_id(req, "id=", id) != 0) {
            mpx_http_send(conn, "ERR id\n");
            return 0;
        }
        st = mpx_store_request_launch(id);
        if (st == -2) {
            mpx_http_send(conn, "ERR not installed\n");
        } else if (st != 0) {
            mpx_http_send(conn, "ERR busy\n");
        } else {
            mpx_http_send(conn, "OK launch\n");
        }
        return 0;
    }
    if (!strncmp(req, "POST /mpx/install", 17)) {
        char id[MPX_STORE_ID_MAX + 1];
        char bundled_id[MPX_STORE_ID_MAX + 1];
        int length;
        int have;
        uint32_t offset = 0;
        int slot;
        int st;
        uint32_t old_span = 0;
        mpx_catalog_t saved_catalog;
        if (net_update_request() || mpx_store_busy || mpx_launch_pending) {
            mpx_http_send(conn, "ERR busy\n");
            return 0;
        }
        if (mpx_query_id(req, "id=", id) != 0) {
            mpx_http_send(conn, "ERR id\n");
            return 0;
        }
        length = mpx_header_length(req, hdr_end, "content-length:");
        if (length <= 128 || length > 6 * 1024 * 1024) {
            mpx_http_send(conn, "ERR length\n");
            return 0;
        }
        mpx_catalog_load();
        saved_catalog = mpx_catalog;
        slot = mpx_slot_index(id);
        if (slot >= 0) {
            old_span = mpx_slot_end(&mpx_catalog.slots[slot]) - mpx_catalog.slots[slot].offset;
        }
        if (slot >= 0 && old_span >= (uint32_t)length) {
            offset = mpx_catalog.slots[slot].offset;
        } else {
            if (slot >= 0) {
                mpx_catalog.slots[slot].id[0] = '\0';
                mpx_catalog.slots[slot].size = 0;
            }
            if (mpx_alloc_extent((uint32_t)length, &offset) != 0) {
                mpx_catalog = saved_catalog;
                mpx_http_send(conn, "ERR no space\n");
                return 0;
            }
        }
        have = got - hdr_end;
        if (have < 0) {
            have = 0;
        }
        st = mpx_install_body(conn, (const uint8_t *)req + hdr_end, have, (uint32_t)length, offset);
        if (st != 0) {
            mpx_catalog = saved_catalog;
            mpx_http_send(conn, st == -2 ? "ERR flash\n" : "ERR recv\n");
            return 0;
        }
        st = mpx_verify_bundle(offset, (uint32_t)length, bundled_id);
        if (st != 0) {
            char line[48];
            mpx_catalog = saved_catalog;
            snprintf(line, sizeof(line), "ERR bundle %d\n", st);
            mpx_http_send(conn, line);
            return 0;
        }
        if (strcmp(bundled_id, id) != 0) {
            mpx_catalog = saved_catalog;
            mpx_http_send(conn, "ERR id mismatch\n");
            return 0;
        }
        if (slot < 0) {
            int i;
            slot = -1;
            for (i = 0; i < MPX_STORE_SLOTS; i++) {
                if (mpx_catalog.slots[i].id[0] == '\0') {
                    slot = i;
                    break;
                }
            }
        }
        if (slot < 0) {
            mpx_http_send(conn, "ERR slots\n");
            return 0;
        }
        mpx_copy_id(mpx_catalog.slots[slot].id, id);
        mpx_catalog.slots[slot].offset = offset;
        mpx_catalog.slots[slot].size = (uint32_t)length;
        if (mpx_catalog_commit() != 0) {
            mpx_http_send(conn, "ERR catalog\n");
            return 0;
        }
        {
            char line[96];
            snprintf(line, sizeof(line), "OK %s %u\n", id, (unsigned)length);
            mpx_http_send(conn, line);
        }
        return 0;
    }
    mpx_http_send(conn, "ERR path\n");
    return 0;
}
#endif /* MINI_WIFI_SUPPORT */

#endif /* MINI_WASM_SUPPORT */
