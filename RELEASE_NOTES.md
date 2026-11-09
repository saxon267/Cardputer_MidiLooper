# MidiLooper v1.2.0

**Cardputer-Adv 四通道 MIDI 循环叠录固件。** v1.2.0 已上机验证，作为新的正式版本发布。

## 本版更新（相对 v1.0.0）

- **Loop 模式**：`L` 开关（NVS 保存）。开启后，第一遍用 `R` 启动，之后每到循环末尾自动进入下一遍 overdub，不再需要按 `R`。`L` 只切换自动续录，不抢 `R` 的录音功能；手动 `R` 结束当前 take 后不会自动续录。
- **录制通道闪动**：`ARMED` / `REC` / `OVERDUB` 中收到 note-on，对应 lane 行闪动 90 ms，UI 以 30 fps 渲染。
- **启动页**：显示 `v1.2.0`，停留 **1.5 秒**。
- **丢音修复**：Quantize 只吸附 note-on / CC / PB，note-off 保留原始时长；note-off 落在 note-on 之前时 clamp 到 `on+1`，避免零长度/挂音。
- **事件上限提示**：缓冲满时屏幕显示 `EVENT LIMIT`；串口 `i` 增加 `rx` / `ovf` / `mode` 诊断字段。
- 之前 v1.0.0 已有的文件播放、per-lane Mute、Quantize、Swing、Undo 全部保留。

## 功能

- **USB Host**：Cardputer-Adv 通过 USB-C 直连 M-VAVE FM-1（Sloop），MIDI IN/OUT 同时工作。
- **四通道循环**：CH1 LEAD / CH2 PAD / CH3 BASS / CH10 DRUM，1–32 小节，480 PPQN。
- **录制/叠录**：首音起录，循环末尾自动停；已有循环时按 `R` 按当前相位叠录。
- **Quantize**：OFF / 1/4 / 1/8 / 1/16 / 1/32，默认 1/16。
- **Swing**：50–75%，只作用于精确 8 分格，播放时动态偏移，不改存储。
- **Undo**：`U` 撤销最后一次叠录（take generation 过滤；叠录中按 = 取消当前 take）。
- **Mute**：`Fn` + `1..4` 静音/恢复 CH1/2/3/10；播放跳过该 lane，并 allNotesOff 防挂音。
- **文件播放**：FILE 页 `Enter` 载入 `/MidiLoop` 的 `.mid`，按文件 tempo/bars 播放；`C` 只清 RAM 轨道，不删除文件。
- **保存**：`S` 写 `/MidiLoop/LOOP_xxxx.MID`，SMF Format 0、480 PPQN、EOT 在循环末尾。
- **文件管理**：删除 / 改名；只显示 `.mid` / `.midi`。
- **节拍器**：本地 I2S click，2 kHz 重音 / 1.35 kHz 普通；首录可选 4 拍 count-in。
- **UI**：240×135 Gruvbox Dark，LOOP / FILE 两页 + 设置浮层。

## 烧写

### 方案 A：已有 M5 Apps / 启动器（推荐，保留 M5 Apps）

只写 CardputerLooper 分区，**不要写 0x0**：

```bash
esptool.py --chip esp32s3 -p COMx -b 460800 write_flash 0x1B0000 CardputerLooper-1.2.0-app.bin
```

或用 M5 Apps 的 Installer 从 SD 卡安装这个 app-only 文件。M5 Apps 位于 `0x10000`，不会被覆盖。

### 方案 B：整机全新烧写

`CardputerLooper-8MB.bin` 是完整 8 MB 镜像，从 **0x0** 烧写：

```bash
esptool.py --chip esp32s3 -p COMx -b 460800 write_flash 0x0 CardputerLooper-8MB.bin
```

**注意**：方案 B 会覆盖 bootloader、分区表和 M5 Apps。已有 M5 Apps 的用户请用方案 A。

## 验证

- **Web 模型**：`web/test.html` / `node web/test/run-node.js`，17/17 `ALL PASS`。
- **交互 smoke**：`pwsh tools/web-smoke.ps1`（headless Edge + CDP），24/24 通过。
- **C++ host**：`test_loop.cpp` 17/17 `RESULT OK`；`test_store.cpp` SMF write→load 往返通过。
- **构建**：RAM 210,116 B（64.1%），Flash 679,601 B（21.6%）。
- **修复记录**：`MTrk` 长度多 4 字节、SMF tempo 少 10 倍、Quantize 压掉短音符。

## 已知限制

- **没有本地音源**：Enter 只把 MIDI 发给 FM-1；FM-1 未连接时不会有声音。
- Cardputer-Adv 只有一个 USB-C：接 PC 串口时不能再接 FM-1。要同时看串口日志，需要 UART0 的 USB-UART 适配器。
- `ARDUINO_USB_CDC_ON_BOOT=0`：USB-C 的 COM 口只能看 ROM/下载日志，app 的 `Serial` 在 UART0。
- 不与 FM-1 sequencer 锁小节相位；内部 BPM 为主。
- 8192 事件上限；32 小节 32 分单音刚好，密集和弦会截断。
- 文件载入只支持 SMF Format 0/1、非 SMPTE；超过 32 小节会截断。
- Mute 只影响回放，保存仍包含全部事件；没有 Solo。
- 旧版 v0.1 保存的 `.mid` tempo 可能只有正确值的 1/10；载入时对 `tempo < 150000` 做 ×10 兼容，仍建议用新固件重新保存。
- Sloop `ARP 2 → HOLD` 必须 OFF，否则停止后挂音。

## SHA-256

```text
c29983ee68102572704b859063e88ade6878c7c3f4c97759f5f2912221f41b3f  CardputerLooper-8MB.bin
3e55e2c72af4ac8a5292d29fecbbb9999caa966c80458570852269d020f5effc  CardputerLooper-1.2.0-app.bin
```
