# MidiLooper v0.1.0 — UI + firmware MVP

**状态**：用户已上机确认可用；UI/Web 逻辑测试与 C++ host test 通过；固件 PlatformIO 编译通过；合并镜像已生成。

## 验证

- **Web 模型**：`web/test.html` / `node web/test/run-node.js`，16/16 `ALL PASS`。
- **交互 smoke**：`pwsh tools/web-smoke.ps1`（headless Edge + CDP），21/21 通过。
- **C++ host**：`firmware/test/host/test_loop.cpp` 16/16 `RESULT OK`；`test_store.cpp`（SMF writer/reader + fake SD）验证 header/track length/EOT 和 save→load 往返，并修掉了 `MTrk` 长度多 4 字节、tempo 少 10 倍的 bug。

## 内容

- 240×135 Gruvbox Dark UI：LOOP / FILE 两页 + 设置浮层；Web 交互预览（`web/index.html`）。
- 固件：ESP32-S3 USB Host MIDI IN/OUT、四通道循环引擎（1–32 小节 / 480 PPQN）、首音起录、按相位叠录、双缓冲合并、Quantize（OFF/1/4/1/8/1/16/1/32）、Swing（50–75%）、Undo（take generation 过滤）、per-lane Mute、`.mid` 载入播放、SMF Format 0 保存/读取、本地 I2S 节拍器、文件删除/改名、NVS。
- 构建数据：RAM 209,060 B（63.8%），Flash 677,897 B（21.5%）；合并镜像 `dist/CardputerLooper-8MB.bin`（726.4 KB，从 `0x0`）。
- SHA-256 见 `dist/SHA256SUMS.txt`。

## 已知限制

- 不与 FM-1 sequencer 锁小节相位；内部 BPM 为主。
- 8192 事件上限；32 小节 32 分单音刚好，密集和弦会截断。
- 叠录一次一条 lane；文件载入只支持 SMF Format 0/1、非 SMPTE，超过 32 小节会截断。
- Mute/Solo：只有 Mute，没有 Solo；Mute 只影响回放，保存仍包含全部事件。
- Quantize 对 on/off 都生效（极短音符可能压到同一 tick）；Swing 只作用于精确 8 分格。
- Sloop `ARP 2 → HOLD` 必须 OFF，否则停止后挂音。
- **旧版文件**：v0.1 旧固件保存的 `.mid` tempo 可能只有正确值的 1/10；本轮 writer 已修，load 对 `tempo < 150000` 做 ×10 兼容，仍建议用新固件重新保存。

详细说明见 [README.md](README.md)，设计评估见 [docs/设计评估.md](docs/设计评估.md)。
