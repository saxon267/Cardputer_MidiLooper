# MidiLooper — 速查表

## 走带

| 键 | 动作 |
|---|---|
| `R` / `Space` | 录音 / 停止录音；有循环时 = 叠录 |
| `Enter` | 播放 / 停止 |
| `S` | 保存 `/MidiLoop/LOOP_xxxx.MID` |
| `C` | 清空当前轨道（不删除 `.mid` 文件） |
| `U` | 撤销最后一次叠录（叠录中 = 取消当前 take） |
| `Fn` + `1..4` | Mute / Unmute CH1 / CH2 / CH3 / CH10 |
| `M` | 节拍器开 / 关 |
| `G` | 设置浮层 |
| `Tab` | LOOP ↔ FILE |
| `Bksp` | 停止 / 返回 / 关闭浮层 |

## 参数

| 键 | 动作 |
|---|---|
| `,` / `/` | BPM −1 / +1 |
| `Fn` + `,` / `/` | BPM −5 / +5 |
| `-` / `=` | BPM −1 / +1 |
| `;` / `.` | 选择 lane（CH1/2/3/10） |
| `Fn` + `;` / `.` | 小节数 + / −（1/2/4/8/16/32） |
| `1` / `2` / `3` / `4` | 直接选 CH1 / CH2 / CH3 / CH10 |

## FILE 页

| 键 | 动作 |
|---|---|
| `;` / `.` | 选择 |
| `D` | 删除（`Enter` 确认，`Bksp` 取消） |
| `N` | 改名（键入选字；`Enter` 确认，`Bksp` 退格，`Tab` 取消） |
| `Enter` | 载入选中的 `.mid` 并播放（文件保留） |
| `Tab` / `Bksp` | 回 LOOP |

## 设置浮层

| 键 | 动作 |
|---|---|
| `;` / `.` | 选行（BPM / BARS / QUANT / SWING / CLICK / COUNT-IN） |
| `,` / `/` | 改值（QUANT 切换网格，SWING ±5） |
| `Enter` | 开关 CLICK/COUNT-IN；BPM/BARS/QUANT/SWING 行关闭 |
| `Bksp` / `G` / `Tab` | 关闭 |

## 状态

`STOP` → 按 R → `ARMED`（count-in 4 拍）→ 首个 MIDI → `REC` → 录满/再按 R → `PLAY`。
`PLAY` 按 R → `OVERDUB`（立即按当前相位）→ 再按 R/循环末尾 → `PLAY`。
`U` → 撤销最后一次叠录；`C` → 清空当前轨道（不删文件）；`Fn` + `1..4` → Mute。

Quantize 默认 1/16（120 ticks），SWING 默认 50；两者都在设置浮层里改。

## Sloop 设置（必须）

- `GLO → MIDI`：CH1=1，CH2=2，CH3=3，CH4=10。
- `GLO → MIDI 2`：`OUT=SEQ` 或 `KEYS`；`IN=NOTES`。
- `ARP MODE=OFF`；`ARP 2 → HOLD=OFF`。
- `DR MAP=GM`。
- `SYNC=INT`；`CLOCK=OUT` 可选。

## 排障

| 现象 | 检查 |
|---|---|
| 顶栏 USB 红 | Host 初始化失败；重新上电，确认 build 是 host |
| USB 黄 | 枚举到设备但没有 MIDI OUT；查描述符/接口 |
| USB 灰 | 没识别 FM-1；换数据线/接口，FM-1 开机 |
| 按 R 没反应 | `CONNECT FM1`；确认 `usbConnected` 绿 |
| 录音没音符 | Sloop `OUT=SEQ`（录 sequencer）或 `OUT=KEYS`（录键盘） |
| 停止后挂音 | Sloop `ARP 2 → HOLD=OFF` |
| 保存失败 | SD 是否 FAT32；`/MidiLoop` 是否可写 |
| 叠录后节奏变了 | 确认没在录音中改 BPM；先 `C` 再改 BARS |
