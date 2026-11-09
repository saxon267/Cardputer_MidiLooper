# MidiLooper v1.0.0

**Cardputer-Adv 四通道 MIDI 循环叠录固件 — 首个正式版本。** 用户已上机确认可用。

## 功能

- **USB Host**：Cardputer-Adv 通过一根 USB-C 直连 M-VAVE FM-1（Sloop），MIDI IN/OUT 同时工作。
- **四通道循环**：CH1 LEAD / CH2 PAD / CH3 BASS / CH10 DRUM，1–32 小节，480 PPQN。
- **录制/叠录**：首音起录，循环末尾自动停；已有循环时按 `R` 立即按当前相位叠录。
- **Quantize**：OFF / 1/4 / 1/8 / 1/16 / 1/32，默认 1/16，在 `onMidi()` 入口吸附。
- **Swing**：50–75%，对奇数 8 分格事件在发送前加 tick 偏移，不改存储。
- **Undo**：`U` 撤销最后一次叠录（take generation 过滤；叠录中按 = 取消当前 take）。
- **Mute**：`Fn` + `1..4` 静音/恢复 CH1/2/3/10；播放跳过该 lane，并 allNotesOff 防挂音。
- **文件播放**：FILE 页 `Enter` 载入 `/MidiLoop` 的 `.mid` 并按文件 tempo/bars 播放；`C` 只清 RAM 轨道，不删除文件。
- **保存**：`S` 写 `/MidiLoop/LOOP_xxxx.MID`，SMF Format 0、480 PPQN、EOT 在循环末尾。
- **文件管理**：删除 / 改名；只显示 `.mid` / `.midi`。
- **节拍器**：本地 I2S click，2 kHz 重音 / 1.35 kHz 普通；首录可选 4 拍 count-in。
- **UI**：240×135 Gruvbox Dark，LOOP / FILE 两页 + 设置浮层。

## 烧写

下载 `CardputerLooper-8MB.bin`，从 **0x0** 烧写：

```bash
esptool.py --chip esp32s3 -p COMx -b 460800 write_flash 0x0 CardputerLooper-8MB.bin
```

或浏览器打开 <https://espressif.github.io/esptool-js/>，选文件，地址填 `0x0`。

**顺序**：断开 FM-1 → Cardputer 接 PC → 进下载模式 → 烧写 → 拔掉 PC 的 USB-C → 断电重开 → 再接 FM-1。Host build 没有 USB-CDC 串口，调试看屏幕。

## 验证

- **Web 模型**：`web/test.html` / `node web/test/run-node.js`，16/16 `ALL PASS`。
- **交互 smoke**：`pwsh tools/web-smoke.ps1`（headless Edge + CDP），21/21 通过。
- **C++ host**：`test_loop.cpp` 16/16 `RESULT OK`；`test_store.cpp` SMF save→load 往返通过。
- **构建**：RAM 209,060 B（63.8%），Flash 677,897 B（21.5%）；合并镜像 726.4 KB。
- 测试过程中修掉：`MTrk` 长度多 4 字节、SMF tempo 少 10 倍。

## 已知限制

- 不与 FM-1 sequencer 锁小节相位；内部 BPM 为主。
- 8192 事件上限；32 小节 32 分单音刚好，密集和弦会截断。
- 文件载入只支持 SMF Format 0/1、非 SMPTE；超过 32 小节会截断。
- Mute 只影响回放，保存仍包含全部事件；没有 Solo。
- Quantize 对 note-on/off 都生效（极短音符可能压到同一 tick）；Swing 只作用于精确 8 分格。
- 旧版 v0.1 保存的 `.mid` tempo 可能只有正确值的 1/10；载入时对 `tempo < 150000` 做 ×10 兼容，仍建议用新固件重新保存。
- Sloop `ARP 2 → HOLD` 必须 OFF，否则停止后挂音。

## SHA-256

```text
f122b933c10c863c5fc524b4d194bd4d416eac8e28f347be39a01090172c032f  CardputerLooper-8MB.bin
```
