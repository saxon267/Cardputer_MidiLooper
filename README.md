# MidiLooper — Cardputer-Adv MIDI Looper

240×135、Gruvbox Dark 的四通道 MIDI 循环叠录固件。Cardputer-Adv 作为 **USB-C Host** 直连 M-VAVE FM-1（Sloop），把 FM-1 的 CH1/2/3/10 录成循环，叠录后回放给 FM-1 发声。

> **状态 v0.1.0**：UI 预览与固件 MVP 已完成，PlatformIO 编译通过（RAM 63.8%，Flash 21.4%），合并镜像已生成；**尚未在真机上逐项验收**。USB Host 枚举 / FM-1 全双工仍是实验路径，第一次上机先看第 6 节。

![LOOP 播放](docs/ui/loop-play.png)

---

## 1. 功能

| 模块 | 说明 |
|---|---|
| USB Host | ESP32-S3 USB-OTG Host，MIDI IN + OUT 同时开；FM-1 自供电 |
| 循环 | 1/2/4/8/16/32 小节；480 PPQN；内部 BPM 40–400 |
| 叠录 | 已有循环时按 R 立即按当前相位叠录；循环继续播放 |
| 通道 | CH1 LEAD / CH2 PAD / CH3 BASS / CH10 DRUM，四 lane 同长度 |
| 节拍器 | 本地 I2S click（2 kHz 重音 / 1.35 kHz 普通）；录音/播放可开关；首录可选 4 拍 count-in |
| 走带 | Enter 播放/停止；停止时 panic（显式 Note Off + CC64/121/123/120） |
| 保存 | `S` → `/MidiLoop/LOOP_xxxx.MID`，SMF Format 0、480 PPQN、EOT 在循环末尾 |
| 文件管理 | `FILE` 页删除 / 改名；只显示 `.mid` / `.midi` |
| 设置 | BPM、小节数、click、count-in；NVS 保存 |
| UI | 两个标签页 LOOP / FILE + 设置浮层；240×135 sprite |

事件类型：Note On/Off、CC64 延音、CC1 调制、Pitch Bend。**不发送** Program Change。

---

## 2. 快速开始

### 2.1 先看 UI（不用刷机）

双击 **`web/index.html`**，或拖进浏览器。左侧是 240×135 的 1:1 画布（3× 放大），右侧是走带 / BPM / 小节 / 节拍器控制；键盘 `R`、`Enter`、`Tab`、`S`、`M`、`1–4`、方向键都能用。右侧 `TESTS` 按钮打开逻辑自检页。

> 模拟器是 UI 的交互预览，不是固件二进制；版式常量与 `firmware/src/ui.cpp` 一一对应。录音时的数据来自 `web/engine.js`（与 `loop.cpp` 同一套状态机/时基契约），不是随机数。

### 2.2 编译 / 烧写

```bash
cd firmware
pio run                 # 编译
pio run -t upload       # USB-C 烧写（需要先断开 FM-1）
```

依赖：PlatformIO + `espressif32@6.9.0`；`M5Unified 0.3.0`、`M5GFX 0.3.0`、`M5Cardputer 1.1.1`。

### 2.3 合并镜像（从 0x0 烧写）

```bash
pwsh tools/make_bin.ps1
# dist/CardputerLooper-8MB.bin
esptool.py --chip esp32s3 -p COMx -b 460800 write_flash 0x0 dist/CardputerLooper-8MB.bin
```

**刷写顺序**：断开 FM-1 → Cardputer 接 PC → 进下载模式 → 烧写 → 拔掉 PC 的 USB-C → 断电重开 → 再接 FM-1。Host build 没有 USB-CDC 串口，调试看屏幕；不要同时把 Cardputer 接到 PC 和 FM-1。

---

## 3. 界面

| LOOP 空闲 | LOOP 录制 | FILE |
|---|---|---|
| ![LOOP idle](docs/ui/loop-idle.png) | ![LOOP rec](docs/ui/loop-rec.png) | ![FILES](docs/ui/files.png) |

| 设置浮层 | 完整预览页 |
|---|---|
| ![SETTINGS](docs/ui/settings.png) | ![preview](docs/ui/preview-page.png) |

顶栏：`LOOP` / `FILE` 标签、USB 状态（红=Host 失败，黄=已枚举无 MIDI OUT，灰=未识别 FM-1，绿=已连接）、SD、电量。

LOOP 页：走带状态、`BAR n/N`、BPM、循环进度/拍点、四 lane 计数与 18 格电平、底部按键提示。

FILE 页：`/MIDILOOP` 路径、文件数、6 行列表（文件名 + 大小），选中行反色。

设置浮层：BPM / BARS / CLICK / COUNT-IN，`;` / `.` 选行，`,` / `/` 改值，`Bksp` / `G` / `Tab` 关闭。

### 3.1 Web 逻辑自检

[web/test.html](<web/test.html>) 直接跑 **11 个场景**：小节/BPM clamp、首录、自动停、叠录相位、从停止叠录、通道过滤、early stop、8192 容量、停止/清空、ARMED 超时、拍点。当前结果 **ALL PASS**。

![Web logic tests](docs/ui/logic-tests.png)

也可以在 PC 上无浏览器跑同一套：

```bash
node web/test/run-node.js
# RESULT OK
```

`pwsh tools/web-smoke.ps1` 会启动 headless Edge，通过 CDP 真实点击/按键走一遍交互流程（count-in → REC → 自动停 → OVERDUB → SAVE → CLEAR → early stop → BARS 保护），当前 **18 项检查全通过**。

> 这验证的是 Web 模型（`web/engine.js`）与 UI 交互契约；固件侧的 C++ 实现对同一组场景另有 host test（第 9 节）。两者都通过，但仍**不能替代** USB/ES8311/SD 的真机验证。

---

## 4. 固件按键

### LOOP 页

| 键 | 动作 |
|---|---|
| `R` / `Space` | 录音 / 停止录音；已有循环时进入叠录 |
| `Enter` | 播放 / 停止 |
| `S` | 保存为 `/MidiLoop/LOOP_xxxx.MID`（会先停止播放） |
| `C` | 清空循环 |
| `M` | 节拍器开 / 关 |
| `G` | 打开设置浮层 |
| `Tab` | 切到 FILE |
| `1` / `2` / `3` / `4` | 选中 CH1 / CH2 / CH3 / CH10 lane |
| `,` / `/` | BPM −1 / +1 |
| `Fn` + `,` / `/` | BPM −5 / +5 |
| `;` / `.` | 上 / 下选择 lane |
| `Fn` + `;` / `.` | 小节数 + / −（1/2/4/8/16/32） |
| `-` / `=` | BPM −1 / +1 |
| `Bksp` | 停止（停止录音时保留已录内容） |

### FILE 页

| 键 | 动作 |
|---|---|
| `;` / `.` | 上 / 下选择文件 |
| `D` | 删除（`Enter` 确认，`Bksp` / `D` 取消） |
| `N` | 改名（直接键入；`Enter` 确认，`Bksp` 退格，`Tab` 取消） |
| `Enter` | 提示可用操作（v0.1 还没有文件载入） |
| `Tab` / `Bksp` | 回到 LOOP |

### 设置浮层

| 键 | 动作 |
|---|---|
| `;` / `.` | 选行 |
| `,` / `/` | 改值（BPM ±5、BARS 切换、CLICK/COUNT-IN 开关） |
| `Enter` | 开关当前项；BPM/BARS 行关闭浮层 |
| `Bksp` / `G` / `Tab` | 关闭 |

---

## 5. 循环行为（固件契约）

这是 v0.1 真正实现的行为，和 UI 提示一致：

1. **长度由小节数决定**：`loopLen = bars × 4 × 480` ticks（4/4、480 PPQN）。32 小节 = 61,440 ticks。
2. **第一次录音**：按 `R` → `ARMED`。若开启 count-in，先响 4 拍 click；收到第一个 CH1/2/3/10 音符 → 从 tick 0 开始录。
3. **录满自动停**：到 `loopLen` 自动结束并进入 `PLAY`。提前按 `R` 会收尾，但**保持小节数不变**，尾部补静音。
4. **叠录**：已有循环且 `PLAY` 时按 `R` → 立即按当前相位叠录（事件 tick 对 `loopLen` 取模）；再按 `R` 结束；到循环末尾自动结束。循环继续播放，不需要等首音或等循环边界。
5. **播放/停止**：`Enter`。停止时按活动音符表发 Note Off，再补 CC64/121/123/120。
6. **保存**：`S` 先停止，再写 `/MidiLoop/LOOP_xxxx.MID`；EOT 落在 `loopLen`，不是最后一个事件。
7. **改长度**：必须先 `C` 清空；有循环时改 BARS 会提示 `CLEAR FIRST`。
8. **录音中禁改 BPM**：避免已录事件的时间戳跳变。

---

## 6. FM-1 / Sloop 设置（运行前提）

| Sloop 项 | 值 |
|---|---|
| `GLO → MIDI` | CH1=1，CH2=2，CH3=3，CH4=10（鼓） |
| `GLO → MIDI 2` | `OUT = SEQ`（录 sequencer/arp）或 `OUT = KEYS`（只录键盘）；`IN = NOTES` |
| `ARP MODE` | **OFF**（否则回放音符会被 arp/quantize 改写） |
| `ARP 2 → HOLD` | **OFF**（HOLD ON 会锁住 Note Off，停止后挂音） |
| `DR MAP` | GM |
| `SYNC` | INT；`CLOCK = OUT` 可选（v0.1 不参与计时） |

v0.1 **以 Cardputer 内部 BPM 为主**，不解析 FA/FB/FC，也不和 FM-1 sequencer 锁第 1 小节。要边跑 FM-1 sequencer 边 loop，请手动设成同一 BPM，并接受没有小节相位锁定。

> 真机第一件事：确认 USB 状态变绿、按 `R` 能进入 `REC`、FM-1 键盘有声音；再检查 `ARP/HOLD` 和 Sloop 的 thru 行为。

---

## 7. 代码结构

```
firmware/src/
├── main.cpp          # 按键、NVS、SD、USB 回调、状态接线
├── loop.h/.cpp       # 纯循环引擎：tick 时基、ARMED/REC/PLAY、双缓冲合并
├── loop_store.h/.cpp # SMF Format 0 writer（/MidiLoop）
├── audio.h/.cpp      # ES8311 + I2S 44.1 kHz，click 合成/混音
├── usb_midi_host.*   # USB Host MIDI IN/OUT、F8 测 BPM、panic
├── library.h/.cpp    # /MidiLoop 扫描、排序
├── ui.h/.cpp         # 240×135 两页 + 设置/删除/改名浮层
├── font8.h           # Silkscreen 8 px（Regular + Bold）
├── config.h          # 引脚、音频、容量、Gruvbox 色板
└── app.h             # 全局 UI/走带状态
```

**任务划分**

- 音频任务（core 1，prio 20）：推进 `looper.pump()`、按 tick 发 USB 事件、混 click。
- USB 任务（core 0，prio 5）：IN/OUT 收发；输入回调只做 `looper.onMidi()`。
- 主任务（core 1）：UI、按键、SD 保存、叠录结束时的排序合并、panic。

**循环引擎**

- 双缓冲 `8192` 事件（64 KB × 2）+ 单 lane staging `4096`（32 KB）：
  - 第一次录音直接写 inactive buffer，结束时排序、交换、播放；
  - 叠录写 staging，结束时与 active 归并到 inactive，再交换；
  - 音频任务只读 active，不在音频任务里排序/写 SD。
- `_phaseQ16` 以音频帧推进（64 帧 = 1.45 ms），循环边界和 click 同源。
- USB 输入 tick 用 `micros()` 换算；先录首音时以首音为 tick 0，叠录时读当前相位（±1 个音频块）。

---

## 8. 内存 / 构建数据

| 项 | 值 |
|---|---:|
| 静态 RAM | 209,020 B / 327,680 B（63.8%） |
| Flash | 671,969 B / 3,145,728 B（21.4%） |
| 循环事件 | 2 × 8192 × 8 B + 4096 × 8 B = 160 KB |
| UI sprite | 240 × 135 × 16 bpp = 64.8 KB（堆） |
| 音频 | 44.1 kHz / 64 帧块 / 6 DMA |

静态占用偏高是因为双缓冲；如果真机上 UI sprite 分配失败，把 `config.h` 的 `MAX_LOOP_EVENTS` 从 8192 降到 6144（约省 32 KB），容量仍覆盖 32 小节 16 分单音 + 部分和弦。

---

## 9. 主机侧测试

循环引擎是纯 C++（`loop.cpp` 不依赖 USB/音频/SD），可以在 PC 上跑：

```bash
cd firmware
g++ -std=c++17 -I test/host -I src test/host/test_loop.cpp src/loop.cpp -o test_loop
./test_loop
# 期望：RESULT OK
```

没有 g++ 时可以用 Zig 自带的 C++ 前端：

```bash
python -m ziglang c++ -std=c++17 -I test/host -I src ^
  test/host/test_loop.cpp src/loop.cpp -o test_loop.exe
test_loop.exe
```

覆盖：小节/BPM clamp、首录、自动停、叠录相位、从停止叠录、通道过滤/lane 映射、early stop、8192 容量、停止/清空、ARMED 超时、拍点。`test/host/Arduino.h` 是 host stub（`millis`/`micros`/`portMUX` 空实现），**不测试** USB、ES8311、SD 和真机时序；同一组场景的 Web 版本见第 3.1 节。

## 10. 已知限制（v0.1 诚实列表）

1. **没有真机验收**：USB 枚举、全双工、ES8311 click、SD 长时读写都只过了编译；第一次上机按第 6 节逐项确认。
2. **USB Host 是实验路径**：FM-1 是 USB MIDI + Audio 复合设备，描述符 >256 B 可能枚举失败；host build 没有 USB-CDC 串口。
3. **不同步 FM-1 sequencer 的小节相位**：不解析 FA/FB/FC；只支持内部 BPM 为主。要同步需后续加 transport + Sloop 外部时钟。
4. **容量**：8192 事件；32 小节 32 分单音刚好，16 分 4 音和弦 4 lane 会截断（`TAKE FULL` / 丢事件）。
5. **叠录一次只叠一条 lane**：staging 4096；第一次录音可以四通道一起录（FM-1 sequencer）。
6. **没有文件载入**：FILE 页只删除/改名，保存的 `.MID` 不能在固件里重新载入播放。
7. **没有 per-lane mute/solo、quantize、undo**；只有 `C` 清空。
8. **count-in 只作用于第一次录音**；叠录立即开始。
9. **Sloop HOLD**：`ARP 2 → HOLD` 必须 OFF，否则停止后挂音；这是 Sloop 侧设置。
10. **不发送 Program Change / SysEx**；只录 Note/CC64/CC1/PB。

---

## 11. 设计文档

- **[设计评估](docs/设计评估.md)**：可行性、4 个必须先定的语义、容量测算、风险与里程碑。
- **UI 规格**：版式常量见 `web/app.js` 与 `firmware/src/ui.cpp`（两边一一对应）。
- **发布说明**：本 README 即 v0.1 说明。

## 12. 许可

MIT（见 `LICENSE`）。字体 Silkscreen 为 SIL OFL 1.1；配色 Gruvbox（MIT）；硬件库 M5Unified / M5GFX / M5Cardputer（MIT）。
