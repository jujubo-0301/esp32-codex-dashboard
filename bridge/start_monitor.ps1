$ErrorActionPreference = 'Stop'

$script = Join-Path $PSScriptRoot 'codex_thread_monitor.py'
$python = 'C:\Espressif\tools\python\v5.5.2\venv\Scripts\python.exe'
$existing = Get-CimInstance Win32_Process -Filter "Name = 'python.exe'" |
    Where-Object { $_.CommandLine -like "*$script*" }
if ($existing) {
    Write-Host 'Codex monitor is already running.'
    exit 0
}

Start-Process -FilePath $python `
    -ArgumentList @('-u', $script) `
    -WorkingDirectory $PSScriptRoot `
    -WindowStyle Hidden

Start-Sleep -Milliseconds 800
Write-Host 'Codex monitor started.'
