# K-9 Spool Manager — Project Notes

**Built by Joe the Builder** — ESP32-2432S028R (CYD), ILI9341 320×240 via TFT_eSPI, XPT2046 touch, PN532 NFC over I2C. Three tag protocols: QIDI (Mifare Classic), OpenSpool U1 (NTAG215/NDEF JSON), Anycubic ACE (NTAG215 raw blocks). Plus a fourth capability: sending filament data directly to a Snapmaker U1 over WiFi (no tag required).

Reference repos used throughout development:
- `jOethebuilder/ACE-RFID` (Joe's fork) — ACE protocol + Anycubic app behavior
- `DnG-Crafts/U1-RFID` — same author's Snapmaker U1 app, confirmed shared color list and Moonraker send mechanism
- `Niko11111/SpoolmanScale` — reference for planned Spoolman integration (UID-based linking pattern)

Full protocol-level detail has been split out into `docs/protocols/` — this file stays the working build log: what's done, what's confirmed, what's been tried and abandoned, and why.

---

## Current State — Confirmed Working

- **WiFi**: full flow — scan, tap SSID, on-screen keyboard (plaintext password, 3-row layout) for locked networks, open networks connect instantly. Credentials persist via `Preferences` (NVS, namespace `k9wifi`), auto-reconnect on boot. FORGET button clears saved creds. `WiFi.disconnect(true,true)` + `WiFi.mode(WIFI_STA)` + `esp_wifi_set_ps(WIFI_PS_NONE)` reset before every connect/scan (stability fix).
- **Settings menu**: vertical button list. WIFI, U1 CONNECTION, BACKLIGHT, NFC STATUS all wired and working. Touch Calibration / Firmware Info / Factory Reset are stub rows or partially wired — check current `.ino` for latest state.
- **QIDI**: read/write confirmed working. 24-color table verified 100% accurate against official wiki — no changes needed.
- **OpenSpool U1**: read/write confirmed working (NDEF JSON, `color_hex` field). Color picker currently uses the QIDI 24-color list as a placeholder — **confirmed incorrect**, should be swapped for the 35-color ARGB list (see `docs/protocols/openspool-schema.md`).
- **Anycubic ACE**: tag write confirmed working on real hardware. All 5 original protocol bugs fixed (page 30 never written, page 31 constant missing, alpha byte wrong, brand/material fields too short, page 4 flag bytes wrong). Color system fully reworked to use the real 35-color ARGB preset list. Full detail: `docs/protocols/anycubic-ace-n033.md`.
- **U1 Connection settings screen**: host entry via keyboard flow, NVS persistence (namespace `k9u1`), live TEST CONNECTION against Moonraker's `/printer/info`. Confirmed working on real hardware — host persists after reboot, TEST CONNECTION returns CONNECTED.
- **U1 SEND feature**: SEND button on OpenSpool entry screen → slot picker (1–4) → fires `SET_PRINT_FILAMENT_CONFIG` over Moonraker's HTTP API. Full protocol detail: `docs/protocols/u1-moonraker-macros.md`.

---

## Known Issue: U1 SEND Blocked by OpenRFID "Official" Filament Lock

**Confirmed on real hardware**: SEND fails with `[print_task_config] filament_config, official filament, not configurable!` when targeting a slot where OpenRFID has actively detected and recognized a tag as "official."

**Root cause (confirmed via paxx12 Extended Firmware docs)**: the U1's `rfid` setting has three modes:
- `snapmaker` (default) — Snapmaker's own built-in reader
- `openrfid` — extended detection (AnyCubic/Bambu/Creality/etc.) — **this is what triggers the lock**
- `external` — disables built-in readers entirely, meant for external readers

OpenRFID mode treats a scanned tag as protected/official data, and blocks `SET_PRINT_FILAMENT_CONFIG` from overwriting it. This is presumably a data-integrity safeguard, not a bug.

**Confirmed workaround**: SEND works on slots with no tag currently detected. Switching to `external` mode would remove the conflict entirely, but disables the printer's own automatic tag detection system-wide — a real tradeoff, not a quick fix. Decision on which mode to standardize on: **pending**.

---

## Known Gaps / Not Yet Built

- **U1 SEND payload is incomplete** — only sends manufacturer/material/color via `SET_PRINT_FILAMENT_CONFIG`. No temp range (that macro doesn't support it for standard ToolHeads — only the ACE-channel variant, `SET_FILAMENT_CONFIG`, does). No `subtype` field yet either.
- **No filament-sensor/print-state safety check before SEND** — TinkerBarn's equivalent feature checks for a printer mid-print or filament already loaded and asks for confirmation before overwriting. K-9 does not do this yet; SEND fires immediately with no check.
- **OpenSpool U1 tag schema is missing fields** confirmed present in the real Extended Firmware spec: `version`, `subtype`, `alpha`, `additional_color_hexes`. See `docs/protocols/openspool-schema.md` for the full confirmed schema and a reference material/subtype database pulled from U1-RFID's own source.
- **Spoolman integration** — planned, not built. Reference pattern from `SpoolmanScale`: link tags to Spoolman entries via the tag's hardware UID stored in Spoolman's `extra.tag` field, rather than parsing/duplicating tag JSON content into Spoolman.
- **ACE_U1 (physical ACE-into-U1 hardware integration)** — explicitly out of scope for Mark 1, noted as a Mark 2 goal. This is unrelated to NFC tag writing — it's Klipper config/plumbing work to wire a standalone Anycubic ACE unit into a U1 as a filament source.
- **Grid-based picker UI** — 3×3 grid with pagination, already used for material/manufacturer/color selection across QIDI/OpenSpool/Anycubic entry screens. Working and confirmed.

---

## Key Learnings & Principles

- **Moonraker send mechanism confirmed**: plain Klipper G-code macros via HTTP GET, port 7125, `/printer/gcode/script?script=`. Query-style commands (read current state) go over WebSocket instead, since they need to wait for an async response.
- **ACE protocol has no network write/set-material method** — confirmed via official Anycubic source (N033 protocol doc, itself sourced from Anycubic's own `Kobra3` firmware repo), and independently confirmed by `multiACE`'s own FAQ giving the identical answer. This is a dead end that will not change without a new discovery.
- **The U1 SEND macros (`SET_PRINT_FILAMENT_CONFIG`/`SET_FILAMENT_CONFIG`) are paxx12 Extended Firmware additions**, not part of Anycubic's protocol at all — they only work because the U1's own Klipper/Moonraker layer is answering the request, not the ACE hardware itself.
- **35-color ARGB preset list is byte-for-byte identical** between ACE-RFID's `Utils.java` and U1-RFID's `Utils.java` — confirmed by directly cloning and reading both sources, not just READMEs. No separate palette needed for OpenSpool U1.
- **NVS credential staleness** causes WiFi connection failures; always flush before connect/scan.
- **VSPI/HSPI** dropped in ESP32 Arduino core 3.x — use `#ifndef` compatibility defines.
- **Confirm UI behavior in plain language before writing any code** — skipping this step led to building and scrapping an entire NVS custom-color-slots system early on.
- **When GitHub search/robots.txt blocks direct browsing of a repo's deeper file paths**, cloning via `git clone` over a plain network connection routes around it cleanly — used successfully to pull U1-RFID's actual Java source when GitHub's web UI wouldn't serve it.

---

## Approach & Patterns

- **Workflow**: Confirm behavior in plain language first (with mockups when helpful) → then code. Never skip to code before design is settled.
- **Code delivery format**: Exact `Find: X / Replace with: Y` blocks only — no prose explanations of what the code does, no ellipsis placeholders, no partial snippets. Blocks must be complete and paste-ready.
- **Multi-question handling**: If Joe answers only some parts of a multi-part question, follow up on unanswered parts rather than assuming defaults and proceeding.
- **Repo structure**: `README.md`, `CHANGELOG.md`, `LICENSE` at root; `src/` for the actual `.ino` + `User_Setup.h`; `web-flasher/` for the ESP Web Tools browser-flashing setup (manifest.json + compiled `.bin` files + index.html); `docs/` for this file plus `docs/protocols/` for deep protocol reference docs.
- **Web flasher maintenance**: after firmware changes, re-export via Arduino IDE's "Export Compiled Binary," replace the `.bin`/`.bootloader.bin`/`.partitions.bin` files in `web-flasher/` (never `boot_app0.bin` — that one never changes), bump the version number in `manifest.json`, commit and push.
- **Reference repo**: `jOethebuilder/ACE-RFID` used as ground-truth for ACE protocol details.

---

## Tools & Resources

- **Hardware**: ESP32-2432S028R (CYD), PN532 NFC reader (I2C), ILI9341 display, XPT2046 touch
- **Firmware environment**: Arduino/C++, TFT_eSPI, ESP32 Preferences (NVS), HTTPClient
- **Reference implementations**: `jOethebuilder/ACE-RFID`, `DnG-Crafts/U1-RFID`, `Niko11111/SpoolmanScale`, TinkerBarn BoxRFID-Touch V4.2
- **Printer integration**: Moonraker HTTP API + WebSocket, paxx12 Extended Firmware (Klipper macros)
- **Web flashing**: ESP Web Tools v10, hosted via GitHub Pages
