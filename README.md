# K-9 mark1
ESP32 RFID spool management system for 3D printing (K-9 project)

---

## What This Is

K-9 is a dedicated spool management tool designed to:
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
  - Snapmaker U1 Via Openspool plus Openspool 
  - Anycubic tag 

---

## Hardware

- ESP32
- PN532
- 2.8" TFT touchscreen
- Wi-Fi for the direct link to the Spoolman server

---

## Features

- Tag detection and handling
- Multi-format spool support
- Touch-driven interface
- Modular design for new printer systems
- Boot splash branding (K-9 mark1 Built by Joe the Builder)

---

## Roadmap

  -Anycubic support
  -Qidi support
  -OpenSpool U1
- Improve auto-detection
- Connect to Spoolman API "In work"
- Add scale integration "In work"
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

https://joethebuilder.github.io/K-9/web-flasher/
