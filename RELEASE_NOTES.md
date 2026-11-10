# MidiLooper v1.3.1

**Cardputer-Adv 四通道 MIDI 循环叠录固件。** v1.3.1 正式发布。

## 本版更新（相对 v1.2.0）

- **`Fn+C` 单轨清空**：只删除当前选中 lane 的 MIDI，其他 lane、loop 长度、Mute 状态和 `.mid` 文件都不变。录音中同时清 active 与当前 take。
- **恢复 `L` 的 Loop 模式启动功能**：
  - `L` 开启即开始：没有循环先 `ARMED`，已有循环直接 `OVERDUB`；
  - 到循环末尾自动续录，不需要反复按 `R`；
  - `L` 关闭即结束当前 take；
  - Loop 录音/ARMED 中按 `R` = 关闭 Loop 模式并停止，避免两个键抢状态；
  - 非 Loop 模式 `R` 仍是普通录音/叠录。
- **启动页**：显示 **`v1.3.1`**，停留 **1.5 秒**。
- 之前 v1.2.0 的 Loop 模式、录制闪动、Quantize note-off 丢音修复全部保留。

## 功能

- **USB Host**：Cardputer-Adv 通过 USB-C 直连 M-VAVE FM-1（Sloop），MIDI IN/OUT 同时工作。
- **四通道循环**：CH1 LEAD / CH2 PAD / CH3 BASS / CH10 DRUM，1–32 小节，480 PPQN。
- **录制/叠录**：首音起录，循环末尾自动停；已有循环时按 `R` 按当前相位叠录。
- **Quantize**：OFF / 1/4 / 1/8 / 1/16 / 1/32，默认 1/16；只吸附 note-on/CC/PB。
- **Swing**：50–75%，只作用于精确 8 分格。
- **Undo**：`U` 撤销最后一次叠录。
- **Mute**：`Fn` + `1..4` 静音/恢复 CH1/2/3/10。
- **单轨清空**：`Fn` + `C` 清空当前选中 lane。
- **全清**：`C` 清空整个 loop，不删除 `.mid` 文件。
- **文件播放**：FILE 页 `Enter` 载入 `/MidiLoop` 的 `.mid` 并播放。
- **保存**：`S` 写 `/MidiLoop/LOOP_xxxx.MID`，SMF Format 0、480 PPQN。
- **节拍器 / count-in / NVS**。

## 烧写

### 方案 A：已有 M5 Apps（推荐，保留 M5 Apps 和其他 app）

用 **M5 Apps Installer → SD Card** 安装：

```text
CardputerLooper-v1.3.1-app.bin
```

或进下载模式后只写 CardputerLooper 分区。当前这台 M5 Apps 分区表里它是：

```bash
esptool.py --chip esp32s3 -p COMx -b 460800 write_flash 0x5A0000 CardputerLooper-v1.3.1-app.bin
```

**不要写 `0x1B0000`**：M5 Apps 安装多个 app 后，那里现在是 `CardputerWaveL`。分区偏移可能随安装变化，优先用 M5 Apps Installer。

### 方案 B：整机全新烧写

`CardputerLooper-8MB.bin` 是完整 8 MB 镜像，从 **0x0** 烧写：

```bash
esptool.py --chip esp32s3 -p COMx -b 460800 write_flash 0x0 CardputerLooper-8MB.bin
```

**这会覆盖 bootloader、分区表和 M5 Apps / 其他 app。** 已有 M5 Apps 的用户请用方案 A。

## 验证

- **Web 模型**：`web/test.html` / `node web/test/run-node.js`，18/18 `ALL PASS`。
- **交互 smoke**：`pwsh tools/web-smoke.ps1`（headless Edge + CDP），24/24 通过。
- **C++ host**：`test_loop.cpp` 18/18 `RESULT OK`；`test_store.cpp` SMF write→load 往返通过。
- **构建**：RAM 210,116 B（64.1%），Flash 681,737 B（21.7%）。
- **启动版本**：`v1.3.1`，1.5 s。

## 已知限制

- **没有本地音源**：Enter 只把 MIDI 发给 FM-1；FM-1 未连接时不会响。
- Cardputer-Adv 只有一个 USB-C：接 PC 串口时不能再接 FM-1。要同时看串口日志，需要 UART0 的 USB-UART 适配器。
- `ARDUINO_USB_CDC_ON_BOOT=0`：USB-C 的 COM 口只能看 ROM/下载日志，app 的 `Serial` 在 UART0。
- 不与 FM-1 sequencer 锁小节相位；内部 BPM 为主。
- 8192 事件上限；32 小节 32 分单音刚好，密集和弦会截断。
- 文件载入只支持 SMF Format 0/1、非 SMPTE；超过 32 小节会截断。
- Mute 只影响回放，保存仍包含全部事件；没有 Solo。
- 旧版 v0.1 保存的 `.mid` tempo 可能只有正确值的 1/10；载入时兼容，仍建议重新保存。
- Sloop `ARP 2 → HOLD` 必须 OFF，否则停止后挂音。

## SHA-256

```text
b116070bb340faaebbec9b3d5a1da9ac02ba945ccfc5dffe70eda53bd744f903  CardputerLooper-8MB.bin
818c20cf42550bb86b9aecac5ba9555cc12d2a9e652e521a4f0103788529ba35  CardputerLooper-v1.3.1-app.bin
```
