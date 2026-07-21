param(
    [string]$Port = ""
)

$env:IDF_PATH = if ($env:IDF_PATH) { $env:IDF_PATH } else { "" }
if (-not $env:IDF_PATH) { throw "请先配置 IDF_PATH，例如 E:\Espressif\v5.5.2\esp-idf" }
$env:PYTHONPATH = "$env:IDF_PATH\components\partition_table"
$python = if ($env:IDF_PYTHON_ENV_PATH) { Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts\python.exe' } else { (Get-Command python -ErrorAction SilentlyContinue).Source }
if (-not $python) { throw "找不到 Python。请配置 IDF_PYTHON_ENV_PATH 或将 Python 加入 PATH。" }
$otatool = "$env:IDF_PATH\components\app_update\otatool.py"

if ([string]::IsNullOrWhiteSpace($Port)) {
    $candidate = Get-CimInstance Win32_SerialPort -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match 'USB|JTAG|ESP32|CP210|CH340' } |
        Select-Object -First 1 -ExpandProperty DeviceID
    if ($candidate) { $Port = $candidate }
}
if ([string]::IsNullOrWhiteSpace($Port)) {
    throw "ESP32 serial port not found. Connect the USB data cable or use -Port COMx."
}

& $python $otatool --port $Port switch_ota_partition --slot 1
if ($LASTEXITCODE -ne 0) {
    throw "Codex switch failed. Check the board port: $Port"
}
Write-Host "Codex selected. The board is rebooting."
