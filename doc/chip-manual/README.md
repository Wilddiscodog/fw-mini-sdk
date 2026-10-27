# AC7912BB（WL82 / AC791N）芯片参考手册

依据官方 SDK（fw-AC79_AIoT_SDK 原始导出）头文件逆向整理的 HTML 版芯片参考手册。
寄存器偏移/基址均可复核：`include_lib/driver/cpu/wl82/asm/WL82.h`（本仓库
`D:\fw-AC79_AIoT_SDK`）；标注【实测】的条目来自 fw-mini-sdk 移植与闭源库
反汇编验证（USB EP DMA 桥、EMI 写语义、UART putbyte、putbyte/AEC 等）。

## 阅读

直接双击 `index.html`（纯静态页面，无外部依赖），或：

```
cd doc/chip-manual
python -m http.server 8931
# 浏览器打开 http://127.0.0.1:8931/
```

## 章节目录（20 页）

| 文件 | 内容 |
|---|---|
| index.html | 芯片总览、三总线外设地址映射、启动/固件布局 |
| cpu-mem.html | 双核 pi32v2、SDRAM/RAM0/Cache RAM、异常调试单元 |
| clock.html | 时钟树（PLL/PLL3）、isd_config 时序、低功耗 |
| gpio.html | PORT A~H 寄存器、IOMAP 输入输出映射、唤醒 |
| irq.html | 66 个中断源总表、request_irq 绑核 |
| uart.html | UART0-2、环形 DMA、putbyte 反汇编还原 |
| spi.html | SPI0-3、SFC 引脚组、DMA |
| iic.html | 硬件 I²C + 软件 I²C |
| timer.html | TIMER0-5、PCNT/GPCNT |
| mcpwm.html | 8 通道 PWM、死区、两位小数占空比 |
| adc.html | LADC 4 差分 MIC + 4 AUX、adkey 实测 |
| audio.html | 音频子系统（采集→SRC/EQ/ANC→DAC）、audio_server、AEC 回采 |
| usb.html | FUSB1.1+HUSB2.0、EP RAM DMA 桥（0x16800）、EP3/4 大缓冲【实测逆向】 |
| sdmmc.html | SD0/SD1、检卡、离线升级联动 |
| sfc-sec.html | SFC XIP、SFCENC、VM 布局、AES/SHA/CRC/RAND/EFUSE |
| display.html | EMI 8080（写语义实测逆向）、IMD RGB、PAP、LCD 框架 |
| camera.html | 双 DVP ISC、ISP、JPEG 编解码、2D 加速 |
| power.html | 电源域、P33 软关机、充电、RTC、WDT |
| wifi-bt.html | 片内 WiFi4+BT/BLE：校准表、库任务、事件路由 |
| misc.html | WDT/RDEC/IR/CTMU/呼吸灯/FFT/烧录模式速览 |

## 重新生成

页面由 Python 生成器产出（改内容请编辑生成器后重跑，勿直接改 HTML）：

```
cd doc/chip-manual/build
python build.py
```

- `gen_core.py` —— 页面模板 / 表格组件 / SVG 绘图 DSL（风格移植自 QQ-SDK AC6956C 手册）
- `regs_wl82.py` —— **自动解析 WL82.h**：81 个外设基址 + 57 个寄存器结构体
  （偏移=成员序×4；实例→typedef 别名表内置），保证寄存器表与官方头文件一致
- `pages_core.py` —— 总览 / CPU 与存储 / 时钟 / GPIO / 中断
- `pages_periph1.py` —— UART / SPI / IIC / TIMER / MCPWM
- `pages_periph2.py` —— ADC / 音频 / USB / SDMMC
- `pages_sys.py` —— Flash 加密 / 显示 / 摄像头 / 电源 / WiFi-BT / 其他

## 准确性说明

- 寄存器偏移来自 `WL82.h` 结构体成员顺序（每成员 4 字节）；
  基址 = 段基址 + (64×grp + adr)×4（LSB=0x10000 / HSB=0x40000 / PORT=0x50000）。
- 位定义仅收录 SDK 头文件明确给出的部分；未公开位域的模块（ANA/MCPWM/WL 等）
  以 SDK API 层面描述为准，页面中有标注。
- 【实测】条目来自 fw-mini-sdk 上板/反汇编验证，含本机（xiaolu-R2 / AC7912BB）
  经验提示，换板时注意甄别。
