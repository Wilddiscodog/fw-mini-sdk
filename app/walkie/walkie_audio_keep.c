#include "app_config.h"
#include "generic/typedef.h"

/*
 * LTO + used-symbol-file 只保住 audio_server 的 subdev_* 壳时,
 * libvad/libdns/lib_dvad 的算法 .text 会被收成 0 (sdk.map 可见),
 * PTT 开麦表现为 audio_set_fmt: err / enc open fail.
 * 这里对叶子符号取址并 used, 强制完整链入.
 */
#if (defined MINI_AUDIO_SUPPORT) && MINI_AUDIO_SUPPORT

#ifdef CONFIG_DNS_ENC_ENABLE
extern const u8 dns_enc_enable;
extern char JLSP_ns_init[];
#endif

#ifdef CONFIG_VAD_ENC_ENABLE
extern char vad_init[];
extern char fvad_new[];
extern char JLSP_vad_init[];
#endif

__attribute__((used))
static const void *const s_walkie_audio_lto_keep[] = {
#ifdef CONFIG_DNS_ENC_ENABLE
    &dns_enc_enable,
    (const void *)JLSP_ns_init,
#endif
#ifdef CONFIG_VAD_ENC_ENABLE
    (const void *)vad_init,
    (const void *)fvad_new,
    (const void *)JLSP_vad_init,
#endif
    0,
};

#endif
