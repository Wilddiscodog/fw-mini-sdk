Minimal voice pack for fw-mini-sdk.
Kept:
  poweron.mp3   — placeholder so isd -res audlogo_mini is non-empty
  fall_alert.wav — 摔倒提醒: 「摔倒了，您没事吧」(SAPI zh-CN, 22.05kHz mono PCM)

Runtime path:
  mnt/sdfile/res/audlogo_mini/fall_alert.wav

App plays fall_alert only inside 步数检测 App when 摔倒提醒 is on (no background).
Full historical set remains in ../audlogo/ if present.
