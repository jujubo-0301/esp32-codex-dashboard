param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [int]$Baud = 460800
)

$ErrorActionPreference = "Stop"
$packageDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$bootloader = Join-Path $packageDir "bootloader.bin"
$partition = Join-Path $packageDir "partition-table.bin"
$application = Join-Path $packageDir "esp-brookesia.bin"

foreach ($file in @($bootloader, $partition, $application)) {
    if (-not (Test-Path -LiteralPath $file)) {
        throw "缺少文件：$file。请把三个固件文件放到 tools 文件夹后再运行。"
    }
}

python -m esptool --chip esp32s3 --port $Port --baud $Baud `
    --before default-reset --after hard-reset `
    write-flash --flash-mode dio --flash-freq 80m --flash-size 16MB `
    0x0 $bootloader 0x8000 $partition 0x10000 $application

if ($LASTEXITCODE -ne 0) {
    throw "刷写失败，退出码：$LASTEXITCODE"
}

Write-Host "刷写完成。看到 Hash of data verified 后，可以拔掉 USB 并重启开发板。" -ForegroundColor Green
