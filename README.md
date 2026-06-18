# K.9-RFID-SPOOL-MANAGEMENT
ESP32 RFID spool management system for 3D printing (K.9 project)

---

## What This Is

K.9 is a dedicated spool management tool designed to:
- Read and write RFID/NFC filament tags
- Support multiple printer formats
- Work with a local Spoolman server
- Expand into a full smart spool system over time

---

## Current Focus

- ESP32 firmware
- PN532 RFID/NFC (I2C)
- Touchscreen UI
- Existing support:
  - Qidi
  - Snapmaker U1
- Next addition:
  - Anycubic tag support

---

## Hardware

- ESP32
- PN532
- 2.8" TFT touchscreen
- Wi-Fi (future integration)

---

## Features

- Tag detection and handling
- Multi-format spool support
- Touch-driven interface
- Modular design for new printer systems
- Boot splash branding (K-9 Built by Joe the Builder)

---

## Roadmap

- Add Anycubic support
- Improve auto-detection
- Connect to Spoolman API
- Add scale integration
- Web-based firmware flashing (GitHub Pages)

---

## Why This Exists

To build a simple, reliable way to track and manage filament spools across different printers without relying on closed ecosystems.

---

## Status

Active development — building step by step from a working base.

---

## License

MIT
## Web Installer

Click here to flash the ESP32:

https://joethebuilder.github.io/k9-RFID-SPOOL-MANAGEMENT/web-flasher/index.html
