/*
 * 游戏 BGM —— FC 风清晰旋律合成
 * 主声道: 50% 方波旋律(音与音之间留空, 不糊)
 * 低音: 三角波只跟根音, 音量压低
 * 不做超快琶音叠八度(那是发糊的主因)
 */
#include "app_config.h"
#include "system/includes.h"
#include "watch_game_bgm.h"

#if (defined MINI_AUDIO_SUPPORT) && MINI_AUDIO_SUPPORT
#include "server/audio_server.h"
#include "server/server_core.h"
#include "device/gpio.h"
#include "syscfg/syscfg_id.h"
#include <string.h>

#define BGM_SR          22050
#define BGM_CH          1
#define BGM_CACHE       (4 * 1024)
#define BGM_PA_IO       IO_PORTH_07
#define BGM_PA_MUTE_LVL 0
#define BGM_VOL_CFG_ID  56   /* 用户区 1~59, 55=平衡球排行 */

/* freq * 2^32 / 22050 ≈ freq * 194392 */
#define HZ_STEP(hz)     ((u32)(hz) * 194392u)

/* C3..C6 */
static const u16 NOTE_HZ[37] = {
    131, 139, 147, 156, 165, 175, 185, 196, 208, 220, 233, 247, /* 0..11  C3 */
    262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494, /* 12..23 C4 */
    523, 554, 587, 622, 659, 698, 740, 784, 831, 880, 932, 988, /* 24..35 C5 */
    1047
};

#define N_REST   0xFE
#define N_END    0xFF
/* 一拍采样数 @22050 ≈ 140ms */
#define BEAT     3080

/*
 * 旋律: (note, beats) 交替; note=半音index(相对C3), N_REST=休止
 * 每套约 40~50 拍(~6–7s) 再循环, 结构 A-B-A' 避免太短发腻
 */
static const u8 MEL_SOFT[] = {
    /* A */
    12, 1, 16, 1, 19, 1, 16, 1,
    17, 1, 21, 1, 24, 1, 21, 1,
    19, 1, 23, 1, 26, 1, 23, 1,
    12, 1, 16, 1, 19, 1, 24, 1,
    /* B */
    24, 1, 23, 1, 21, 1, 19, 1,
    17, 1, 16, 1, 14, 1, 12, 1,
    14, 1, 16, 1, 17, 1, 19, 1,
    21, 2, 19, 1, N_REST, 1,
    /* A' 高八度点缀 */
    12, 1, 16, 1, 19, 1, 24, 1,
    26, 1, 24, 1, 23, 1, 21, 1,
    19, 1, 17, 1, 16, 1, 14, 1,
    12, 2, 19, 1, 24, 1,
    N_REST, 2,
    N_END
};
static const u8 MEL_ACTIVE[] = {
    /* A 跳跃 */
    12, 1, 12, 1, 19, 1, 19, 1,
    21, 1, 19, 1, 17, 1, 16, 1,
    14, 1, 16, 1, 17, 1, 19, 1,
    12, 2, 19, 2,
    /* B 上行 */
    12, 1, 14, 1, 16, 1, 17, 1,
    19, 1, 21, 1, 23, 1, 24, 1,
    26, 1, 24, 1, 23, 1, 21, 1,
    19, 2, N_REST, 1, 19, 1,
    /* A' 加花 */
    12, 1, 16, 1, 19, 1, 24, 1,
    21, 1, 19, 1, 17, 1, 16, 1,
    14, 1, 12, 1, 14, 1, 16, 1,
    17, 1, 19, 1, 12, 2,
    N_REST, 2,
    N_END
};
static const u8 MEL_CANDY[] = {
    /* A */
    17, 1, 19, 1, 21, 1, 24, 1,
    21, 1, 19, 1, 17, 2,
    16, 1, 17, 1, 19, 1, 21, 1,
    19, 1, 17, 1, 16, 2,
    /* B */
    24, 1, 21, 1, 19, 1, 17, 1,
    16, 1, 14, 1, 12, 2,
    14, 1, 16, 1, 17, 1, 19, 1,
    21, 1, 24, 1, 21, 2,
    /* A' */
    17, 1, 21, 1, 24, 1, 26, 1,
    24, 1, 21, 1, 19, 1, 17, 1,
    16, 1, 17, 1, 19, 1, 21, 1,
    24, 2, 17, 1, N_REST, 1,
    N_REST, 2,
    N_END
};

struct bgm {
    struct server *dec;
    volatile u8 run;
    u8 theme;
    u8 dac_vol;          /* 送给 audio_server 的 0~100 */

    const u8 *mel;
    u16 mel_i;
    u16 note_left;       /* 当前音符剩余采样 */
    u16 gap_left;        /* 音尾空隙, 防糊 */

    u8 note;             /* 当前旋律半音 */
    u8 bass_note;
    u8 gate;             /* 1=发音中 */

    u32 ph_pulse;
    u32 ph_tri;
    u32 step_pulse;
    u32 step_tri;
    u8 env;              /* 0..15 */
};

static struct bgm B;
static u8 s_bgm_vol = WATCH_BGM_VOL_DEFAULT;

extern void bt_music_audio_takeover(void);

static void pa_unmute(u8 on)
{
    gpio_direction_output(BGM_PA_IO, on ? !BGM_PA_MUTE_LVL : BGM_PA_MUTE_LVL);
}

static u8 clamp_note(int n)
{
    if (n < 0) {
        return 0;
    }
    if (n > 36) {
        return 36;
    }
    return (u8)n;
}

static s16 pulse50(u32 phase, s16 amp)
{
    return (phase < 0x80000000u) ? amp : (s16)(-amp);
}

static s16 tri32(u32 phase, s16 amp)
{
    u8 step = (u8)(phase >> 27);
    s16 v = (step < 16) ? (s16)(15 - step) : (s16)(step - 16);
    return (s16)(((s32)(v - 8) * amp) / 8);
}

static void apply_dac_vol(void)
{
    union audio_req req = {0};

    /* 设置滑条 0~100 → DAC 音量; 略压一点避免方波刺耳 */
    B.dac_vol = (u8)((u16)s_bgm_vol * 70 / 100);
    if (s_bgm_vol && B.dac_vol < 8) {
        B.dac_vol = 8;
    }
    if (!B.dec || !B.run) {
        return;
    }
    req.dec.cmd = AUDIO_DEC_SET_VOLUME;
    req.dec.volume = B.dac_vol;
    server_request(B.dec, AUDIO_REQ_DEC, &req);
}

static void start_note(u8 n, u16 beats)
{
    u16 total = (u16)(beats * BEAT);
    /* 85% 发音 + 15% 空隙 → 颗粒清晰不糊 */
    u16 sound = (u16)((total * 85) / 100);
    u16 gap = (u16)(total - sound);

    if (n == N_REST) {
        B.gate = 0;
        B.note_left = total;
        B.gap_left = 0;
        B.env = 0;
        B.step_pulse = 0;
        B.step_tri = 0;
        return;
    }
    B.note = clamp_note(n);
    B.bass_note = (B.note >= 12) ? (u8)(B.note - 12) : B.note;
    B.step_pulse = HZ_STEP(NOTE_HZ[B.note]);
    B.step_tri = HZ_STEP(NOTE_HZ[B.bass_note]);
    B.gate = 1;
    B.env = 14;
    B.note_left = sound;
    B.gap_left = gap;
    B.ph_pulse = 0;
}

static void advance_mel(void)
{
    u8 n, beats;

    if (!B.mel) {
        return;
    }
    if (B.mel[B.mel_i] == N_END) {
        B.mel_i = 0;
    }
    n = B.mel[B.mel_i++];
    beats = B.mel[B.mel_i++];
    if (!beats) {
        beats = 1;
    }
    start_note(n, beats);
}

static void synth_fill(s16 *out, u32 n)
{
    u32 i;
    s32 mix;
    s16 lead, bass;

    for (i = 0; i < n; i++) {
        if (!B.note_left && !B.gap_left) {
            advance_mel();
        }
        if (B.note_left) {
            B.note_left--;
            if ((i & 63) == 0 && B.env > 6) {
                B.env--; /* 轻衰减, 保留力度 */
            }
        } else if (B.gap_left) {
            B.gap_left--;
            B.gate = 0;
            B.env = 0;
        }

        if (!B.gate || !B.env || !s_bgm_vol) {
            out[i] = 0;
            continue;
        }

        B.ph_pulse += B.step_pulse;
        B.ph_tri += B.step_tri;

        /* 主旋律方波清晰; 低音三角很轻, 只垫底不抢 */
        lead = pulse50(B.ph_pulse, (s16)(B.env * 1100));
        bass = tri32(B.ph_tri, (s16)(B.env * 220));
        mix = (s32)lead + (s32)bass;
        if (mix > 26000) {
            mix = 26000;
        }
        if (mix < -26000) {
            mix = -26000;
        }
        out[i] = (s16)mix;
    }
}

static int vfs_fread(void *file, void *data, u32 len)
{
    u32 samples;
    (void)file;
    if (!B.run || !data || len < 2) {
        memset(data, 0, len);
        return (int)len;
    }
    samples = len / 2;
    synth_fill((s16 *)data, samples);
    return (int)(samples * 2);
}

static int vfs_fclose(void *file)
{
    (void)file;
    return 0;
}

static int vfs_flen(void *file)
{
    (void)file;
    return 0x7FFFFFFF;
}

static const struct audio_vfs_ops s_vfs = {
    .fread = vfs_fread,
    .fclose = vfs_fclose,
    .flen = vfs_flen,
};

static void bgm_event(void *priv, int argc, int *argv)
{
    (void)priv;
    (void)argc;
    (void)argv;
}

static const u8 *theme_mel(u8 theme)
{
    switch (theme) {
    case WATCH_BGM_ACTIVE:
        return MEL_ACTIVE;
    case WATCH_BGM_CANDY:
        return MEL_CANDY;
    default:
        return MEL_SOFT;
    }
}

void watch_game_bgm_vol_init(void)
{
    u8 v = 0;
    if (syscfg_read(BGM_VOL_CFG_ID, &v, sizeof(v)) != sizeof(v) ||
        v > WATCH_BGM_VOL_MAX) {
        v = WATCH_BGM_VOL_DEFAULT;
    }
    s_bgm_vol = v;
}

u8 watch_game_bgm_vol_get(void)
{
    return s_bgm_vol;
}

void watch_game_bgm_vol_set(u8 vol)
{
    if (vol > WATCH_BGM_VOL_MAX) {
        vol = WATCH_BGM_VOL_MAX;
    }
    s_bgm_vol = vol;
    apply_dac_vol();
}

void watch_game_bgm_vol_persist(void)
{
    syscfg_write(BGM_VOL_CFG_ID, &s_bgm_vol, sizeof(s_bgm_vol));
}

void watch_game_bgm_stop(void)
{
    union audio_req req = {0};

    B.run = 0;
    if (B.dec) {
        req.dec.cmd = AUDIO_DEC_STOP;
        server_request(B.dec, AUDIO_REQ_DEC, &req);
        server_close(B.dec);
        B.dec = NULL;
    }
    pa_unmute(0);
}

void watch_game_bgm_start(u8 theme)
{
    union audio_req req = {0};

    watch_game_bgm_stop();
    if (!s_bgm_vol) {
        return; /* 音量 0 = 静音不开解码 */
    }

    bt_music_audio_takeover();

    memset(&B, 0, sizeof(B));
    B.theme = theme;
    B.mel = theme_mel(theme);
    B.mel_i = 0;
    advance_mel();
    apply_dac_vol();

    B.dec = server_open("audio_server", "dec");
    if (!B.dec) {
        printf("[bgm] open dec fail\n");
        return;
    }
    server_register_event_handler(B.dec, NULL, bgm_event);

    memset(&req, 0, sizeof(req));
    req.dec.cmd = AUDIO_DEC_OPEN;
    req.dec.volume = B.dac_vol;
    req.dec.output_buf_len = BGM_CACHE;
    req.dec.channel = BGM_CH;
    req.dec.sample_rate = BGM_SR;
    req.dec.vfs_ops = &s_vfs;
    req.dec.dec_type = "pcm";
    req.dec.sample_source = CONFIG_AUDIO_DEC_PLAY_SOURCE;
    if (server_request(B.dec, AUDIO_REQ_DEC, &req)) {
        printf("[bgm] dec open fail\n");
        server_close(B.dec);
        B.dec = NULL;
        return;
    }
    B.run = 1;
    req.dec.cmd = AUDIO_DEC_START;
    server_request(B.dec, AUDIO_REQ_DEC, &req);
    pa_unmute(1);
    printf("[bgm] clear-mel theme=%u vol=%u dac=%u\n", theme, s_bgm_vol, B.dac_vol);
}

u8 watch_game_bgm_is_on(void)
{
    return B.run;
}

#else /* !MINI_AUDIO_SUPPORT */

void watch_game_bgm_vol_init(void)
{
}
u8 watch_game_bgm_vol_get(void)
{
    return WATCH_BGM_VOL_DEFAULT;
}
void watch_game_bgm_vol_set(u8 vol)
{
    (void)vol;
}
void watch_game_bgm_vol_persist(void)
{
}
void watch_game_bgm_start(u8 theme)
{
    (void)theme;
}
void watch_game_bgm_stop(void)
{
}
u8 watch_game_bgm_is_on(void)
{
    return 0;
}

#endif
