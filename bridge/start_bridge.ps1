$ErrorActionPreference = 'Stop'

$port = 8787
$python = 'C:\Espressif\tools\python\v5.5.2\venv\Scripts\python.exe'
$script = Join-Path $PSScriptRoot 'codex_status_bridge.py'
$existing = Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue
if ($existing) {
    Write-Host 'Status bridge is already running.'
    & (Join-Path $PSScriptRoot 'start_monitor.ps1')
    exit 0
}

Start-Process -FilePath $python `
    -ArgumentList @('-u', $script, '--host', '0.0.0.0', '--port', "$port") `
    -WorkingDirectory $PSScriptRoot `
    -WindowStyle Hidden

Start-Sleep -Milliseconds 2500
if (-not (Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue)) {
    throw 'Status bridge did not start.'
}
Write-Host 'Status bridge started.'
& (Join-Path $PSScriptRoot 'start_monitor.ps1')
