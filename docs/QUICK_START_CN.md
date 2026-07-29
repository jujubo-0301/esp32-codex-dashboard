# ESP32-S3 Codex Dashboard 完整教程

这份教程针对 Windows 10/11 和 Waveshare ESP32-S3-Touch-AMOLED-2.16。第一次刷写前请完整读完“重要警告”。

## 一、你需要准备什么

- Waveshare ESP32-S3-Touch-AMOLED-2.16 开发板
- 能传数据的 USB-C 数据线（只能充电的线不能刷写）
- Windows 10/11 电脑
- 手机，用来配置 Wi‑Fi
- 2.4GHz Wi‑Fi。ESP32-S3 不支持 5GHz Wi‑Fi

## 二、重要警告：先备份

普通“Codex 单固件”刷写会覆盖开发板当前的应用和分区表。它不包含原厂桌面和小智固件。想保留原厂功能，必须先备份完整 16MB Flash：

```powershell
python -m esptool --chip esp32s3 --port COM3 --baud 460800 read-flash 0x0 0x1000000 factory-backup.bin
```

把 `COM3` 换成你的端口。备份文件只保存在本地，不要上传或发给别人，因为里面可能包含设备数据和 Wi‑Fi 信息。

恢复备份时使用：

```powershell
python -m esptool --chip esp32s3 --port COM3 --baud 460800 write-flash 0x0 factory-backup.bin
```

## 三、确认 COM 端口

1. 用 USB 数据线连接开发板。
2. 打开“设备管理器”。
3. 展开“端口（COM 和 LPT）”。
4. 记下 `USB Serial/JTAG` 或 `USB 串行设备` 后面的端口，例如 `COM3`。
5. 如果没有端口，换一根数据线、换 USB 接口，或重新安装 Espressif USB 驱动。

## 四、路线 A：使用预编译固件（推荐）

在 GitHub 仓库的 Releases 下载最新 Codex 固件包。包内应有三个文件：

```text
bootloader.bin
partition-table.bin
esp-brookesia.bin
```

先安装 ESP-IDF 自带的 Python 环境，或者安装 `esptool`：

```powershell
py -3 -m pip install esptool
```

在三个文件所在目录打开 PowerShell，执行：

```powershell
python -m esptool --chip esp32s3 --port COM3 --baud 460800 --before default-reset --after hard-reset write-flash --flash-mode dio --flash-freq 80m --flash-size 16MB 0x0 bootloader.bin 0x8000 partition-table.bin 0x10000 esp-brookesia.bin
```

看到 `Hash of data verified` 才算刷写完成。不要在刷写过程中拔线。

## 五、路线 B：从源码编译

### 1. 安装工具

在 VS Code 安装扩展 **Espressif IDF**，选择 ESP-IDF 5.5.x，并让扩展安装 Python、CMake、Ninja 和 ESP32-S3 工具链。安装结束后打开 ESP-IDF PowerShell。

### 2. 编译

```powershell
cd firmware/esp-idf/06_codex_brookesia
idf.py set-target esp32s3
idf.py build
```

成功后使用以下文件刷写：

```text
build/bootloader/bootloader.bin
build/partition_table/partition-table.bin
build/esp-brookesia.bin
```

### 3. 从源码直接刷写

```powershell
idf.py -p COM3 flash
```

首次编译会下载依赖组件，需要保持网络连接。

## 六、第一次 Wi‑Fi 配置

1. 开机进入 Codex 面板。
2. 进入“设置”，点第一行“WiFi 设置”。右侧应保持“配置中”。
3. 手机 Wi‑Fi 列表连接开放热点 `Codex-Setup`，不需要密码。
4. 如果手机没有自动弹出网页，手动打开 `http://192.168.4.1`。
5. 页面会把开发板已保存的网络放在最前面，并列出附近扫描到的 2.4GHz Wi‑Fi。
6. 选择网络，只填写密码，点击“保存并连接”。
7. 等待开发板重新连接。连接成功后设置页显示“已连接”。

更换地点时不需要重新刷固件：重复以上步骤即可。开发板不会读取手机的 Wi‑Fi 历史，只能读取自己保存的网络并扫描附近网络。

## 七、电脑端桥接

电脑与开发板连接同一个局域网后，在仓库根目录打开第一个 PowerShell：

```powershell
cd bridge
python codex_status_bridge.py
```

再打开第二个 PowerShell：

```powershell
cd bridge
python codex_thread_monitor.py
```

桥接服务使用 TCP `8787`，设备发现使用 UDP `8788`。Windows 防火墙弹窗出现时允许“专用网络”。开发板设置页显示“桥接在线”后，任务状态才会实时同步。

## 八、保留原厂桌面和小智

如果你想要“原厂桌面 + 小智 + Codex”：

1. 先备份完整 16MB Flash。
2. 不要把原厂固件上传到 GitHub。
3. 准备 Waveshare 原厂固件和正确的分区表。
4. 按仓库 `dual_firmware/` 中的脚本和说明操作。
5. 日常切换使用开发板的 KEY 按键，不要长时间按 BOOT。

本仓库无法凭空恢复你设备里的原厂桌面和小智数据。

## 九、常见问题

### 找不到 `Codex-Setup`

确认已经在开发板设置中点击“WiFi 设置”，并等待 3 秒。手机必须开启 Wi‑Fi；ESP32-S3 只扫描 2.4GHz。

### 手机连接热点但没有网页

手动打开 `http://192.168.4.1`。不同手机系统不一定自动弹出 captive portal，这不影响配置页本身。

### 页面显示“已连接”但电脑端离线

确认电脑和开发板连接的是同一个局域网；关闭 VPN；允许 TCP 8787 和 UDP 8788 通过 Windows 专用网络防火墙。

### 刷写时报 COM 口被占用

关闭 VS Code 串口监视器、ESP-IDF Monitor、Arduino Serial Monitor 和其他串口软件后重试。

### 刷写后白屏或无法启动

确认使用的是 ESP32-S3 固件、16MB Flash 参数和同一套 bootloader、partition-table、app 文件。必要时使用之前保存的完整备份恢复。

## 十、成功标准

完成后应同时满足：

- 开发板能显示 Codex 页面；
- 设置页显示 Wi‑Fi“已连接”；
- 电脑端桥接显示在线；
- Codex 任务变化能在屏幕上更新；
- 更换 Wi‑Fi 后无需重新编译或改源码。
