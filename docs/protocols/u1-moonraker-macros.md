# Snapmaker U1 — Sending Filament Data over WiFi

## Summary

The Snapmaker U1 runs Klipper + Moonraker. paxx12's Extended Firmware adds custom G-code macros that accept filament data (vendor/type/subtype/color, and for ACE channels, full temp/diameter/weight data) and apply it directly to a ToolHead or ACE channel — **no NFC tag required**. This is a genuine network write path, confirmed by cloning and reading DnG-Crafts' `U1-RFID` Android app source directly (`Utils.java`), and independently confirmed as a real, working feature by TinkerBarn's BoxRFID-Touch firmware (V4.2/4.2.1 "Tag senden" feature) and by U1-RFID's own v5 changelog ("Add ability to write filament data directly to the printer").

This is separate from, and not to be confused with, the Anycubic ACE's own native protocol — see `anycubic-ace-n033.md`. That protocol has no write path. This one does, but only exists on the U1 side (paxx12 Extended Firmware), not on a stock/standalone Anycubic printer.

---

## Transport

All commands go through Moonraker's standard HTTP API — nothing custom, nothing proprietary.

### Fire-and-forget commands (set/clear)

```
GET http://<u1host>:7125/printer/gcode/script?script=<URL-encoded G-code>
```

Response body is JSON, e.g. `{"result":"ok"}`. Check for `result == "ok"`.

### Query-style commands (read current filament, sensor status)

These need to wait for an async response, so they go over a WebSocket instead:

```
ws://<u1host>:7125/websocket
```

Send JSON-RPC:
```json
{"jsonrpc": "2.0", "method": "printer.gcode.script", "params": {"script": "<cmd>"}, "id": <ms-timestamp>}
```
Wait for a message containing `"method": "notify_gcode_response"`.

---

## Confirmed G-code Macro Signatures

### Standard ToolHead (no ACE)
```
SET_PRINT_FILAMENT_CONFIG CONFIG_EXTRUDER=<n> VENDOR='<v>' FILAMENT_TYPE='<t>' FILAMENT_SUBTYPE='<s>' FILAMENT_COLOR_RGBA=<hex>
```
This is what K-9's OpenSpool SEND feature currently uses. Note: **no temperature fields** — temps set on K-9's entry screen are not transmitted via this macro.

### ACE Channel (ACE unit wired into U1 via SnapAce/U1-Ace/multiACE)
```
SET_FILAMENT_CONFIG CHANNEL=<n> VENDOR='<v>' TYPE='<t>' SUBTYPE='<s>' COLOR=<hex> ALPHA=<a> OFFICIAL=<o> LENGTH=<l> DIAMETER=<d> WEIGHT=<w> EXT_TEMP_MIN=<> EXT_TEMP_MAX=<> BED_TEMP_MIN=<> BED_TEMP_MAX=<>
```
Full field set, including temps — not yet used by K-9, but available if ACE-via-U1 support is added later.

### Other confirmed macros
- Clear: `FILAMENT_DT_CLEAR CHANNEL=<n>`
- Query current: `FILAMENT_DT_QUERY CHANNEL=<n>` or `GET_PRINT_TASK_CONFIG` (WebSocket)
- Sensor check: `QUERY_FILAMENT_SENSOR Sensor=e<n>_filament` (WebSocket)
- Dryer: `ACE_START_DRYING TEMPERATURE=<t> DURATION=<d>` / `ACE_STOP_DRYING`

---

## Confirmed Issue: OpenRFID Blocks SEND on "Official" Slots

**Real-world error hit during testing**, confirmed both on the printer's own screen and in Fluidd's console:
```
003-0522-0000-0000 [print_task_config] not allowed to set extruder map during printing!
[print_task_config] filament_config, official filament, not configurable!
```

**Root cause**: the U1's Extended Firmware `rfid` setting (configured via Firmware Config → Filament Detection) has three modes:

| Mode | Behavior |
|---|---|
| `snapmaker` (default) | Snapmaker's own built-in RFID reader only |
| `openrfid` | Extended detection — adds AnyCubic, Bambu Lab, Creality, TigerTag tag support |
| `external` | Disables built-in readers entirely — for external readers (e.g. `wasikuss/snapmaker-u1-remote-rfid-reader`) |

When running in **`openrfid`** mode, any slot where the printer has actively read and recognized a tag is treated as "official" — Klipper's `print_task_config` refuses to let `SET_PRINT_FILAMENT_CONFIG` overwrite it. This is presumably a data-integrity safeguard (so a macro call can't silently override RFID-verified ground truth), not a bug.

**Confirmed workaround**: SEND succeeds on slots with **no tag currently detected**. Testing on an empty slot (or one with `source: no material`) should succeed where a slot with an active official tag will not.

**Switching to `external` mode** would remove this conflict entirely, since the printer would have no tag data of its own to protect — but this disables the U1's automatic tag detection system-wide. All filament data would then need to come from SEND or manual screen entry, for every slot, always. This is a real tradeoff to weigh, not a simple settings toggle to flip without consequence.

---

## K-9 Implementation Notes

- Host/IP stored via NVS (`Preferences`, namespace `k9u1`), set via Settings → U1 CONNECTION
- Connection test hits `GET http://<host>:7125/printer/info`, checks for HTTP 200 — confirmed working on real hardware
- SEND flow: OpenSpool entry screen → SEND button → slot picker (1–4) → fires `SET_PRINT_FILAMENT_CONFIG` with `CONFIG_EXTRUDER` set to the chosen slot number
- URL-encoding is done manually in firmware (percent-encoding reserved chars: space, `'`, `=`, etc.) before appending to the `script=` query parameter
- **Known gaps**:
  - No `subtype` field currently exposed on K-9's OpenSpool entry screen — SEND currently passes an empty string, which the macro layer treats as falling back to a default ("Basic")
  - No filament-sensor / print-state safety check before firing SEND (TinkerBarn's equivalent feature checks this and asks for confirmation first — K-9 does not yet)
  - Will always fail with the "official filament" error on any slot currently locked by OpenRFID — see above

## Why This Doesn't Apply to Standalone ACE

`SET_FILAMENT_CONFIG`/`SET_PRINT_FILAMENT_CONFIG` are **paxx12 Extended Firmware additions living entirely on the U1's Klipper/Moonraker side.** They are not part of Anycubic's own ACE protocol (see `anycubic-ace-n033.md` — that protocol has no write method, confirmed independently by multiACE's own FAQ). If an ACE unit is wired into a U1, these macros work because the U1's firmware is the one answering the request, not the ACE hardware itself. A standalone Anycubic printer with its own onboard ACE has no equivalent — Moonraker doesn't exist on that hardware.
