# SLOOP（sloop-fm1）固件移植到 Cardputer 的可行性评估

> 对象：[isod89/sloop-fm1](https://github.com/isod89/sloop-fm1) v2.8.5 —— M-VAVE FM-1 的开源 groovebox 固件（GPL-3.0，源自 Felucca / Hügelton Instruments）
> 目标硬件：M5Stack Cardputer-Adv（ESP32-S3FN8，2× LX7 @ 240 MHz，512 KB SRAM，8 MB Flash，**无 PSRAM**，ES8311 单声道 codec，240×135 ST7789V2，56 键 QWERTY，TCA8418）
> 评估日期：2026-11-10
> 评估方法：读代码 + **实际交叉编译验证**（用 PlatformIO 的 `xtensa-esp32s3-elf-gcc` 把 SLOOP 的 DSP/音序器核心编成 ESP32-S3 目标文件并链接量测，见 §5）。证据都标了文件与行号，§9 列出全部待实测项。

---

## 0. 结论

| 问题 | 回答 |
|---|---|
| 能不能把 SLOOP "原样"移植到 Cardputer-Adv？ | **不能。** 不是"改改 HAL 就行"：静态 RAM 账本本身就超出 512 KB（实测核心段 308 KB，还没算屏幕 sprite、USB、FreeRTOS、DMA），而屏幕高度只有 135 px（SLOOP 按 240×240 设计）、没有旋钮、没有 27 键琴键区、没有立体声、没有 PSRAM |
| 能不能做"精简版 SLOOP"（保留引擎 + 音序器，重写 UI）？ | **能，但等于重做一个产品**：要先砍掉约 1/3 的音频特性才装得下内存；UI 8.5k 行几乎全部重写；预估 250–400 h |
| 最省事、风险最低的做法 | **不移植。** 让 Cardputer 继续当 USB-MIDI Host/音序器驱动 FM-1（你仓库里的 `MidiLooper` v1.2.0 已经做到），或反过来用 PC 端 SLOOP 的 Web Editor。60–120 h 就能到可用状态 |
| 难度（完整移植） | **9 / 10**（不是 5–6 分档的项目） |
| 工作量（完整可玩移植） | **600–1000 h**（1 人全职 4–6 个月），其中 UI 与输入重构占一半以上 |
| 最大瓶颈排序 | **内存（硬约束）> 屏幕/输入形态（产品约束）> 立体声与 USB 角色（硬件约束，§3.5）> CPU（可测、大概率够）> 工具链与 HAL（体力活）** |
| 一句话 | **SLOOP 不是"某个芯片上的合成器"，而是"一台 240×240 屏幕 + 27 键 + 14 个功能键 + 7 个旋钮 + 立体声编解码 + 双核 470 KB 内存"的专用机器；把它搬到 Cardputer 上，剩下的那部分才叫 SLOOP。** |

---

## 1. SLOOP 到底是什么（先对齐认知）

来源：`README.md:18-20, 499-513`；`BUILDING.md`；`firmware/app.ld`。

| 项 | SLOOP / FM-1 | 来源 |
|---|---|---|
| SoC | JieLi AC79 系列（pi32v2 双核，clang 定制工具链） | `BUILDING.md:19-32`、`firmware/hal/*` |
| RAM | 96 KB RAM 区 + **344 KB .pool** + 29 KB 低区（实测已用到 98,304 中 58,220 等） | `firmware/app.ld:14-21`、`RAM.md:9-17` |
| Flash | XIP 窗口 `0x8DFBC` ≈ **581 KB**，程序装满（2.7 起"flash is full"） | `firmware/app.ld:16`、`README.md:346` |
| 屏幕 | **240×240** ST7789 类，SPI1 12 MHz；UI 分 20 px 头 + **124 px 图形区** + 38 px 脚 | `firmware/src/lcd.c:1-11`、`firmware/src/ui.c:37-46`、`gfx.c:30` |
| 输入 | **27 个音符键 + 14 个功能键 + 7 个编码器**，11×6 二极管矩阵；LED 背光 | `firmware/hal/fm1_input.h:1-16, 61-78` |
| 音频 | 44.1 kHz，ALNK0 I2S → 外置 codec，**立体声** 24-bit；`HALF_FRAMES 256`（5.8 ms/半缓冲） | `hal/fm1_audio.h:1-20`、`src/core.h:16`、`src/audio.c:7` |
| 引擎 | 12 个合成引擎（ANALOG / DIGITAL / PHASE / LOFI / SAMPLE / FORMANT / TRIO / DRAWBAR / GRAIN / FM6 / PHYS / NOISE），153 个预设，37 套鼓，8 声部共享 | `README.md:501-504` |
| 音序器 | 4 轨 × 64 步，参数锁、微时序、fill、swing、录音/叠录，采样精确时钟 | `README.md:505-506` |
| 效果 | 27 个 punch-in + 每轨 INSERT + 立体声 chorus / tempo delay / 立体声 reverb + limiter | `README.md:508`、`fx.c:5-17` |
| USB | **设备侧**：USB-MIDI + **USB Audio（UAC，44.1/48 kHz 立体声）+ CDC 控制台**，浏览器直连刷机 | `usb.c`、`README.md:510-513` |
| 代码量 | **24.4k 行** C（去空行/注释）：UI 8.5k、DSP 7.9k、音序器/工程/存储 5.2k、USB/MIDI/OTA 2.0k、启动 0.8k；另 HAL 1.4k（含 .S） | §5.1 实测 |

**关键架构特征（这是好消息）**：`firmware/src/felucca.c` 是**单编译单元**，HAL 是**纯头文件**（`#include` 顺序见 `felucca.c:4-15`）；`tests/hostsim.c` 和 `firmware/host/preview.c` 就是"换一套 HAL 把 DSP+音序器跑在 PC / WebAssembly 上"的**现成范例**（`preview.c:9-34`）。所以"SLOOP 的算法层是可移植的"，而"硬件层必须重写"。

---

## 2. Cardputer-Adv 的硬指标与缺口

来源：本仓库已有的 `Cardputer_MIDI/可行性评估.md:4,54-57`、`Cardputer_MPC/docs/可行性评估.md:32-39`、`Cardputer_MusicTools/README.md:313`。

| 项 | Cardputer-Adv | 对 SLOOP 的直接后果 |
|---|---|---|
| CPU | ESP32-S3，2× LX7 @ 240 MHz，单精度 FPU，无 DSP 扩展 | SLOOP 是定点 DSP，不依赖 FPU，**不排斥**；每 MHz 吞吐待实测（§3.3） |
| SRAM | **512 KB，无 PSRAM** | SLOOP 静态占用 ≈ 308 KB + sprite 65 KB + USB/RTOS/DMA ≈ **超预算**（§3.2） |
| Flash | 8 MB | **真·优势**：SLOOP 在 AC79 上程序已经装不下，8 MB 完全没有这个问题 |
| 屏幕 | 240×135 ST7789V2 | SLOOP 的图形区是 **240×124**，全屏内容 240×240；135 px 高度**每屏少 105 px**，所有页面要重新设计 |
| 输入 | 56 键 QWERTY + G0 + 触摸板 + IMU | **没有旋钮**（SLOOP 有 7 个）、没有 27 键琴键区、没有 14 个可长按的功能键；SLOOP 的整个"按住层键 + 白键 + 旋钮"交互（`ui_layers.c`）要重新发明 |
| 音频 | ES8311 **单声道** DAC + 1 W；无 line-in | SLOOP 的立体声 chorus/reverb/delay、pan、UAC 立体声输出**全部降级或作废** |
| 麦克风 | ES8311 ADC 可全双工 | SLOOP 不用它；可作为"采样输入"差强人意的替代（`Cardputer_MPC` 已做 2 s 录音） |
| USB | 只有一个 USB-C，**Host 或 Device 二选一** | SLOOP 的"USB MIDI + USB Audio + 浏览器刷机"三合一无法同时成立；接 FM-1 就不能接 PC，反之亦然 |
| 存储 | microSD（1-bit）+ 8 MB Flash | 比 AC79 的闪存 XIP 窗口宽松得多，但 SD 读延迟在音频回调里是硬伤（只能后台预取） |

---

## 3. 五个决定性障碍（按优先级）

### 3.1 工具链与 HAL：不是"改几个寄存器"，而是重写底层 1.4k 行 + 引导

实测（§5.2）：SLOOP 的 DSP/音序器核心**可以**用 `xtensa-esp32s3-elf-gcc` 编过，但必须**把 HAL 完全替换成桩**才行——和 `preview.c` 一样的做法：

| SLOOP 原语 | ESP32-S3 映射 | 难度 | 备注 |
|---|---|---|---|
| `fm1_irq_off/on`（`seq.c`、`fx.c`、`project.c` 等 30 处） | 音频任务内 `taskENTER_CRITICAL`；ISR 内 `portENTER_CRITICAL_ISR` | 低 | 语义从"关中断"变成"关调度"，临界区不能无限长 |
| `fm1_audio_*`（ALNK0 DMA 半缓冲） | I2S 标准模式双缓冲 + `onReceive/onSend` 回调 | 中 | 256 帧/半、5.8 ms 的块结构可以照搬；ES8311 初始化 `Cardputer_MIDI/src/audio.cpp` 已验证 |
| `fm1_ticks()` / TIMER4/5 | `esp_timer` / `esp_cpu_get_cycle_count` | 低 | 直方图式计时可复用 |
| `fm1_flash.h`（XIP 读、4 KB 擦、页写、JEDEC ID） | ESP32-S3：Flash 用 `esp_partition`；**用户样本/工程改放 SD 或小分区** | 中 | 关键：ESP32-S3 擦除 Flash 会**关 cache**，音频渲染代码必须 `IRAM_ATTR`，否则一写盘就欠载 |
| `fm1_usb.h`（SIE 寄存器直驱，1300+ 行 `usb.c`） | ESP-IDF TinyUSB：`tusb_midi` + `tusb_audio`（UAC） | **高** | `usb.c` 里全是自有端点和描述符逻辑（`usb.c:960-1020`），需要重写设备描述符与回调层；UAC 在 ESP-IDF 里支持有限（多为 UAC1 麦克风示例） |
| `fm1_input.h`（矩阵扫描 + 编码器 + LED） | TCA8418 中断 + 键盘扫描；**编码器不存在** | **高** | 不是映射问题，是产品重设计（§3.4） |
| `fm1_lcd_hw.h` + `lcd.c` | M5GFX/SPI 直接驱动 ST7789V2，240×135 | 中 | `lcd_window/lcd_blit` 接口可保留，坐标与页面要重排 |
| `main.c` 的 `fm1_cstart` / 向量表 / `crt0.S` / `fm1_guard` / `fm1_sys`（供电、看门狗、复位原因） | ESP-IDF 启动流程 | 中改高 | 大量"防止第二核乱跑"的代码（`DUALCORE.md:75-80`）在 ESP32-S3 上语义不同，可直接删，但**不能只删**——双核渲染的分工要重新做 |

**结论**：底层不是"适配"，是**重写一份 ESP32-S3 HAL + 删除 JieLi 引导体系**。这部分工程量约 150–250 h，且没有一行能直接从 AC79 搬。

### 3.2 内存：**决定性硬约束**（实测账本，超出 Cardputer-Adv 预算）

用 `-Os` 把 SLOOP 的 DSP+音序器核心（`core.h`/`engines.c`/`drums.c`/`params.c`/`voice.c`/`slicer.c`/`fx.c`/`usb.c`/`midi_uart.c`/`seq.c`，即 `preview.c` 的同款集合）编成 ESP32-S3 目标文件并链接（§5.2）：

| 段 | 内容 | 大小 | 归属 |
|---|---|---:|---|
| `.text` | 全部算法代码 | **84.8 KB** | Flash |
| `.rodata` | 153 预设参数、32 套鼓合成参数、8 套 CC0 采样 ADPCM、FM6 表 | **328.9 KB** | Flash（`SMP_DATA` 一项就 283 KB） |
| `.data` + `.bss` | 全局状态、`trk[4]`、`ins`、`drums` | **26.7 KB** | **RAM** |
| `.pool` | `dly_buf` 128 KB、`punch_ring` 64 KB、`sl_buf` 32 KB、`eng_arena` 25.7 KB、`cv_px` 59.5 KB… | **261.2 KB** | **RAM** |
| `.lowram` | `rev_line` 17.3 KB、双核 scratch、核 1 栈 | **19.7 KB** | **RAM** |
| **静态 RAM 合计** | | **≈ 308 KB** | |

（未启用的 `FELUCCA_SLICE` 里还有 4×8 KB，不计。）

对照 Cardputer-Adv 的 512 KB SRAM，还要额外装下（均为现有 Cardputer 项目的实测口径：`Cardputer_MIDI/可行性评估.md:304`、`Cardputer_MPC/docs/可行性评估.md:75`）：

| 新增开销 | 估算 |
|---|---:|
| 240×135×16bpp 全屏 sprite（M5Canvas） | 65 KB |
| ESP-IDF + FreeRTOS + TinyUSB + ES8311 驱动基线 | 60–90 KB |
| I2S DMA 缓冲（4×256 帧立体声）+ 代码 IRAM 副本 | 16–24 KB |
| 双核音频任务的栈、事件队列 | 8–16 KB |
| **合计** | **≈ 460–500 KB**（SRAM 只有 512 KB，且 IDF 启动后可用堆通常只有 380–420 KB） |

**判断：完整 SLOOP 在 Cardputer-Adv 上装不下。** 不是"优化一下就行"，而是必须在下列地方**砍功能**（每一项都要改 `core.h` 与所有引用）：

1. `dly_buf` 128 KB（1.49 s 立体声时延线，`fx.c:5`）：降到 0.5 s 单声道 → −96 KB；
2. `punch_ring` 64 KB（27 个 punch-in FX 的冻结/回放，`punch.c:9`）：只保留 8 个 FX 或砍掉 → −48 KB；
3. `sl_buf` 32 KB（每轨 SLICER 采样，`slicer.c:22`）：砍掉 → −32 KB；
4. `cv_px` 59.5 KB（240×124 画布）：改按脏矩形/分段绘制 → −40 KB 或挪到 PSRAM（本机没有）；
5. 8 套 CC0 采样 + 4 个用户槽：只保留 2 套 + 1 槽 → 省 Flash，不减 RAM。

砍完约 200–220 KB 静态 RAM，**刚好够但毫无余量**；一旦 USB Host（接 FM-1）或者 SD 预取再开，又要加。

> **如果 Cardputer 带 PSRAM（部分 ESP32-S3 变体 / 你确认自己那台有 PSRAM）会怎样？**
> 能挪的只有"冷缓冲"：`dly_buf`（时延线每采样都读写，PSRAM 延迟会直接进音频路径，**不建议**）、`cv_px`（画布，可挪）、`punch_ring`、`sl_buf`（约 155 KB）。挪走画布和采样类缓冲后内部 SRAM 可降到 ≈ 240 KB，`dly_buf` 仍建议留在内部。但 PSRAM 带宽只有内部 SRAM 的几分之一，音频回调里做 128 KB 时延线的随机读写很容易欠载，所以 PSRAM 只能解决"装得下"，解决不了"实时"。**且本仓库已有的三份 Cardputer 评估都明确记的是"无 PSRAM"（`Cardputer_MIDI/可行性评估.md:4`、`Cardputer_MPC/docs/可行性评估.md:33`），本文按此口径评估。**

> 反面参照：你自己的 `Cardputer_MPC` 是 16 声部 + 8 种合成鼓 + 2 s 录音，内存已经"采样池最多约 192 KB"（`Cardputer_MPC/docs/可行性评估.md:60`）。SLOOP 的 DSP 复杂度（12 引擎 × 8 声部 + 27 FX + 立体声总线）是它的 5–10 倍。

### 3.3 CPU：**大概率够，但必须实测**（唯一真正的未知量）

- SLOOP 官方给出的设备实测：4 轨混音（含鼓、时延、混响）**关闭第二核 31%、开启 23%**（`DUALCORE.md:48-57`），而这已经是"双核 240 MHz 级别"的 AC79 上的一大半时间预算（半缓冲 5.8 ms，渲染 ~1.8 ms）。
- ESP32-S3 也是 2× LX7 @ 240 MHz，但**没有 pi32v2 的 DSP 类指令**、cache 更小（32 KB I / 16 KB D），定点乘加指令密度可能不如 AC79；反过来 LX7 有 128-bit SIMD 面（编译器基本不会自动向量化这份代码）。
- 定性判断：**在 2× 误差带内是可行的**，也就是"很可能能跑，但必须第一时间测"。这也是为什么 §7 的第 1 个里程碑就是"在 ESP32-S3 上量一次满配混音的渲染时间"，而不是先写 UI。
- 若实测不够，可回退的手段：音频回调只跑"引擎 + 简单混音"（把 delay/reverb 放到第二核），或把 27 个 punch FX 降级为 8 个。

### 3.4 屏幕与输入：产品层不匹配，UI 8.5k 行基本作废

| SLOOP 依赖 | Cardputer-Adv 上 |
|---|---|
| 240×240，每屏 20+124+38 px 三段 | 240×135，**高度少 44%**；SLOOP 的钢琴卷帘、鼓格 16×64 步、GLO 菜单在 135 px 里放不下 |
| 27 音符键 + 14 功能键 + 7 编码器，全部靠"长按层键 + 白键/旋钮"操作（`ui_layers.c`、`fm1_input.h:61-78`） | 56 键 QWERTY，**没有旋钮**，没有可长按的 14 键；只能用 `Fn+` 组合模拟编码器 |
| LED 背光 / 键位点亮（`fm1_led_*`） | 没有 |

**这是最容易被低估的部分**：`ui.c`（49.2 KB）、`ui_input.c`（58 KB）、`ui_draw.c`（47 KB）、`ui_layers.c`（47.7 KB）、`ui_studio.c`（43 KB）、`ui_vis.c`（29 KB）、`editor*.c`（48 KB）合计 8,482 行，几乎**全部**要按 240×135 重画、按 QWERTY 重绑定。UI 的"手感"（SLOOP 的核心卖点）不能靠移植保留，只能重新设计。

### 3.5 音频通路：没有"立体声输出"这个 sink

- SLOOP 的渲染端是 `audio_block()`：`out[2*i]` / `out[2*i+1]` 左右声道分开写 24-bit（`src/audio.c:28-44`），混音总线是立体声（`fx.c` 的 `part_buf[2][CTL]`、`drum_buf[4][CTL]`）。
- Cardputer-Adv 的 ES8311 只有**一个 DAC**，耳机口也是单声道（`Cardputer_MPC/docs/可行性评估.md:39,54`）。要跑 SLOOP 就必须在 `audio_block` 出口做 `(L+R)/2`，而 SLOOP 的 chorus / delay / reverb / pan 正是**靠左右差异做空间感**的——求和后这些效果会互相抵消或变成相位染色，听起来会比 FM-1 差一截。
- 这不是"改一行求和"就完：`punch.c` 的 freeze/rewind、`fx.c` 的立体声 reverb 都需要重新判断"单声道下这个 FX 还有没有意义"。想保留则须改硬件（外接 I2S 立体声 DAC + 功放），那就不再是"Cardputer 移植"了。

---

## 4. 可复用的资产与"可移植度"分级（这是移植唯一的好消息）

| 层 | 代码 | 行数 | 可移植度 | 说明 |
|---|---|---:|---:|---|
| **DSP 引擎**（12 引擎、鼓合成、FM6、PHYS、切片） | `eng_*.c`、`drums.c`、`drum_synth.c`、`fm6_core.c`、`phys_dsp.c`、`slicer.c` | 7.9k | **90%** | 纯定点 C，无平台依赖；已实测可编到 ESP32-S3（§5.2） |
| **混音 / 效果 / 声部** | `fx.c`、`voice.c`、`dsp.c`、`punch.c` | 含在上面 | **85%** | 只碰 `fm1_irq_off/on` 与 `HALF_FRAMES/CTL/FS` 常量 |
| **音序器 / 工程 / 预设** | `seq.c`、`arranger.c`、`project.c`、`params.c`、`upreset.c` | 5.2k | **80%** | 无平台依赖，只有 IRQ 临界区；工程格式可原样保留（`project.c` 的自校验） |
| **MIDI 解析** | `midi_uart.c` | 0.2k | **85%** | UART 换成 ESP32 的 UART/`usb_midi` 即可 |
| **存储格式** | `storage.c` + `fm1_flash.h` 接口 | 0.3k | **50%** | 逻辑可留，底层换成 `esp_partition`/SD |
| **USB 设备栈** | `usb.c` | 1.3k | **10%** | 寄存器级实现，只能用 IDF TinyUSB 重写 |
| **HAL / 引导 / 双核** | `hal/*`、`crt0.S`、`main.c` 启动段、`ota.c`、`recovery.c` | 1.4k+ | **0%** | 整段重写 |
| **UI / 输入** | `ui*.c`、`panel.c`、`gfx.c`、`lcd.c` | 8.5k | **15%** | 画法（小画布 + 分条 blit）可留，布局与交互全换 |

**合计**：约 **13.5k 行算法层可较高比例复用**（其中约 11k 行几乎零改动），约 **10.8k 行 UI/输入/USB/HAL 必须重写或大改**。

---

## 5. 本次实测证据（可复现）

### 5.1 代码规模

在 `firmware/` 下按组件统计非空、非注释行：

```
UI 层 (ui*, panel, gfx, lcd, editor*)                       8482
DSP 引擎 (eng_*, fm6, phys, drums, slicer, voice, fx, dsp)  7860
音序器/工程/存储 (seq, arranger, project, params, upreset)   5243
USB/MIDI/OTA (usb, midi_uart, ota, recovery, console)       2041
main/启动 (main, felucca, libc, audio, cpu1)                 806
HAL (firmware/hal/*, 含 .S)                                 1439
loader (引导/OTA)                                            260
```

### 5.2 交叉编译验证（重点）

用本机 PlatformIO 的 `toolchain-xtensa-esp32s3`（crosstool-NG esp-2021r2-patch5，GCC 8.4.0）把**与 `firmware/host/preview.c` 相同的源码集合**编成 ESP32-S3 目标文件；用 `tools/gen_tables.py`、`gen_fm6_patches.py`、`gen_samples.py`、`gen_drumkits.py` 生成缺失的头文件；HAL 全部以桩替换（`fm1_irq_off/on`、`fm1_delay_ms`、`cpu1_*`、`SMP_USER_XIP`、`fm1_in`）。

```bash
xtensa-esp32s3-elf-gcc -Os -ffreestanding -fno-common -c \
  -Ifirmware/src -Ibuild/gen -o portcheck.o portcheck.c   # 0 error（-w 关闭上游自身告警）
```

结果：

- **编译通过**：`core.h` + 全部引擎 + 鼓 + `fx.c` + `usb.c` + `midi_uart.c` + `seq.c` 在 Xtensa 上零错误。**"算法层被 JieLi 指令集锁死"的担忧可以排除。**
- 链接（自有 linker script 重定义 `.pool/.lowram`，`-lgcc` 提供 64 位除法辅助）：`.text 84,838 B`、`.rodata 328,870 B`、`.data 2,620 B`、`.bss 24,092 B`、`.pool 261,244 B`、`.lowram 19,700 B`。
- 生成代码里最热的几个函数（Xtensa 指令数）：`events_block 6244`、`mix_part_run 2803`、`drums_mix 2438`、`grain_render 2026`、`formant_render 1659`、`fm6_render 1518`、`digital_render 904`——与官方 `tests/target_budget.txt` 的 pi32v2 循环预算（`drums_mix 6153`、`analog_render 638`、`digital_render 333`）**不是同一个口径**，不能直接相除，只能说明"同一份 C 在 Xtensa 上生成的代码规模正常"。

### 5.3 结论性数字

| 指标 | 数值 | 判定 |
|---|---:|---|
| SLOOP 静态 RAM（核心） | **≈ 308 KB** | Cardputer-Adv 512 KB − sprite 65 KB − 基线 60–90 KB − DMA/栈 25–40 KB = **≈ 300–330 KB 可用** → **刚好压线/超支** |
| SLOOP Flash（核心，不含 UI） | **≈ 414 KB** | Cardputer 8 MB → **毫无压力** |
| 可零改动复用的算法行数 | ≈ 11k | 需要新增 HAL ≈ 1.5–2k 行 |
| 必须重写的 UI/输入/USB/引导 | ≈ 10.8k | 这部分才是移植的真实成本 |

---

## 6. 三条路线（从省事到激进）

### 路线 A（推荐给"想马上能用"）：**不移植，Cardputer 当 SLOOP 的主机**

- 已有基础：`MidiLooper` v1.2.0 已把 Cardputer-Adv 做成 FM-1 的 USB Host + 4 lane 循环叠录器。
- 可扩展：在它之上加 SLOOP 的 `GLO → MIDI 3`（CLOCK OUT / REMOTE / FX CH / DR MAP）控制，做成"Cardputer 当 SLOOP 的遥控/音序脑"。
- 工作量：**60–120 h**，风险低；缺点：SLOOP 的音源仍来自 FM-1，Cardputer 本身不出声（或只出节拍器）。

### 路线 B（"精简版 SLOOP"）：**移植算法层 + 重写 UI**

- 保留：`eng_*.c`/`fx.c`/`voice.c`/`seq.c`/`project.c`/`params.c`（约 11k 行），用 ESP32-S3 HAL 重新接 I2S/ES8311/TCA8418/ST7789V2；砍 §3.2 列出的 5 项以装进 SRAM。
- 界面：240×135 三页（TRACKS / SEQ / SOUND），用 QWERTY 映射 4 轨 + 旋钮替代方案（`Fn+↑↓` 模拟编码器）。
- 工作量：**250–400 h**；风险：CPU 实测（§3.3）、UI 手感、单声道降级。
- 适合：你确实想在 Cardputer 上"玩到 SLOOP 的引擎和音序器"，而不是要一台 FM-1。

### 路线 C（完整移植）：**不建议**

- 必须同时做到：重写全部 HAL + 引导 + USB 设备栈（UAC 用 IDF）、重做双核渲染分工、重做 8.5k 行 UI、砍功能以进 512 KB、放弃立体声与 USB Audio。
- 工作量：**600–1000 h**；风险高；产出的东西既不是 SLOOP（UI/手感/立体声都变），也不比路线 B 多多少"可玩性"。

> 如果目标只是"在 Cardputer 上要一台 FM 合成鼓机/groovebox"，还有更省的路：以 `Cardputer_MPC`（16 声部、步进音序、采样）为骨架，**只移植 SLOOP 的 2–3 个引擎**（FM6、ANALOG、鼓合成）——这是 120–200 h 的新项目，不是 SLOOP 移植。

---

## 7. 如果坚持要移植：里程碑（每阶段都有明确"停手点"）

| 阶段 | 内容 | 出口判据 | 工作量 |
|---|---|---|---:|
| M0 内存/CPU 预验 | 把 SLOOP 核心用 `-Os` 编到 ESP32-S3，在真机上跑 4 轨满配 `mix_block`，用 `esp_timer` 量 5.8 ms 预算内的渲染时间与最大 RAM 峰值 | 满配渲染 ≤ 4.5 ms/半缓冲，静态+动态 RAM ≤ 430 KB | 30–50 h |
| M1 音频通路 | ES8311 + I2S 双缓冲，先放一个引擎（ANALOG）出声；`HALF_FRAMES=256` 不变 | 连续 1 h 无欠载、无爆音；延迟 ≤ 12 ms | 40–70 h |
| M2 核心移植 | 12 引擎 + 鼓 + 混音 + 音序器 + 工程读写接上；`fm1_irq_*` → FreeRTOS 临界区；`fm1_flash` → 分区/SD，音频代码 `IRAM_ATTR` | 4 轨 64 步完整播放、工程保存/读取、`project.c` 自校验通过 | 80–140 h |
| M3 输入与 UI | TCA8418 扫描、`Fn+` 模拟旋钮、240×135 三页 + 局部刷新 | 不看文档能完整编一段 beat | 120–200 h |
| M4 USB/存储/收尾 | USB-MIDI（Device）或 Host 二选一；SD 采样；OTA | 与 FM-1/PC 各自可用；OTA 可用 | 60–120 h |
| M5 打磨 | 砍功能后的音质/电平/latency 调优、文档 | 可发布 | 100–200 h |

**M0 必须先做**：如果 M0 的 RAM 峰值或渲染时间不达标，后面 800 h 都是沉没成本。

---

## 8. 合规与其他注意

- **许可**：SLOOP 是 **GPL-3.0-only**（`LICENSING.md:3-27`），"分发基于它的固件必须给出完整对应源码"。移植版必须同样以 GPL-3.0 开源发布；不要指望闭源或只发 bin。第三方部分：`fm6_core.c` 为 Apache-2.0（msfa），`phys_dsp.c/phys_symp.c` 为 MIT（DaisySP/Rings），采样为 CC0——都可以带到新平台。
- **不要复制"设备资产"**：Felucca 的 `assets/icons.png`、`panel.jpg`、Hügelton Sample Pack 是"all rights reserved"（`LICENSING.md:5-9`），SLOOP 自己的固件不含它们；移植时同样避开。
- **SLOOP 的 OTA/Web 安装器**（浏览器 Web MIDI 直刷 `.fwsc`）是 FM-1 专属格式，Cardputer 上要换成 esptool/OTA 分区，别试图复用。
- **法律/品牌**：可称 "based on SLOOP/Felucca (GPL-3.0)"，但别用 M-VAVE/FM-1 商标做产品名。

---

## 9. 待实测项清单（写报告时不要当结论）

| # | 待确认 | 为什么关键 | 怎么测 |
|---|---|---|---|
| 1 | 4 轨满配 `mix_block` 在 ESP32-S3 上的耗时 | 决定路线 B/C 是否成立 | M0：真机 + `esp_timer`；或 `hostsim` 在 ESP32 上跑 60 s 统计 |
| 2 | IDF 启动后最大连续内部 SRAM | 决定砍功能清单 | `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)` |
| 3 | `fm1_irq_off` 临界区在 FreeRTOS 下是否需改结构（音频回调与主任务竞争） | 决定 `seq.c/fx.c` 的 30 处临界区 | 原型 + 压力测试 |
| 4 | ES8311 单声道下 SLOOP 的立体声效果如何降级（合并/丢弃） | 音质与工作量 | 先只做 `L+R` 求和，听感验收 |
| 5 | ESP32-S3 上 Flash 擦写期间的 cache 关闭对音频的影响 | 决定"保存工程时是否爆音" | 音频任务 `IRAM_ATTR` + 写盘压力测试 |
| 6 | 240×135 上 SLOOP 的三页信息密度是否可读 | 决定 UI 是否可行 | 先出 240×135 设计稿（可用 `Cardputer_MPC/design` 的 1× 流程） |
| 7 | USB Host（接 FM-1）与 USB-MIDI Device 能否共存 | 决定"Cardputer 当主机还是设备" | 只能二选一，需产品决策 |

---

## 10. 最终评分卡

| 维度 | 分数（10 分制） | 理由 |
|---|---:|---|
| 算法/音序器可移植性 | **8.5** | 单编译单元 + 纯头文件 HAL，已实测能编到 Xtensa；有 host/WASM 双重验证 |
| 底层重写量 | **2.5** | 1.4k 行 HAL + 1.3k 行 USB + 引导全部重写 |
| 内存可行性（Cardputer-Adv） | **2.0** | 静态 308 KB + 新增 ≈ 100 KB，超 512 KB |
| CPU 可行性 | **6.5** | 同代 240 MHz 双核，31% 基线；需实测，2× 误差带内 |
| UI/输入可行性 | **2.0** | 8.5k 行 UI 与"键 + 旋钮"交互整体重设计；135 px 高度不够 |
| 音频质量保留 | **3.5** | 立体声、USB Audio、pan 全部损失；单声道求和会削弱 chorus/reverb |
| 工程量（完整移植） | **1.0** | 600–1000 h，单人 4–6 个月 |
| **综合（"完整移植到 Cardputer-Adv"）** | **2.5 / 10** | **不可行，不建议** |
| **综合（"精简版 SLOOP"）** | **6.0 / 10** | 可行但等于新产品，250–400 h |
| **综合（"不移植，走路线 A"）** | **8.5 / 10** | 复用现有 `MidiLooper`，60–120 h 可交付 |

**建议**：先做 §7 的 **M0（30–50 h）**。它只回答两个问题——RAM 峰值、满配渲染时间——这两个数出来，路线 B 值不值得做就有结论了。在 M0 之前，任何"移植工期"都只是估算。
