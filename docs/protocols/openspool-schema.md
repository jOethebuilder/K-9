# OpenSpool U1 — Tag Schema Reference

## Current K-9 Implementation

`openSpoolReadTag()`/`openSpoolWriteTag()` read/write NDEF JSON on NTAG215 tags, starting at page 4. Confirmed working read/write on real hardware.

Fields currently written:
```json
{
  "protocol": "openspool",
  "brand": "<manufacturer>",
  "type": "<material>",
  "color_hex": "#RRGGBB",
  "min_temp": <int>,
  "max_temp": <int>,
  "bed_min_temp": <int>,
  "bed_max_temp": <int>
}
```

Color picker currently reuses the QIDI 24-color list (`QIDI_COLORS[]`) — this is a placeholder; see below for the confirmed correct source.

---

## Confirmed Fuller Schema (from Extended Firmware docs + U1-RFID app)

The Snapmaker U1's Extended Firmware (paxx12) auto-detects filament via its OpenRFID system: tags are automatically read when filament is loaded into the feeder, and tag data clears when filament is removed. No network push is needed to get data "to the U1" — writing a correctly-formatted tag *is* the delivery mechanism (separate from the SEND-over-WiFi feature, which is an additional option, not a requirement).

Confirmed real-world schema, from the firmware docs:
```json
{
  "protocol": "openspool",
  "version": "1.0",
  "type": "PETG",
  "subtype": "Rapid",
  "color_hex": "AFAFAF",
  "additional_color_hexes": ["EEFFEE", "FF00FF"],
  "alpha": "FF",
  "brand": "Elegoo",
  "min_temp": "230",
  "max_temp": "260"
}
```

Confirmed compatible tag types: NTAG213/215/216 and Mifare Classic 1K — no hardware/tag change needed, K-9's existing NTAG215 write path is already correct.

### Gaps between current K-9 schema and confirmed fuller schema

| Field | K-9 status | Notes |
|---|---|---|
| `version` | Missing | Not user-facing — hardcode `"1.0"` |
| `subtype` | Missing | New UI field needed (e.g. "Matte", "Rapid", "HF", "CF") — no picker built yet |
| `alpha` | Missing | Recommend always `"FF"` (matches ACE precedent), no UI needed |
| `additional_color_hexes` | Missing | Only relevant for multi-color/gradient spools — skip unless multi-color support is wanted |
| Temp field quoting | K-9 writes as int, some U1-RFID examples show quoted strings | JSON parsers are generally forgiving either way; worth testing against real Extended Firmware if issues arise |

---

## Color List — Correct Source

**Do not use the QIDI 24-color list for OpenSpool U1.** Confirmed via cloning and reading U1-RFID's actual `Utils.java` source: `presetColors()` is byte-for-byte identical to ACE-RFID's 35-color ARGB list (see `anycubic-ace-n033.md` for the full hex list). K-9's existing `ANYCUBIC_COLORS[][3]` array and picker UI should be reused as-is for OpenSpool U1's color picker — no new palette needed, no separate source to maintain.

---

## U1-RFID Material Database Reference (from `Utils.populateDatabase()`)

For reference, U1-RFID ships a built-in filament database with these vendor/type/subtype combinations and temp ranges (nozzle min/max, bed min/max):

| Brand | Type | Subtype | Nozzle °C | Bed °C |
|---|---|---|---|---|
| Snapmaker | PLA | Matte | 190–220 | 50–60 |
| Snapmaker | PLA | SnapSpeed | 210–230 | 50–60 |
| Snapmaker | PLA | Basic | 190–210 | 50–60 |
| Snapmaker | PLA | Support | 180–200 | 50–60 |
| Snapmaker | PETG | Basic | 230–250 | 70–80 |
| Snapmaker | PETG | HF | 240–260 | 70–80 |
| Snapmaker | TPU | 95A | 210–230 | 30–50 |
| Snapmaker | TPU | 95A HF | 220–240 | 30–50 |
| Snapmaker | PVA | Basic | 180–200 | 50–60 |
| Snapmaker | ABS | Basic | 240–260 | 90–110 |
| Polymaker | PLA | Polylite | 190–230 | 40–60 |
| Polymaker | PLA | PolySonic | 210–240 | 40–60 |
| Polymaker | PLA | PolyTerra | 190–230 | 30–60 |
| Polymaker | ABS | Polylite | 245–265 | 90–100 |
| Polymaker | PETG | Polylite | 230–240 | 70–80 |
| Generic | PLA | Basic | 200–220 | 50–60 |
| Generic | PLA | Silk | 205–225 | 50–60 |
| Generic | PLA | CF | 210–230 | 50–60 |
| Generic | PLA | Support | 190–210 | 50–60 |
| Generic | PETG | Basic | 230–250 | 70–85 |
| Generic | ABS | Basic | 230–260 | 100–110 |
| Generic | TPU | 95A | 220–240 | 40–60 |
| Generic | TPU | 95A HF | 230–250 | 40–60 |
| Generic | ASA | Basic | 240–260 | 100–110 |
| Generic | BVOH | Basic | 190–210 | 50–60 |
| Generic | EVA | Basic | 180–210 | 30–50 |
| Generic | HIPS | Basic | 220–240 | 90–110 |
| Generic | PA | Basic | 260–290 | 80–100 |
| Generic | PA | CF | 270–300 | 80–100 |
| Generic | PC | Basic | 270–300 | 100–120 |
| Generic | PCTG | Basic | 250–270 | 70–80 |
| Generic | PE | Basic | 220–250 | 70–100 |
| Generic | PE | CF | 230–260 | 70–100 |
| Generic | PHA | Basic | 190–210 | 40–60 |
| Generic | PVA | Basic | 190–210 | 50–60 |

Useful as a reference for expanding K-9's `OS_MATERIALS[]`/subtype system, if/when the subtype picker gets built.
