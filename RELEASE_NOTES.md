# MidiLooper v0.1.0 — UI + firmware MVP

**状态**：UI 预览、Web 逻辑自检与 C++ host test 通过；固件 PlatformIO 编译通过；合并镜像已生成。**尚未在真机逐项验收**。

## 验证

- **Web 模型**：`web/test.html` / `node web/test/run-node.js`，11/11 `ALL PASS`。
- **交互 smoke**：`pwsh tools/web-smoke.ps1`（headless Edge + CDP），18/18 通过。
- **C++ host**：`firmware/test/host/test_loop.cpp`，11/11 `RESULT OK`。

## 内容

- 240×135 Gruvbox Dark UI：LOOP / FILE 两页 + 设置浮层；Web 交互预览（`web/index.html`）。
- 固件：ESP32-S3 USB Host MIDI IN/OUT、四通道循环引擎（1–32 小节 / 480 PPQN）、首音起录、按相位叠录、双缓冲合并、本地 I2S 节拍器、SMF Format 0 保存 `/MidiLoop`、文件删除/改名、NVS。
- 构建数据：RAM 209,020 B（63.8%），Flash 671,969 B（21.4%）；合并镜像 `dist/CardputerLooper-8MB.bin`（720.6 KB，从 `0x0`）。
- SHA-256 见 `dist/SHA256SUMS.txt`。

## 未验证 / 已知限制

- USB Host 枚举、FM-1 全双工、ES8311 click、SD 长时读写均未在真机验证；USB Host 仍是实验路径。
- 不与 FM-1 sequencer 锁小节相位；内部 BPM 为主。
- 8192 事件上限；32 小节 32 分单音刚好，密集和弦会截断。
- 叠录一次一条 lane；FILE 页只能删除/改名，不能载入。
- Sloop `ARP 2 → HOLD` 必须 OFF，否则停止后挂音。

详细说明见 [README.md](README.md)，设计评估见 [docs/设计评估.md](docs/设计评估.md)。
