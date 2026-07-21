$ErrorActionPreference = 'Stop'

if (-not (Get-NetFirewallRule -DisplayName 'ESP32 Codex Bridge HTTP' -ErrorAction SilentlyContinue)) {
    New-NetFirewallRule -DisplayName 'ESP32 Codex Bridge HTTP' -Direction Inbound -Action Allow `
        -Protocol TCP -LocalPort 8787 -Profile Private | Out-Null
}
if (-not (Get-NetFirewallRule -DisplayName 'ESP32 Codex Bridge Discovery' -ErrorAction SilentlyContinue)) {
    New-NetFirewallRule -DisplayName 'ESP32 Codex Bridge Discovery' -Direction Inbound -Action Allow `
        -Protocol UDP -LocalPort 8788 -Profile Private | Out-Null
}
Write-Host 'ESP32 Codex Bridge firewall rules are enabled.'
