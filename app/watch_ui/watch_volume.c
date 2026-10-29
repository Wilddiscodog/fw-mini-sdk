#include "app_config.h"
#include "system/includes.h"
#include "syscfg/syscfg_id.h"
#include "watch_volume.h"
#include "watch_style.h"

static u8 s_vol = WATCH_VOL_DEFAULT;

void watch_volume_init(void)
{
    u8 v = 0;
    if (syscfg_read(CFG_MUSIC_VOL, &v, sizeof(v)) < 0 ||
        v < WATCH_VOL_MIN || v > WATCH_VOL_MAX) {
        v = WATCH_VOL_DEFAULT;
    }
    s_vol = v;
}

u8 watch_volume_get(void)
{
    return s_vol;
}

void watch_volume_set(u8 vol)
{
    if (vol < WATCH_VOL_MIN) {
        vol = WATCH_VOL_MIN;
    } else if (vol > WATCH_VOL_MAX) {
        vol = WATCH_VOL_MAX;
    }
    s_vol = vol;
    syscfg_write(CFG_MUSIC_VOL, &s_vol, sizeof(s_vol));
#ifdef CONFIG_NET_MUSIC_MODE_ENABLE
    {
        extern int watch_net_music_set_volume(int vol);
        watch_net_music_set_volume(vol);
    }
#endif
}
