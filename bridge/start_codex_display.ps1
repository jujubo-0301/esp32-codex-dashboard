$ErrorActionPreference = 'Stop'

$bridge = Join-Path $PSScriptRoot 'start_bridge.ps1'
$monitor = Join-Path $PSScriptRoot 'start_monitor.ps1'
& $bridge
& $monitor
Write-Host 'ESP32 Codex display link is ready.'
