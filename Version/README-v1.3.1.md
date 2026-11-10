# CardputerLooper v1.3.1

**文件名：** `CardputerLooper-v1.3.1-app.bin`  
**类型：** app-only（用于 M5 Apps / 启动器安装，不包含 bootloader、分区表、M5 Apps）

## 安装

### 推荐：M5 Apps Installer

把 `CardputerLooper-v1.3.1-app.bin` 放到 SD 卡，用 M5 Apps 的 **Installer → SD Card** 安装；安装后从 M5 Apps 的 Launcher 选择 `CardputerLooper` 启动。

### 手动 esptool

当前这台设备的 CardputerLooper 分区是 **`0x5A0000`**（M5 Apps 安装后分区表会变化，**不要再用 `0x1B0000`**，那里现在是 `CardputerWaveL`）：

```bash
esptool.py --chip esp32s3 -p COMx -b 460800 write_flash 0x5A0000 CardputerLooper-v1.3.1-app.bin
```

只写 app 分区，不碰 `0x10000` 的 M5 Apps 和 bootloader。

## 版本标识

- 启动页显示 **`v1.3.1`**，停留 **1.5 秒**。
- Loop 模式：
  - `L` 开启即开始（无循环先 ARM，有循环直接 overdub），关闭即结束；
  - Loop 录音/ARMED 中按 `R` = 关闭 Loop 模式并停止当前 take；
  - 非 Loop 模式 `R` 仍正常手动录音。
- `Fn+C`：只清空当前选中 lane。
- `C`：清空整个 loop，不删除 `.mid` 文件。

## 校验

```text
818c20cf42550bb86b9aecac5ba9555cc12d2a9e652e521a4f0103788529ba35  CardputerLooper-v1.3.1-app.bin
```

## 注意

- 固件没有本地音源，Enter 只把 MIDI 发给 FM-1；FM-1 未连接时不会响。
- Cardputer 只有一个 USB-C：接 PC 串口时不能同时接 FM-1。要同时看串口日志，需要 UART0 的 USB-UART 适配器。
- M5 Apps 保持在 `0x10000`；不要用 8 MB 合并镜像从 `0x0` 烧写。
