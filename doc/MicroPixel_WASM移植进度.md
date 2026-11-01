# MicroPixel WASM 运行时移植进度(wasm-test 分支)

> 目标:不换主控(杰理 AC7912BB/WL82),在 fw-mini-sdk 内实现 MicroPixel 兼容宿主,
> 让 PC 上用 WASI SDK 编译的 WASM 应用跑在本设备上。
> 配套改动:micropixel 仓库(Wilddiscodog/xch1-game fork)小幅扩展 bundle 格式。
> 分支:`wasm-test`(基于 lcd_demo)。

## 总体架构

```
PC:  WASI SDK(C++23)→ Guest App(.wasm)→ build_app_bundle.py --wasm → .mpxbundle
WL82: app/mpx 宿主 = WAMR 2.4.3 classic 解释器(非 LTO)
      + bundle 加载器 + 7 个 ABI natives + 7 个服务
      + Graphics(DirectSurface + 'MPRS' 光栅)+ Input(CST816 事件桥)
```

## 已完成(真机验证)

### 1. WAMR 解释器移植(third/wamr + 平台层)
- 源自 micropixel 仓库 pinned fork(WAMR 2.4.3),拷贝 core/ 子集
- 新写 `core/shared/platform/jl82/` 平台层(线程/互斥/条件变量接杰理 os_api,
  mmap 走 malloc,BUILD_TARGET_ARC 扁平软浮点打包 + invokeNative_general.c)
- 编译配置:独立 -O0/-O1 非 LTO(见"已知问题"),`MINI_WASM_SUPPORT=1` 开关
- 修复 3 个移植坑:杰理 `bool` typedef 与 stdbool 顺序、杰理 offsetof 非常量表达式、
  与闭源库重名(os_malloc/os_sem_post,直接复用杰理实现)

### 2. 宿主本体(app/mpx/)
- `mpx_bundle.c`:App Bundle 加载器(WASM 段校验,FNV/对齐/无重叠规则对齐上游 reader)
- `mpx_host.c`:micropixel 模块 7 个 natives(签名串与 ESP 宿主一致)
- `mpx_services.c`:ServiceRegistry + System/Timer/Storage/Random/Devices/Graphics/Input
  七服务(Timer 10ms sys_timer 驱动,Storage 为 RAM KV,Graphics 广告 CAP_RASTER)
- `mpx_display.c`:DirectSurface 缓冲(SDRAM,面板字节序)+ present 直走
  `lcd_lvgl_full`(EMI 8 位并口,自带 TE 同步)
- `mpx_raster.c`:'MPRS' 光栅执行器(两遍式:整表校验→逐条执行;
  RECT 含 opacity 混合、SPRITE 缩放/透明/纯色/加色;纹理/调色板槽位上传)
- `mpx_libc_shim.c`:对齐安全 memcpy/memmove/memset + 逐字节 vsnprintf
  (杰理 libc 对奇地址做字宽访问会总线错误,必须覆盖)
- 触摸桥:CST816 事件回调 → TOUCH_DOWN/MOVE/UP 事件
- `third/micropixel-abi/`:micropixel_abi.h + bundle_format.h vendor
  (pi32v2 uint64_t 按 4 对齐,已补丁强制 8 对齐对齐线上格式)

### 3. micropixel 仓库增量(可 upstream)
- `bundle_format.h`:新增 `SECTION_WASM=5`、`FORMAT_WASM_BYTECODE=12`、
  `AOT_TARGET_MASK_WASM_INTERP`
- `bundle_reader.c`:TOC 校验接受 WASM 段(AOT/WASM 可共存双目标 bundle)
- `tools/build_app_bundle.py`:`--wasm` 输入(已实测打出合法 bundle)
- ESP 宿主行为不变(wasm-only 包打开时报缺 AOT)

### 4. 工程级 bug 修复(影响整个 fw-mini-sdk)
1. **链接脚本孤儿段**(sdk_ld_sdram.c):`.data.*/.bss.*` 段此前仅在
   CONFIG_CXX_SUPPORT 时收集,`-fdata-sections` 产生的静态数据全部成为孤儿段
   —— 落在堆区、boot 不清零、运行被 malloc 踩。已无条件收集 + SUBALIGN(4)。
2. **make 不重链**:链接规则补上 `sdk.ld` 依赖(改链接脚本后重编仍打包旧 ELF)。
3. **pi32v2 非对齐字访问**:rodata/bss 内 char 数组按 1 对齐摆放 →
   WAMR 内部字宽 memset/vsnprintf %.*s 拷贝即总线错误。
   修复 = SUBALIGN(4) + 内嵌 wasm 数组/错误缓冲显式 aligned(4) + libc shim。

### 5. 网络 OTA 自动化(tools/mpx/)
- `ota_ipwatch.sh`:盯 COM19 串口 → 检测新开机 → 等 DHCP 拿 IP →
  窗口内重锤 POST /upload → 确认 OK。10+ 次成功推送。
- 配套:mpx_task 开机延迟 20s 执行(给 OTA 留窗口)+ 20s 节点检测
  `net_update_request()`,升级进行中测试自动让路(不会打断烧写)。
- 真机验证:流水线 runtime init → wasm load → instantiate → function lookup
  全部通过(纯计算模块加载验证)。

## 当前阻塞点(诚实交底)

**杰理 clang 4.0.1(pi32v2 后端)误编译解释器核心**:
- 手编 20 条指令的阶乘模块(已排除缓冲对齐/分配器/栈大小等全部环境因素)
- -Oz+LTO:野跳到恒定地址 0x1E00000(computed goto 标签地址表重定位缺陷,
  15 处分发表项烤成垃圾值)
- -O0/-O1 + switch 分派(WASM_ENABLE_LABELS_AS_VALUES=0):解释器入口
  `wasm_interp_call_wasm` 收到垃圾 module_inst 指针(0x4001B08 = 解释器自身
  .data 表区,与真实堆上实例无关),挂死不崩溃
- 256KB 任务栈 / 两种分配器 / libc shim 均不影响 —— 调用约定或寄存器分配
  在大型新代码上出了问题,属于工具链代码生成缺陷,远程调试无法再深入

**已排除**:非对齐访问(SUBALIGN+shim)、栈溢出(64KB→256KB 无效)、
分配器(EMS/JieLi malloc 双验证)、链接孤儿段、跳转表(-fno-jump-tables)。

## 待完成

1. **解决工具链问题**(二选一):
   - 换更新的杰理工具链重编(推荐,固件代码已全部就绪)
   - 有杰理调试器/simulator 时本地单步 `wasm_interp_call_wasm` 入口,
     确认参数错乱机制
2. **SMOKE PASS 真机确认**(工具链解决后即通,纯计算模块已就绪:
   `app/mpx/hello_nocall.h`)
3. **M3 真机联调**:光栅命令(RECT/SPRITE)+ 触摸事件桥端到端
4. **M4**:
   - PC 装 WASI SDK 33,用 MicroPixel Guest SDK 写真实应用
   - 平衡球游戏 WASM 化(触摸驱动,后续接 SC7U22 Sensors 服务)
   - Storage 从 RAM KV 换杰理 VM 区(32KB,OTA 持久)
   - clock_now 换硬件 µs 定时器(动画需要,当前 10ms tick)
   - Bundle 加载走 flash 资源区(当前内嵌 C 数组)+ USB 拖入安装
5. **CI/文档**:tools/ci 接入 wasm-test 构建;本文档随进展更新

## 关键文件清单

| 位置 | 内容 |
|---|---|
| third/wamr/ | WAMR 2.4.3 core + jl82 平台层(shared/platform/jl82/) |
| third/micropixel-abi/ | micropixel_abi.h(已打 u64 对齐补丁)+ bundle_format.h |
| app/mpx/ | 宿主本体(host/bundle/services/display/raster/libc_shim + 测试模块) |
| tools/mpx/ | hello_wasm 生成器、bundle_to_header、OTA 脚本、串口监控 |
| app/board/wl82/Makefile | MINI_WASM_SUPPORT 开关 + WAMR 编译配置 |
| cpu/wl82/sdk_ld_sdram.c | 链接脚本孤儿段修复 |

## 调试快速手册

- 串口:COM19(CH340),**1Mbaud**(杰理 UART0 调试口;115200 只在部分状态可读)
- CDC 日志:COM44/COM45(双 CDC,device 侧 userlog 在 CDC2)
- OTA:POST http://192.168.5.94/upload,multipart 字段名 fw,
  文件 = cpu/wl82/tools/update-ota.ufw(勿加 Expect: 头)
- 构建:`cd app\board\wl82 && make.bat MINI_WASM_SUPPORT=1`(改 DEFINES 后
  需 `rm -rf objs/third/wamr objs/app/mpx` 强制重编)
- 异常定位:`tools/mpx/map_lookup.py <addr>`(sdk.map 符号反查)
