// ============================================================
//  K.9 RFID SPOOL MANAGEMENT
//  Built by Joe the Builder
//  Batch 1 — PN532 Init + Tag Detection
//
//  Hardware: ESP32-2432S028R (CYD)
//  NFC Reader: PN532 via I2C
//    CYD GPIO 27 -> PN532 SDA
//    CYD GPIO 22 -> PN532 SCL
//    CYD 3.3V    -> PN532 VCC
//    CYD GND     -> PN532 GND
//
//  IMPORTANT: Set PN532 to I2C mode before use
//    Switch 1: ON
//    Switch 2: OFF
//
//  Required Libraries (install via Arduino Library Manager):
//    - Adafruit PN532 (by Adafruit)
//    - Wire (built-in)
// ============================================================

#include <Wire.h>
#include <Adafruit_PN532.h>

// --- Pin Definitions ---
#define PN532_SDA  27
#define PN532_SCL  22

// --- PN532 Setup (I2C mode) ---
Adafruit_PN532 nfc(PN532_SDA, PN532_SCL);

// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("========================================");
  Serial.println("  K.9 RFID SPOOL MANAGEMENT");
  Serial.println("  Built by Joe the Builder");
  Serial.println("  Batch 1 — Tag Detection");
  Serial.println("========================================");

  // Start I2C on correct CYD pins
  Wire.begin(PN532_SDA, PN532_SCL);

  // Initialize PN532
  nfc.begin();

  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("[ERROR] PN532 not found. Check wiring and I2C switch settings.");
    Serial.println("  -> Switch 1: ON, Switch 2: OFF on PN532 module");
    while (1); // Halt
  }

  // Print PN532 firmware info
  Serial.print("[OK] PN532 found. Chip: PN5");
  Serial.println((versiondata >> 24) & 0xFF, HEX);
  Serial.print("     Firmware version: ");
  Serial.print((versiondata >> 16) & 0xFF, DEC);
  Serial.print(".");
  Serial.println((versiondata >> 8) & 0xFF, DEC);

  // Configure to read MiFare Classic AND NTAG (covers QIDI, Snapmaker, Anycubic ACE, OpenSpool)
  nfc.SAMConfig();

  Serial.println("");
  Serial.println("[READY] Waiting for tag...");
  Serial.println("========================================");
}

// ============================================================
void loop() {
  uint8_t uid[7];
  uint8_t uidLength;

  // Wait for a tag (timeout 500ms so we keep looping cleanly)
  bool tagFound = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 500);

  if (tagFound) {
    Serial.println("");
    Serial.println(">>> TAG DETECTED <<<");
    Serial.print("    UID Length : ");
    Serial.print(uidLength, DEC);
    Serial.println(" bytes");
    Serial.print("    UID Value  : ");
    for (uint8_t i = 0; i < uidLength; i++) {
      if (uid[i] < 0x10) Serial.print("0");
      Serial.print(uid[i], HEX);
      if (i < uidLength - 1) Serial.print(":");
    }
    Serial.println("");

    // Identify tag type by UID length
    if (uidLength == 4) {
      Serial.println("    Tag Type   : MIFARE Classic 1K");
      Serial.println("    Compatible : QIDI Box / Snapmaker U1");
    } else if (uidLength == 7) {
      Serial.println("    Tag Type   : NTAG (215 / 213 / 216)");
      Serial.println("    Compatible : Anycubic ACE / OpenSpool");
    } else {
      Serial.println("    Tag Type   : Unknown");
    }

    Serial.println("========================================");

    // Small delay to avoid repeated reads of same tag
    delay(2000);
  }
}
