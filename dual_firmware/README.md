# 原厂桌面 / 小智 / Codex 三应用方案

当前镜像保留三部分：

- `factory`：原厂桌面和原厂应用
- `ota_0`：原版小智
- `ota_1`：Codex

## 开发板上的长期切换

使用 `GPIO18 / KEY3`，不要按 BOOT：

1. 按住 KEY3 不松开。
2. 用 PWR 让设备开机或重启。
3. 看见开机画面后松开 KEY3，不需要计时。

切换规则：

- 原厂桌面 → Codex
- Codex → 原厂桌面
- 小智 → 原厂桌面

切换结果写入启动选择区，普通重启和断电后仍保持，不是临时跳转。

## 电脑端切换

连接 USB 数据线后，在本目录运行：

```powershell
.\switch-to-codex.ps1
.\switch-to-factory.ps1
```

脚本会自动寻找 ESP32 串口，也可以手动指定：

```powershell
.\switch-to-codex.ps1 -Port COM3
```

只更新按键切换逻辑而不改动应用和用户数据时，运行：

```powershell
.\update-bootloader.ps1 -Port COM3
```

## 验收要求

刷入新的 bootloader 后，必须按以下顺序实机验证：

1. 原厂桌面启动并保持。
2. 长按 KEY3 重启，进入 Codex。
3. 再次长按 KEY3 重启，回到原厂桌面。
4. 从原厂桌面点击小智进入小智。
5. 长按 KEY3 重启，回到原厂桌面。
6. 断电再上电，确认最后选择保持。
