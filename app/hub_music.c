#include "app_config.h"
#include "system/includes.h"
#include "os/os_api.h"
#include "server/audio_server.h"
#include "server/server_core.h"
#include "network_download/net_download.h"
#include "system/wait.h"
#include "net/http/http_cli.h"
#include "walkie_hub.h"
#include "hub_music.h"
#include "syscfg/syscfg_id.h"
#include <string.h>
#include <stdlib.h>

/*
 * HUB 网络曲库播放器
 * list:  GET /cgi-bin/hub?cmd=list&ch=music
 * play:  GET /cgi-bin/hub?cmd=get&id=N  → net_download → audio_server
 * 解码:  mp3/flac/ape/wav/wma/m4a/aac/ogg/amr (与 Makefile 已链库一致)
 */

extern void bt_music_audio_takeover(void);
extern int lwip_canceladdrinfo(void);

#define HUB_MUSIC_VOL_MIN  0
#define HUB_MUSIC_VOL_MAX  100

struct hub_music_hdl {
    struct hub_music_track tracks[HUB_MUSIC_MAX_TRACKS];
    int n;
    int cur; /* -1=未选 */
    char volume;
    u8 vol_inited; /* 与 volume==0(静音)区分未加载 */
    u8 playing;
    u16 wait_dl;
    int dl_ready;
    void *net_file;
    struct server *dec;
    char url[256];
    char dec_type[8];
    volatile u8 list_busy;
    hub_music_list_cb list_cb;
    void *list_priv;
    u32 play_t0; /* 开播时刻, 用于识别 HUB 掐流导致的假 END */
};

static struct hub_music_hdl s_hm = { .cur = -1 };
#define __this (&s_hm)

static const struct audio_vfs_ops s_vfs = {
    .fread = net_download_read,
    .fseek = net_download_seek,
    .flen  = net_download_get_file_len,
};

/* ---- JSON 极简解析 (与 walkie_hub 同风格) ---- */

static const char *jfind(const char *p, const char *end, const char *key)
{
    char pat[24];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *hit = strstr(p, pat);
    if (hit && end && hit >= end) {
        return NULL;
    }
    return hit;
}

static long jnum(const char *p, const char *end, const char *key, long def)
{
    const char *hit = jfind(p, end, key);
    return hit ? strtol(hit + strlen(key) + 3, NULL, 10) : def;
}

static int jstr(const char *p, const char *end, const char *key, char *out, int outlen)
{
    const char *hit = jfind(p, end, key);
    const char *s, *e;
    int n;

    out[0] = '\0';
    if (!hit) {
        return -1;
    }
    s = strchr(hit + strlen(key) + 3, '"');
    if (!s || (end && s >= end)) {
        return -1;
    }
    s++;
    e = strchr(s, '"');
    if (!e || (end && e > end)) {
        return -1;
    }
    n = (int)(e - s);
    if (n >= outlen) {
        n = outlen - 1;
    }
    memcpy(out, s, n);
    out[n] = '\0';
    return 0;
}

struct hm_dl {
    u8 *buf;
    u32 cap;
    u32 len;
    int ok;
};

static int hm_dl_cb(void *cctx, void *buf, unsigned int size, void *priv, httpin_status st)
{
    struct hm_dl *dl = (struct hm_dl *)priv;
    httpcli_ctx *ctx = (httpcli_ctx *)cctx;

    switch (st) {
    case HTTPIN_PROGRESS:
        if (!dl->buf) {
            dl->cap = ctx->content_length > 0 ? (u32)ctx->content_length + 64 : 16384;
            if (dl->cap > 256 * 1024) {
                dl->cap = 256 * 1024; /* 列表 JSON 上限 */
            }
            dl->buf = malloc(dl->cap);
            if (!dl->buf) {
                dl->ok = -1;
                return -1;
            }
        }
        if (dl->len + size > dl->cap) {
            dl->ok = -1;
            return -1;
        }
        memcpy(dl->buf + dl->len, buf, size);
        dl->len += size;
        break;
    case HTTPIN_FINISHED:
        dl->ok = 1;
        break;
    case HTTPIN_ABORT:
    case HTTPIN_ERROR:
        dl->ok = -1;
        break;
    default:
        break;
    }
    return 0;
}

static const char *fmt_to_dec(const char *fmt)
{
    if (!fmt || !fmt[0]) {
        return NULL;
    }
    if (!strcmp(fmt, "mp3")) {
        return "mp3";
    }
    if (!strcmp(fmt, "flac")) {
        return "flac";
    }
    if (!strcmp(fmt, "ape")) {
        return "ape";
    }
    if (!strcmp(fmt, "wav")) {
        return "wav";
    }
    if (!strcmp(fmt, "wma")) {
        return "wma";
    }
    if (!strcmp(fmt, "amr")) {
        return "amr";
    }
    if (!strcmp(fmt, "ogg")) {
        return "ogg";
    }
    if (!strcmp(fmt, "m4a") || !strcmp(fmt, "aac")) {
        return "aac";
    }
    /* opus/dts 等本机未链或不稳, 跳过 */
    return NULL;
}

static int hm_ensure_dec(void)
{
    if (__this->dec) {
        return 0;
    }
    __this->dec = server_open("audio_server", "dec");
    return __this->dec ? 0 : -1;
}

static void hm_dec_stop(void)
{
    union audio_req r = {0};

    if (__this->wait_dl) {
        wait_completion_del(__this->wait_dl);
        __this->wait_dl = 0;
    }
    if (__this->net_file) {
        net_download_buf_inactive(__this->net_file);
        if (__this->dec) {
            r.dec.cmd = AUDIO_DEC_STOP;
            server_request(__this->dec, AUDIO_REQ_DEC, &r);
        }
        lwip_canceladdrinfo();
        net_download_close(__this->net_file);
        __this->net_file = NULL;
    }
    __this->playing = 0;
}

static int hm_dl_ready(void *p)
{
    (void)p;
    __this->dl_ready = net_download_check_ready(__this->net_file);
    return __this->dl_ready ? 1 : 0;
}

static int hm_play_looks_truncated(void)
{
    u32 played_ms;
    u32 expect_bytes = 0;
    int flen = 0;

    if (__this->cur < 0 || __this->cur >= __this->n) {
        return 0;
    }
    expect_bytes = __this->tracks[__this->cur].bytes;
    played_ms = timer_get_ms() - __this->play_t0;
    if (__this->net_file) {
        flen = net_download_get_file_len(__this->net_file);
    }
    /* HUB CGI 默认 ~60s 掐断: 大文件播不到 2 分钟就 EOF */
    if (expect_bytes > (2u * 1024u * 1024u) && played_ms < 120000u) {
        printf("[hub-music] truncated? play=%ums expect_bytes=%u flen=%d\n",
               (unsigned)played_ms, (unsigned)expect_bytes, flen);
        return 1;
    }
    (void)flen;
    return 0;
}

static void hm_dec_event(void *priv, int argc, int *argv)
{
    (void)priv;
    (void)argc;
    switch (argv[0]) {
    case AUDIO_SERVER_EVENT_ERR:
    case AUDIO_SERVER_EVENT_END:
        if (hm_play_looks_truncated()) {
            printf("[hub-music] incomplete stream, skip auto-next\n");
            hm_dec_stop();
            break;
        }
        printf("[hub-music] track end\n");
        hm_dec_stop();
        /* 自动下一首 */
        if (__this->n > 0 && __this->cur >= 0) {
            int next = __this->cur + 1;
            if (next >= __this->n) {
                next = 0;
            }
            hub_music_play_idx(next);
        }
        break;
    default:
        break;
    }
}

static int hm_start_dec(void *file)
{
    union audio_req req = {0};
    int err;
    const char *dtype;

    __this->wait_dl = 0;
    if (__this->dl_ready < 0) {
        goto fail;
    }

    dtype = __this->dec_type[0] ? __this->dec_type : net_download_get_media_type(file);
    if (!dtype) {
        printf("[hub-music] unknown media type\n");
        goto fail;
    }

    if (hm_ensure_dec()) {
        goto fail;
    }
    server_register_event_handler(__this->dec, NULL, hm_dec_event);

    bt_music_audio_takeover();
    net_download_set_read_timeout(file, 8000);

    req.dec.cmd            = AUDIO_DEC_OPEN;
    req.dec.volume         = __this->volume;
    req.dec.output_buf_len = 6 * 1024;
    req.dec.file           = (FILE *)file;
    req.dec.channel        = 0;
    req.dec.sample_rate    = 0;
    req.dec.priority       = 1;
    req.dec.vfs_ops        = &s_vfs;
    req.dec.dec_type       = dtype;
    req.dec.sample_source  = CONFIG_AUDIO_DEC_PLAY_SOURCE;

    err = server_request(__this->dec, AUDIO_REQ_DEC, &req);
    if (err) {
        printf("[hub-music] DEC_OPEN err=%d type=%s\n", err, dtype);
        goto fail;
    }
    net_download_set_read_timeout(file, 0);
    req.dec.cmd = AUDIO_DEC_START;
    server_request(__this->dec, AUDIO_REQ_DEC, &req);
    net_download_set_pp(file, 0);
    __this->playing = 1;
    __this->play_t0 = timer_get_ms();
    printf("[hub-music] playing type=%s\n", dtype);
    return 0;

fail:
    hm_dec_stop();
    return -1;
}

/* ---- public ---- */

int hub_music_refresh(void)
{
    char url[192];
    struct hm_dl dl;
    httpcli_ctx ctx;
    httpin_error e;
    const char *p, *obj;
    struct hub_music_track tmp[HUB_MUSIC_MAX_TRACKS];
    int nn = 0;

    memset(&dl, 0, sizeof(dl));
    snprintf(url, sizeof(url),
             "http://%s:%d/cgi-bin/hub?cmd=list&key=%s&ch=music&since=0&limit=%d",
             walkie_hub_ip(), walkie_hub_port(), walkie_hub_key(),
             HUB_MUSIC_MAX_TRACKS);

    memset(&ctx, 0, sizeof(ctx));
    ctx.url = url;
    ctx.cb = hm_dl_cb;
    ctx.priv = &dl;
    ctx.timeout_millsec = WALKIE_HUB_TIMEOUT;
    ctx.connection = "close";
    e = httpcli_get(&ctx);
    if (e != HERROR_OK || dl.ok != 1 || !dl.buf) {
        if (dl.buf) {
            free(dl.buf);
        }
        printf("[hub-music] list fail\n");
        return -1;
    }

    memset(tmp, 0, sizeof(tmp));
    p = strstr((const char *)dl.buf, "\"rows\":[");
    if (!p) {
        free(dl.buf);
        __this->n = 0;
        return 0;
    }
    obj = strstr(p + 8, "{\"id\":");
    while (obj && nn < HUB_MUSIC_MAX_TRACKS) {
        const char *objend = strchr(obj, '}');
        char fmt[8], title[HUB_MUSIC_TITLE_LEN];
        long id, bytes;

        if (!objend) {
            break;
        }
        id = jnum(obj, objend, "id", 0);
        bytes = jnum(obj, objend, "bytes", 0);
        jstr(obj, objend, "fmt", fmt, sizeof(fmt));
        jstr(obj, objend, "txt", title, sizeof(title));
        if (id > 0 && fmt_to_dec(fmt)) {
            tmp[nn].id = (u32)id;
            tmp[nn].bytes = (u32)bytes;
            snprintf(tmp[nn].fmt, sizeof(tmp[nn].fmt), "%s", fmt);
            snprintf(tmp[nn].title, sizeof(tmp[nn].title), "%s",
                     title[0] ? title : "未命名");
            nn++;
        }
        obj = strstr(objend, "{\"id\":");
    }
    free(dl.buf);
    memcpy(__this->tracks, tmp, sizeof(tmp));
    __this->n = nn;
    if (__this->cur >= nn) {
        __this->cur = nn ? 0 : -1;
    }
    printf("[hub-music] listed %d playable tracks\n", nn);
    return nn;
}

static void hm_list_task(void *arg)
{
    int n;

    (void)arg;
    n = hub_music_refresh();
    __this->list_busy = 0;
    if (__this->list_cb) {
        __this->list_cb(n, __this->list_priv);
    }
}

int hub_music_refresh_async(hub_music_list_cb cb, void *priv)
{
    if (__this->list_busy) {
        return -1;
    }
    __this->list_busy = 1;
    __this->list_cb = cb;
    __this->list_priv = priv;
    if (thread_fork("hm_list", 12, 8192, 0, 0, hm_list_task, NULL)) {
        __this->list_busy = 0;
        printf("[hub-music] list task fork fail\n");
        return -2;
    }
    return 0;
}

u8 hub_music_list_busy(void)
{
    return __this->list_busy;
}

int hub_music_count(void)
{
    return __this->n;
}

const struct hub_music_track *hub_music_get(int idx)
{
    if (idx < 0 || idx >= __this->n) {
        return NULL;
    }
    return &__this->tracks[idx];
}

int hub_music_play_idx(int idx)
{
    struct net_download_parm parm = {0};
    const struct hub_music_track *t;
    const char *dtype;
    int err;

    if (idx < 0 || idx >= __this->n) {
        return -1;
    }
    t = &__this->tracks[idx];
    dtype = fmt_to_dec(t->fmt);
    if (!dtype) {
        return -2;
    }

    hm_dec_stop();
    __this->cur = idx;
    snprintf(__this->dec_type, sizeof(__this->dec_type), "%s", dtype);
    /* 切歌用缓存 IP, 避免每首 DISCOVER(~2s); 列表 refresh 已 ensure */
    snprintf(__this->url, sizeof(__this->url),
             "http://%s:%d/cgi-bin/hub?cmd=get&key=%s&id=%u",
             walkie_hub_ip_cached(), walkie_hub_port(), walkie_hub_key(),
             (unsigned)t->id);

    if (!__this->vol_inited) {
        char v = 50;
        if (syscfg_read(CFG_MUSIC_VOL, &v, 1) == 1) {
            if (v < HUB_MUSIC_VOL_MIN) {
                v = HUB_MUSIC_VOL_MIN;
            } else if (v > HUB_MUSIC_VOL_MAX) {
                v = HUB_MUSIC_VOL_MAX;
            }
            __this->volume = v;
        } else {
            __this->volume = 50;
        }
        __this->vol_inited = 1;
    }

    parm.url = __this->url;
    parm.cbuf_size = 500 * 1024;
    parm.timeout_millsec = 30000;
    parm.max_reconnect_cnt = 3;
    parm.seek_threshold = 1024 * 200;
    /* 大文件(ape)多缓冲一点再开播 */
    parm.start_play_threshold = 64 * 1024;

    err = net_download_open(&__this->net_file, &parm);
    if (err) {
        printf("[hub-music] download open err=%d\n", err);
        return err;
    }
    __this->wait_dl = wait_completion(hm_dl_ready,
                                      (int (*)(void *))hm_start_dec,
                                      __this->net_file, NULL);
    return 0;
}

int hub_music_pause_resume(void)
{
    union audio_req r = {0};

    if (!__this->dec || !__this->net_file) {
        return -1;
    }
    r.dec.cmd = AUDIO_DEC_PP;
    server_request(__this->dec, AUDIO_REQ_DEC, &r);
    if (r.dec.status == AUDIO_DEC_START) {
        net_download_set_pp(__this->net_file, 0);
        __this->playing = 1;
    } else if (r.dec.status == AUDIO_DEC_PAUSE) {
        net_download_set_pp(__this->net_file, 1);
        __this->playing = 0;
    }
    return 0;
}

void hub_music_stop(void)
{
    hm_dec_stop();
}

int hub_music_next(void)
{
    if (__this->n <= 0) {
        return -1;
    }
    return hub_music_play_idx((__this->cur + 1) % __this->n);
}

int hub_music_prev(void)
{
    if (__this->n <= 0) {
        return -1;
    }
    return hub_music_play_idx((__this->cur - 1 + __this->n) % __this->n);
}

u8 hub_music_is_playing(void)
{
    return __this->playing;
}

u8 hub_music_is_busy(void)
{
    return (__this->wait_dl || (__this->net_file && !__this->playing)) ? 1 : 0;
}

int hub_music_cur_idx(void)
{
    return __this->cur;
}

int hub_music_volume_get(void)
{
    if (!__this->vol_inited) {
        char v = 50;
        if (syscfg_read(CFG_MUSIC_VOL, &v, 1) == 1) {
            if (v < HUB_MUSIC_VOL_MIN) {
                v = HUB_MUSIC_VOL_MIN;
            } else if (v > HUB_MUSIC_VOL_MAX) {
                v = HUB_MUSIC_VOL_MAX;
            }
            __this->volume = v;
        } else {
            __this->volume = 50;
        }
        __this->vol_inited = 1;
    }
    return (int)(u8)__this->volume;
}

int hub_music_volume_set(int vol)
{
    union audio_req req = {0};

    if (vol < HUB_MUSIC_VOL_MIN) {
        vol = HUB_MUSIC_VOL_MIN;
    } else if (vol > HUB_MUSIC_VOL_MAX) {
        vol = HUB_MUSIC_VOL_MAX;
    }
    __this->volume = (char)vol;
    __this->vol_inited = 1;
    syscfg_write(CFG_MUSIC_VOL, &__this->volume, sizeof(__this->volume));
    if (__this->dec) {
        req.dec.cmd = AUDIO_DEC_SET_VOLUME;
        req.dec.volume = vol;
        server_request(__this->dec, AUDIO_REQ_DEC, &req);
    }
    return 0;
}
