# ESP32-S3 Codex Dashboard

给 Waveshare **ESP32-S3-Touch-AMOLED-2.16** 使用的 Codex 任务状态面板：在小屏幕上显示任务、进度、日志、资源指标、额度和电脑端连接状态。

第一次使用请直接看：[中文完整教程](docs/QUICK_START_CN.md)。教程包含刷写、Wi‑Fi、电脑桥接、双固件和故障恢复。

## 仓库内容

- `firmware/esp-idf/06_codex_brookesia/`：Codex 固件源码
- `bridge/`：电脑端状态桥接和任务监视器
- `dual_firmware/`：已有原厂固件时的双固件切换脚本
- `docs/QUICK_START_CN.md`：面向新用户的完整操作步骤

## 最短流程

1. 下载 Releases 中的预编译包，或按教程安装 ESP-IDF 后自行编译。
2. 用 USB 数据线连接开发板，在设备管理器确认 `COMx`。
3. 备份原厂固件后，按教程烧录三个文件：bootloader、partition-table、Codex app。
4. 开机进入设置，点“WiFi 设置”；手机连接开放热点 `Codex-Setup`，打开 `http://192.168.4.1`，选择附近的 2.4GHz Wi‑Fi 并输入密码。
5. 电脑启动 `bridge/codex_status_bridge.py` 和 `bridge/codex_thread_monitor.py`。电脑与开发板必须在同一局域网。

## 重要边界

本仓库发布的是 Codex 面板和桥接程序，不包含 Waveshare 原厂桌面、小智固件、原厂资源分区或任何个人 Wi‑Fi 密码。想保留原厂桌面和小智，请先完整备份，并按教程的“双固件”章节操作；不要把个人备份上传到 GitHub。

## 开源与许可证

本项目代码使用仓库中的许可证。Waveshare、ESP-IDF、Brookesia 及其他依赖的版权和许可证仍然适用，详见 `THIRD_PARTY_NOTICES.md`。
