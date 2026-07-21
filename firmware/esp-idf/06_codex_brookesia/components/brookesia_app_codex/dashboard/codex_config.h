#pragma once

// Optional first-boot fallback. Leave these empty to use the Wi-Fi profile
// saved by the factory firmware (the default and recommended mode).
#define CODEX_WIFI_SSID ""
#define CODEX_WIFI_PASSWORD ""

// The bridge is discovered automatically over UDP 8788. This URL is only an
// optional static fallback for installations that cannot use discovery.
#define CODEX_STATUS_URL ""
