/*
 * 音频库配置符号(lib audio_config.c 同款机制) —— 闭源 audio_server.a/库按名字
 * 引用这些 const 符号, 逐个按链接缺符号补入(避免整文件搬运带入无关配置)。
 * dns_enc_enable: aec 编码路径的降噪开关(CONFIG_DNS_ENC_ENABLE=1 时开)。
 */
#include "app_config.h"
#include "typedef.h"

#if (defined MINI_AUDIO_SUPPORT) && MINI_AUDIO_SUPPORT

#ifdef CONFIG_DNS_ENC_ENABLE
const u8 dns_enc_enable = 1;
#else
const u8 dns_enc_enable = 0;
#endif

/* 库日志标签(.LOG_TAG_CONST 段, 与 lib_update_config.c 同机制) */
const char log_tag_const_v_AUDIO_ENCODER AT(.LOG_TAG_CONST) = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_AUDIO_ENCODER AT(.LOG_TAG_CONST) = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_d_AUDIO_ENCODER AT(.LOG_TAG_CONST) = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_w_AUDIO_ENCODER AT(.LOG_TAG_CONST) = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_e_AUDIO_ENCODER AT(.LOG_TAG_CONST) = CONFIG_DEBUG_LIB(TRUE);

#endif  /* MINI_AUDIO_SUPPORT */
