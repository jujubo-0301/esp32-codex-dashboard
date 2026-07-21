# ESP32 Codex display bridge

The firmware is installed once. After that, the board reads the current
status from the PC over Wi-Fi; task status changes do not require a rebuild or
another flash.

Run `start_codex_display.ps1` once after Windows starts. It starts both the
local HTTP bridge and the Codex session monitor. The monitor follows the most
recently active Codex task, even when you switch to another project, and
publishes only a compact state summary:

- `running`: Codex is processing a turn
- `done`: the turn completed
- `error`: the turn was interrupted
- `idle`: no active turn

The board polls `GET /status` every two seconds. The raw conversation and tool
output stay on the PC and are never sent to the ESP32.

`publish_status.py` remains available for manual testing, but it is no longer
needed during normal use.
