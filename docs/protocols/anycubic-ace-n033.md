# Anycubic ACE — N033 Protocol & Color System

## Source

Reference repo for ACE protocol + Anycubic app behavior: `jOethebuilder/ACE-RFID` (Joe's fork). Key files:
- `AceRFID/app/src/main/java/dngsoftware/acerfid/MainActivity.java` — tag read/write, picker dialog UI
- `AceRFID/app/src/main/java/dngsoftware/acerfid/Utils.java` — helper functions, **`presetColors()`** (the 35-color list)
- `colors.xml` is a red herring — only has UI theme colors, not filament presets

The N033 hub communication protocol doc (`docs/N033 Material box communication protocol.md` in DnG-Crafts' `ACE-RFID` repo) is sourced directly from Anycubic's own official open-source firmware: `ANYCUBIC-3D/Kobra3` klipper-go repo. This is stock, official Anycubic protocol — not reverse-engineered guesswork.

---

## Tag Format (NTAG215, raw block layout)

ACE stores raw ARGB directly on the tag at **page 20**, byte order `[Alpha, Blue, Green, Red]`. There is no fixed palette in the protocol itself — any RGB value is valid.

Other confirmed fields/pages:
- Page 4: flag bytes (must be `0x7B, 0x00, 0x65, 0x00`)
- Pages 5–9: SKU (20 bytes)
- Pages 10–14: brand (20 bytes) — always hardcoded to `"AC"`, not user-selectable
- Pages 15–19: material name (20 bytes)
- Page 20: color `[Alpha, Blue, Green, Red]`
- Page 24: nozzle temp min/max (int16 LE pair)
- Page 29: bed temp min/max (int16 LE pair)
- Page 30: diameter/length data
- Page 31: fixed constant (`0xE8, 0x03, 0x00, 0x00`)

## 35-Color ARGB Preset List

Confirmed via the real Android app's source (`setupPresetColors()` / `presetColors()` in `Utils.java`) that the app itself never names colors — it only ever displays the raw hex code. The old K-9 code guessed a color *name* by nearest-match against QIDI's unrelated 24-color list, which was frequently wrong.

Confirmed identical between ACE-RFID's `Utils.java` and U1-RFID's `Utils.java` — same 35 colors, same order, byte-for-byte match (confirmed by directly cloning and reading both source files). Used for both the Anycubic entry screen and reused as-is for OpenSpool U1's color picker.

```
#25C4DA #0099A7 #0B359A #0A4AB6 #11B6EE #90C6F5 #FA7C0C #F7B30F
#E5C20F #B18F2E #8D766D #6C4E43 #E62E2E #EE2862 #EA2A2B #E83D89
#AE2E65 #611C8B #8D60C7 #B287C9 #006764 #018D80 #42B5AE #1D822D
#54B351 #72E115 #474747 #668798 #B1BEC6 #58636E #F8E911 #F6D311
#F2EFCE #FFFFFF #000000
```

Known reference point: `FF611C8B` = Deep Purple, preset #18.

---

## Network Protocol (webhook, read/status only)

The ACE hub exposes a native JSON webhook interface for status/control, documented in N033:

**Read-only (status/info):**
- `filament_hub/info` — box-level info (id, slots, model, firmware)
- `filament_hub/filament_info` — per-slot detail: SKU, brand, type, color, temps, diameter
- `filament_hub` (subscription) — live status broadcast: drying state, temp/humidity, per-slot status/color/SKU
- `filament_hub/get_config` — box config readback

**Action/control (writes, but not material data):**
- `UNWIND_FILAMENT ID=x INDEX=y` / `FEED_FILAMENT ID=x INDEX=y` — physically retract/feed a slot (motion, not data)
- `filament_hub/start_drying` / `stop_drying` / `set_fan_speed` — dryer controls
- `filament_hub/set_config` — box-level settings only: `auto_refill`, `ext_spool`, `runout_detect`, `flush_multiplier`
- `filament_hub/filament_recognition` — "remove the editing color" — clears/refreshes a slot back toward RFID-read state

### Confirmed: No write/set-material method exists

Every slot record has a `source` field: `0-unknown, 1-RFID, 2-user definition, 3-no material`. This proves Anycubic's firmware *can* hold manually-entered material data — but **no method in this protocol exposes a way to set it.**

Checked more aggressive community reverse-engineering (`decay71/multiACE`, forked from `BlackFrogKok/SnapAce`) — their own FAQ gives the same answer:

> Does it work with / without RFID tags? Yes. Anycubic-RFID (or self written) spools work fine — or **set filament type and color manually via the Snapmaker display.**

**Conclusion: there is no network write path into a stock/standalone Anycubic ACE unit, documented or undocumented, found by anyone.** The only two ways to get material data onto an ACE slot are: (1) an NFC tag the ACE reads itself, or (2) manual entry on the printer's own physical screen. K-9's tag-writing approach is the only viable path for ACE.

This is distinct from the Snapmaker U1's own separate Moonraker macro layer — see `u1-moonraker-macros.md`. That layer is a U1 Extended Firmware addition, not part of Anycubic's protocol, and doesn't apply to standalone ACE units.
