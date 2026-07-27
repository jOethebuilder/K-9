# Changelog

All notable changes to K-9 Spool Manager are documented here. Format is one entry per fix/feature, newest first.

## Unreleased / In Progress

### Added
- **U1 SEND feature** — new SEND button on the OpenSpool U1 entry screen. Opens a slot picker (1–4), then pushes manufacturer/material/color to the selected Snapmaker U1 ToolHead over WiFi using the `SET_PRINT_FILAMENT_CONFIG` Klipper macro via Moonraker's HTTP API. Does not yet include temp range data.
- **U1 Connection settings screen** — new Settings row, "U1 CONNECTION". Lets you enter/save the U1's host/IP (persisted via NVS, namespace `k9u1`) and run a live TEST CONNECTION check against Moonraker's `/printer/info` endpoint.

### Fixed
- **Anycubic ACE color write bug** — `tagData.r/g/b` were never set from the currently-selected preset/custom color before `aceWriteTag()` ran, so the tag could be written with stale or blank color data even though the entry screen displayed the correct hex. Write path now sources color from the same variables the display uses.
- **Settings screen brace mismatch** — a missing closing brace on the `SCR_SETTINGS` touch handler (later duplicated by accident during a subsequent edit) caused every case after it in the main touch-handling switch to fail to compile.
- **`drawOpenSpoolEntry()` missing read-result branch** — an edit accidentally dropped the entire "tag read result" display branch, breaking the function's brace structure and causing a large cascade of compile errors.
- **OpenSpool entry button hit-region mismatch** — BACK/SAVE button tap regions weren't updated after the button row was resized to fit the new SEND button, causing SEND taps to land in SAVE's old hit box and trigger the tag-write flow instead.

## Prior Work (pre-changelog, see `docs/PROJECT_NOTES.md` for full detail)

- WiFi: full scan/connect/persist/auto-reconnect flow, with NVS state-clearing fix for connection stability
- Anycubic ACE: 5 protocol bugs fixed (page 30/31 data, alpha byte, brand/material field width, page 4 flags), full 35-color ARGB preset system replacing incorrect nearest-name-match approach
- Screensaver, Settings stubs (NFC Status test, Backlight presets), manual READ hold with live polling
- QIDI and OpenSpool entry screens fully wired with grid-picker UI for manufacturer/material/color selection
- Presence-tracking fixes (`qidiTagPresent`, `osTagPresent`, `aceTagPresent`) to prevent scan-starving the touch loop
