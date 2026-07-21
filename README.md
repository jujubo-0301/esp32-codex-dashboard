# ESP32-S3 Codex Dashboard

这是一个运行在 Waveshare ESP32-S3-Touch-AMOLED-2.16 上的 Codex 状态面板。
它通过局域网桥接程序显示电脑端任务状态、运行进度、资源指标、日志和网络状态。

## 目录

- `firmware/esp-idf/06_codex_brookesia/`：ESP-IDF 固件源码
- `bridge/`：电脑端状态桥接程序
- `dual_firmware/`：双固件切换脚本和分区表

原厂桌面、小智固件、个人 Wi-Fi 配置和任何预编译固件均不包含在本仓库中。

## 编译固件

需要 ESP-IDF 5.5.x、Python 环境和适用于 ESP32-S3 的工具链。

```powershell
$env:IDF_PATH = "你的 ESP-IDF 路径"
$env:IDF_TOOLS_PATH = "你的 ESP-IDF 工具路径"
$env:IDF_PYTHON_ENV_PATH = "你的 ESP-IDF Python 环境路径"

cd firmware/esp-idf/06_codex_brookesia
idf.py set-target esp32s3
idf.py build
```

首次编译时，ESP-IDF 会根据 `dependencies.lock` 下载依赖组件。

## Wi-Fi

固件默认优先读取原厂固件保存的共享 Wi-Fi 配置，不需要把密码写进源码。
如果没有保存的配置，可以在本地未提交的 `codex_config.h` 中填写临时测试值；
不要把真实密码提交到 GitHub。

## 电脑端桥接

在电脑上运行：

```powershell
cd bridge
python codex_status_bridge.py
```

另开一个终端启动状态监视器：

```powershell
python codex_thread_monitor.py
```

桥接 HTTP 使用 8787 端口，设备发现使用 UDP 8788 端口。设备和电脑必须在同一局域网。

## 双固件切换

切换脚本需要先配置 `IDF_PATH` 和 `IDF_PYTHON_ENV_PATH`：

```powershell
$env:IDF_PATH = "你的 ESP-IDF 路径"
$env:IDF_PYTHON_ENV_PATH = "你的 ESP-IDF Python 环境路径"
.\dual_firmware\switch-to-codex.ps1 -Port COMx
```

日常切换也可以使用开发板按键，不需要重新编译。

## 开源范围

本仓库只发布 Codex 面板和桥接相关代码。Waveshare、ESP-IDF、Brookesia 及其他依赖的
版权和许可证仍然适用，详见 `THIRD_PARTY_NOTICES.md`。
