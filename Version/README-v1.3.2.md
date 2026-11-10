# CardputerLooper v1.3.2

**文件名：** `CardputerLooper-v1.3.2-app.bin`  
**类型：** app-only（用于 M5 Apps / 启动器安装，不包含 bootloader、分区表、M5 Apps）

## 修复内容

- **Loop 模式残留状态修复**：启动时如果没有已存在的 loop，不再从 NVS 恢复 `LOOP` 开启状态，避免开机后按 `L` 反而把它关掉、看起来“无法录音”。
- **`L` 开启即开始**：无循环先 `ARMED`，有循环直接 `OVERDUB`；到循环末尾自动续录。
- **`R` 在 Loop ARMED 阶段不再取消**：只提示 `PLAY A NOTE`；必须弹第一个 MIDI 音才开始 REC。
- **`R` 在 Loop REC 阶段**：关闭 Loop 模式并停止当前 take。
- **`Fn+C`**：只清当前选中 lane；`C` 仍清整个 loop。
- 启动页 **`v1.3.2`**，1.5 秒。

## 安装

### 推荐：M5 Apps Installer

把文件放 SD 卡，用 M5 Apps 的 **Installer → SD Card** 安装；随后在 Launcher 里选择 `CardputerLooper` 启动。

### 手动 esptool

当前这台机器 M5 Apps 分区表里 CardputerLooper 在 **`0x5A0000`**：

```bash
esptool.py --chip esp32s3 -p COMx -b 460800 write_flash 0x5A0000 CardputerLooper-v1.3.2-app.bin
```

**不要写 `0x1B0000`**：那里现在是 `CardputerWaveL`。分区偏移可能随 M5 Apps 安装变化，优先用 Installer。

## 校验

```text
693622e45a0e1471c70e06354cbab076651814e587bab9843190e28a8a4b81e6  CardputerLooper-v1.3.2-app.bin
```

## 注意

- 固件没有本地音源；Enter 只把 MIDI 发给 FM-1，FM-1 未连接时不会响。
- Cardputer 只有一个 USB-C：接 PC 串口时不能同时接 FM-1。要同时看串口日志，需要 UART0 的 USB-UART 适配器。
- M5 Apps 在 `0x10000`；不要用 8 MB 合并镜像从 `0x0` 烧写。
