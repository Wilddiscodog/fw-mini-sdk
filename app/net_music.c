/*
 * 网络音乐 / DLNA MediaRenderer(移植自 demo_audio/demo/net_music.c)
 *
 * 流程: STA DHCP → ai_server CONNECT → libdlna 广播 → 手机投屏 URL
 *       → network_download → audio_server 解码到 DAC。
 * 不依赖 REGISTER_APPLICATION 模式机; 由 wifi DHCP / 系统事件拉起。
 */
#include "app_config.h"

#ifdef CONFIG_NET_MUSIC_MODE_ENABLE

#include "server/audio_server.h"
#include "server/server_core.h"
#include "server/ai_server.h"
#include "network_download/net_download.h"
#include "event/net_event.h"
#include "syscfg/syscfg_id.h"
#include "system/wait.h"
#include "generic/log.h"
#include "lwip.h"
#include "net_music.h"
#include <string.h>
#include <stdlib.h>

#define VOLUME_STEP         5
#define MIN_VOLUME_VALUE    5
#define MAX_VOLUME_VALUE    100
#define INIT_VOLUME_VALUE   50

struct net_music_hdl {
    char volume;
    u16 wait_download;
    int download_ready;
    int play_time;
    int total_time;
    void *net_file;
    const char *ai_name;
    struct server *dec_server;
    struct server *ai_server;
    char *url;
    u8 started;
};

static struct net_music_hdl net_music_handler;
#define __this  (&net_music_handler)

extern void bt_music_audio_takeover(void);

static const struct audio_vfs_ops net_audio_dec_vfs_ops = {
    .fread = net_download_read,
    .fseek = net_download_seek,
    .flen  = net_download_get_file_len,
};

int get_app_music_volume(void)
{
    return __this->volume;
}

int get_app_music_playtime(void)
{
    return __this->play_time;
}

int get_app_music_total_time(void)
{
    return __this->total_time;
}

static int __net_download_ready(void *p)
{
    (void)p;
    __this->download_ready = net_download_check_ready(__this->net_file);
    return __this->download_ready ? 1 : 0;
}

static int net_music_dec_play_pause(u8 notify)
{
    union audio_req r = {0};
    union ai_req req = {0};

    r.dec.cmd = AUDIO_DEC_PP;
    server_request(__this->dec_server, AUDIO_REQ_DEC, &r);
    if (r.dec.status == AUDIO_DEC_START) {
        net_download_set_pp(__this->net_file, 0);
    } else if (r.dec.status == AUDIO_DEC_PAUSE) {
        net_download_set_pp(__this->net_file, 1);
    }

    if (notify && __this->ai_server && r.dec.status != AUDIO_DEC_STOP) {
        req.evt.event   = AI_EVENT_PLAY_PAUSE;
        req.evt.ai_name = __this->ai_name;
        if (r.dec.status == AUDIO_DEC_PAUSE) {
            req.evt.arg = 1;
        }
        ai_server_request(__this->ai_server, AI_REQ_EVENT, &req);
    }
    return 0;
}

static int net_music_dec_stop(void)
{
    union audio_req r = {0};

    if (!__this->net_file) {
        return 0;
    }

    printf("[net-music] stop\n");
    net_download_buf_inactive(__this->net_file);

    if (__this->wait_download) {
        wait_completion_del(__this->wait_download);
        __this->wait_download = 0;
    } else if (__this->dec_server) {
        r.dec.cmd = AUDIO_DEC_STOP;
        server_request(__this->dec_server, AUDIO_REQ_DEC, &r);

        int argv[2];
        argv[0] = AUDIO_SERVER_EVENT_END;
        argv[1] = (int)__this->net_file;
        server_event_handler_del(__this->dec_server, 2, argv);
    }

    extern int lwip_canceladdrinfo(void);
    lwip_canceladdrinfo();
    net_download_close(__this->net_file);
    __this->net_file = NULL;
    return 0;
}

void net_music_audio_takeover(void)
{
    net_music_dec_stop();
}

static int net_music_dec_end(void)
{
    union ai_req req = {0};

    printf("[net-music] end\n");
    net_music_dec_stop();
    if (!__this->ai_server) {
        return 0;
    }
    req.evt.event   = AI_EVENT_MEDIA_END;
    req.evt.ai_name = __this->ai_name;
    return ai_server_request(__this->ai_server, AI_REQ_EVENT, &req);
}

static int __net_music_dec_file(void *file)
{
    int err;
    union ai_req r = {0};
    union audio_req req = {0};

    __this->wait_download = 0;

    if (__this->download_ready < 0) {
        goto __err;
    }

    req.dec.dec_type = net_download_get_media_type(file);
    if (req.dec.dec_type == NULL) {
        goto __err;
    }
    printf("[net-music] type: %s\n", req.dec.dec_type);

    /* 投屏与 A2DP 互斥: 占 DAC 前先停蓝牙音乐 */
    bt_music_audio_takeover();

    net_download_set_read_timeout(file, 5000);

    req.dec.cmd             = AUDIO_DEC_OPEN;
    req.dec.volume          = __this->volume;
    req.dec.output_buf_len  = 6 * 1024;
    req.dec.file            = (FILE *)file;
    req.dec.channel         = 0;
    req.dec.sample_rate     = 0;
    req.dec.priority        = 1;
    req.dec.vfs_ops         = &net_audio_dec_vfs_ops;
    req.dec.sample_source   = CONFIG_AUDIO_DEC_PLAY_SOURCE;

    err = server_request(__this->dec_server, AUDIO_REQ_DEC, &req);
    if (err) {
        goto __err;
    }

    __this->play_time = req.dec.play_time;
    __this->total_time = req.dec.total_time;

    net_download_set_read_timeout(file, 0);
    req.dec.cmd = AUDIO_DEC_START;
    server_request(__this->dec_server, AUDIO_REQ_DEC, &req);
    net_download_set_pp(file, 0);
    return 0;

__err:
    printf("[net-music] play fail\n");
    net_download_buf_inactive(file);
    if (__this->dec_server) {
        req.dec.cmd = AUDIO_DEC_STOP;
        server_request(__this->dec_server, AUDIO_REQ_DEC, &req);
    }
    net_download_close(file);
    __this->net_file = NULL;
    if (__this->ai_server) {
        r.evt.event   = AI_EVENT_MEDIA_END;
        r.evt.ai_name = __this->ai_name;
        ai_server_request(__this->ai_server, AI_REQ_EVENT, &r);
    }
    return -1;
}

static int net_music_dec_file(const char *url)
{
    int err;
    struct net_download_parm parm = {0};

    net_music_dec_stop();
    if (!url) {
        return -1;
    }

    printf("[net-music] open url\n");
    parm.url = url;
#ifdef CONFIG_NO_SDRAM_ENABLE
    parm.cbuf_size = 150 * 1024;
#else
    parm.cbuf_size = 500 * 1024;
#endif
    parm.timeout_millsec = 10000;
    parm.seek_threshold  = 1024 * 200;

    err = net_download_open(&__this->net_file, &parm);
    if (err) {
        printf("[net-music] download open err=%d\n", err);
        return err;
    }

    __this->wait_download = wait_completion(__net_download_ready,
                                            (int (*)(void *))__net_music_dec_file,
                                            __this->net_file, NULL);
    return 0;
}

static int net_music_get_dec_status(void)
{
    union audio_req req = {0};
    req.dec.cmd = AUDIO_DEC_GET_STATUS;
    server_request(__this->dec_server, AUDIO_REQ_DEC, &req);
    return req.dec.status;
}

static int net_music_set_dec_volume(int step)
{
    union audio_req req = {0};
    union ai_req ai = {0};
    int volume = __this->volume + step;

    if (volume < MIN_VOLUME_VALUE) {
        volume = MIN_VOLUME_VALUE;
    } else if (volume > MAX_VOLUME_VALUE) {
        volume = MAX_VOLUME_VALUE;
    }
    if (volume == __this->volume) {
        return -1;
    }
    __this->volume = volume;

    req.dec.cmd    = AUDIO_DEC_SET_VOLUME;
    req.dec.volume = volume;
    server_request(__this->dec_server, AUDIO_REQ_DEC, &req);

    syscfg_write(CFG_MUSIC_VOL, &__this->volume, sizeof(__this->volume));

    if (__this->ai_server) {
        ai.evt.event   = AI_EVENT_VOLUME_CHANGE;
        ai.evt.arg     = __this->volume;
        ai.evt.ai_name = __this->ai_name;
        return ai_server_request(__this->ai_server, AI_REQ_EVENT, &ai);
    }
    return 0;
}

int watch_net_music_set_volume(int vol)
{
    if (vol < MIN_VOLUME_VALUE) {
        vol = MIN_VOLUME_VALUE;
    } else if (vol > MAX_VOLUME_VALUE) {
        vol = MAX_VOLUME_VALUE;
    }
    if (!__this->dec_server) {
        __this->volume = vol;
        syscfg_write(CFG_MUSIC_VOL, &__this->volume, sizeof(__this->volume));
        return 0;
    }
    return net_music_set_dec_volume(vol - __this->volume);
}

static int net_music_dec_seek(int seek_step)
{
    union audio_req r = {0};

    if (seek_step == 0) {
        return 0;
    }
    if (__this->total_time != 0 && __this->total_time != -1) {
        if (__this->play_time + seek_step <= 0 ||
            __this->play_time + seek_step >= __this->total_time) {
            return -1;
        }
    }
    if (seek_step > 0) {
        r.dec.cmd = AUDIO_DEC_FF;
        r.dec.ff_fr_step = seek_step;
    } else {
        r.dec.cmd = AUDIO_DEC_FR;
        r.dec.ff_fr_step = -seek_step;
    }
    return server_request(__this->dec_server, AUDIO_REQ_DEC, &r);
}

static void dec_server_event_handler(void *priv, int argc, int *argv)
{
    (void)priv;
    (void)argc;
    switch (argv[0]) {
    case AUDIO_SERVER_EVENT_ERR:
    case AUDIO_SERVER_EVENT_END:
        net_music_dec_end();
        break;
    case AUDIO_SERVER_EVENT_CURR_TIME:
        __this->play_time = argv[1];
        break;
    }
}

static void ai_server_event_handler(void *priv, int argc, int *argv)
{
    (void)priv;
    (void)argc;

    if (!__this->ai_server) {
        if (argv[0] == AI_SERVER_EVENT_URL) {
            free((void *)argv[1]);
        }
        return;
    }

    switch (argv[0]) {
    case AI_SERVER_EVENT_URL:
    case AI_SERVER_EVENT_URL_TTS:
    case AI_SERVER_EVENT_URL_MEDIA: {
        const char *url = (const char *)argv[1];
        __this->total_time = 0;
        __this->ai_name = (const char *)argv[2];
        free(__this->url);
        __this->url = malloc(strlen(url) + 1);
        if (__this->url) {
            strcpy(__this->url, url);
        }
        printf("[net-music] URL from %s\n", __this->ai_name ? __this->ai_name : "?");
        net_music_dec_file(url);
        break;
    }
    case AI_SERVER_EVENT_CONTINUE:
        if (AUDIO_DEC_PAUSE == net_music_get_dec_status()) {
            net_music_dec_play_pause(0);
        }
        break;
    case AI_SERVER_EVENT_PAUSE:
        if (AUDIO_DEC_START == net_music_get_dec_status()) {
            net_music_dec_play_pause(0);
        }
        break;
    case AI_SERVER_EVENT_STOP:
        net_music_dec_stop();
        break;
    case AI_SERVER_EVENT_SEEK:
        net_music_dec_seek(argv[1] - __this->play_time);
        break;
    case AI_SERVER_EVENT_VOLUME_CHANGE:
        net_music_set_dec_volume(argv[1] - __this->volume);
        break;
    case AI_SERVER_EVENT_SET_PLAY_TIME:
        __this->play_time = argv[1];
        break;
    default:
        break;
    }
}

static int net_music_ai_connect(void)
{
    union ai_req req = {0};

    if (__this->ai_server) {
        return 0;
    }
    __this->ai_server = server_open("ai_server", NULL);
    if (!__this->ai_server) {
        printf("[net-music] ai_server open fail\n");
        return -1;
    }
    server_register_event_handler_to_task(__this->ai_server, NULL,
                                          ai_server_event_handler, "app_core");
    ai_server_request(__this->ai_server, AI_REQ_CONNECT, &req);
    printf("[net-music] DLNA/ai_server connected\n");
    return 0;
}

int net_music_start(void)
{
    if (__this->started) {
        if (lwip_dhcp_bound()) {
            return net_music_ai_connect();
        }
        return 0;
    }

    memset(__this, 0, sizeof(*__this));

    if (syscfg_read(CFG_MUSIC_VOL, &__this->volume, sizeof(__this->volume)) < 0 ||
        __this->volume < MIN_VOLUME_VALUE || __this->volume > MAX_VOLUME_VALUE) {
        __this->volume = INIT_VOLUME_VALUE;
    }
    __this->ai_name = "unknown";

    __this->dec_server = server_open("audio_server", "dec");
    if (!__this->dec_server) {
        printf("[net-music] audio_server open fail\n");
        return -1;
    }
    server_register_event_handler_to_task(__this->dec_server, NULL,
                                          dec_server_event_handler, "app_core");
    __this->started = 1;
    printf("[net-music] started\n");

    if (lwip_dhcp_bound()) {
        return net_music_ai_connect();
    }
    return 0;
}

void net_music_stop(void)
{
    if (!__this->started) {
        return;
    }
    net_music_dec_stop();
    if (__this->dec_server) {
        server_close(__this->dec_server);
        __this->dec_server = NULL;
    }
    if (__this->ai_server) {
        server_close(__this->ai_server);
        __this->ai_server = NULL;
    }
    free(__this->url);
    __this->url = NULL;
    __this->started = 0;
}

int net_music_on_net_event(struct net_event *event)
{
    if (!event) {
        return 0;
    }
    switch (event->event) {
    case NET_EVENT_CONNECTED:
        net_music_start();
        break;
    case NET_EVENT_DISCONNECTED:
        net_music_dec_stop();
        break;
    default:
        break;
    }
    return 0;
}

#endif /* CONFIG_NET_MUSIC_MODE_ENABLE */
