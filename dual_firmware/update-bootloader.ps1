param(
    [string]$Port = ""
)

$idftools = if ($env:IDF_PATH) { $env:IDF_PATH } else { "" }
if (-not $idftools) { throw "请先配置 IDF_PATH，例如 E:\Espressif\v5.5.2\esp-idf" }
$python = if ($env:IDF_PYTHON_ENV_PATH) { Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts\python.exe' } else { (Get-Command python -ErrorAction SilentlyContinue).Source }
if (-not $python) { throw "找不到 Python。请配置 IDF_PYTHON_ENV_PATH 或将 Python 加入 PATH。" }
$esptool = Join-Path $idftools 'components\esptool_py\esptool\esptool.py'
$project = Split-Path $PSScriptRoot -Parent
$bootloader = Join-Path $project 'firmware\esp-idf\06_codex_brookesia\build\bootloader\bootloader.bin'

if ([string]::IsNullOrWhiteSpace($Port)) {
    $candidate = Get-CimInstance Win32_SerialPort -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match 'USB|JTAG|ESP32|CP210|CH340' } |
        Select-Object -First 1 -ExpandProperty DeviceID
    if ($candidate) { $Port = $candidate }
}
if ([string]::IsNullOrWhiteSpace($Port)) {
    throw "ESP32 serial port not found. Connect the USB data cable or use -Port COMx."
}
if (-not (Test-Path -LiteralPath $bootloader)) {
    throw "New bootloader.bin not found. Run idf.py build first."
}

& $python $esptool --chip esp32s3 --port $Port --before default_reset --after hard_reset `
    write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB 0x0 $bootloader
if ($LASTEXITCODE -ne 0) {
    throw "Bootloader update failed."
}
Write-Host "Bootloader updated. Factory, Xiaozhi, Codex, and user data were not changed."
