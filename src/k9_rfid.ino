// ============================================================
//  K.9 RFID SPOOL MANAGEMENT
//  Built by Joe the Builder
//  v0.3 — K.9 Branding + Graceful PN532 Handling
//
//  Hardware: ESP32-2432S028R (CYD)
//  Display:  ILI9341 240x320 TFT via TFT_eSPI
//  Touch:    XPT2046 resistive touchscreen
//  NFC:      PN532 via I2C
//    CYD GPIO 27 -> PN532 SDA
//    CYD GPIO 22 -> PN532 SCL
//    CYD 3.3V    -> PN532 VCC
//    CYD GND     -> PN532 GND
//
//  IMPORTANT: Set PN532 to I2C mode
//    Switch 1: ON  / Switch 2: OFF
//
//  Required Libraries (Arduino Library Manager):
//    - TFT_eSPI by Bodmer
//    - XPT2046_Touchscreen by Paul Stoffregen
//    - Adafruit PN532 by Adafruit
//    - Wire (built-in)
//    - SPI (built-in)
//
//  IMPORTANT: After installing TFT_eSPI you MUST copy
//  User_Setup.h from this repo's /src folder into:
//  Arduino/libraries/TFT_eSPI/User_Setup.h
// ============================================================

#include <Wire.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <Adafruit_PN532.h>

// --- Display ---
TFT_eSPI tft = TFT_eSPI();

// --- Touch ---
#define TOUCH_CS  33
#define TOUCH_IRQ 36
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);

// --- PN532 I2C pins ---
#define PN532_SDA 27
#define PN532_SCL 22
Adafruit_PN532 nfc(PN532_SDA, PN532_SCL);

// --- K.9 Color Palette (RGB565) ---
#define COLOR_BG        0x1082   // Dark brown/black  ~#1a1200
#define COLOR_CARD      0x2082   // Slightly lighter  ~#211800
#define COLOR_ORANGE    0xFB60   // Orange            ~#FF7A00
#define COLOR_ORANGE_DIM 0x7940  // Dim orange        ~#7a3a00
#define COLOR_TEXT      0xEDB0   // Warm off-white    ~#e8d9c0
#define COLOR_MUTED     0x7528   // Muted brown       ~#7a6a50
#define COLOR_WHITE     0xFFFF
#define COLOR_RED       0xF800

// --- PN532 present flag ---
bool nfcReady = false;

// --- Screen state ---
enum Screen { BOOT, HOME, SCANNING, RESULT, NO_READER };
Screen currentScreen = BOOT;

// --- Tag result storage ---
String tagType   = "";
String tagUID    = "";
String tagCompat = "";

// ============================================================
//  HELPERS
// ============================================================

// Draw corner tick marks like the web flasher hero box
void drawCornerTicks(int x, int y, int w, int h, uint16_t color) {
  int t = 10; // tick length
  // top-left
  tft.drawFastHLine(x, y, t, color);
  tft.drawFastVLine(x, y, t, color);
  // top-right
  tft.drawFastHLine(x + w - t, y, t, color);
  tft.drawFastVLine(x + w - 1, y, t, color);
  // bottom-left
  tft.drawFastHLine(x, y + h - 1, t, color);
  tft.drawFastVLine(x, y + h - t, t, color);
  // bottom-right
  tft.drawFastHLine(x + w - t, y + h - 1, t, color);
  tft.drawFastVLine(x + w - 1, y + h - t, t, color);
}

// ============================================================
//  BOOT SCREEN
// ============================================================

void drawBootScreen() {
  tft.fillScreen(COLOR_BG);

  // Corner ticks on full screen
  drawCornerTicks(4, 4, 312, 232, COLOR_ORANGE_DIM);

  // Big K·9 title
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(COLOR_ORANGE, COLOR_BG);
  tft.setTextSize(5);
  tft.drawString("K.9", 160, 32);

  // Flanking lines + Affirmative!
  tft.drawFastHLine(20,  98, 60, COLOR_ORANGE);
  tft.drawFastHLine(240, 98, 60, COLOR_ORANGE);
  tft.setTextSize(1);
  tft.setTextColor(COLOR_ORANGE, COLOR_BG);
  tft.drawString("Affirmative!", 160, 93);

  // Divider
  tft.drawFastHLine(20, 112, 280, COLOR_ORANGE_DIM);

  // Built by / version
  tft.setTextColor(COLOR_MUTED, COLOR_BG);
  tft.drawString("BUILT BY JOE THE BUILDER", 160, 122);
  tft.drawString("RFID SPOOL MANAGER v0.3", 160, 136);

  // Loading bar background
  tft.drawRect(20, 158, 280, 6, COLOR_ORANGE_DIM);

  // Animate loading bar
  for (int i = 0; i <= 280; i += 4) {
    tft.fillRect(20, 159, i, 4, COLOR_ORANGE);
    delay(10);
  }

  // Status line
  tft.setTextColor(COLOR_ORANGE, COLOR_BG);
  tft.drawString("Affirmative! K.9 ready.", 160, 172);

  delay(1200);
}

// ============================================================
//  HOME SCREEN
// ============================================================

void drawHomeScreen() {
  tft.fillScreen(COLOR_BG);

  // Header bar
  tft.fillRect(0, 0, 320, 24, COLOR_CARD);
  tft.drawFastHLine(0, 24, 320, COLOR_ORANGE_DIM);
  tft.setTextColor(COLOR_ORANGE, COLOR_CARD);
  tft.setTextSize(1);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("K.9 RFID SPOOL MANAGEMENT", 160, 12);

  // NFC status badge
  if (!nfcReady) {
    tft.fillRect(4, 28, 312, 16, COLOR_BG);
    tft.setTextColor(COLOR_RED, COLOR_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(1);
    tft.drawString("! NO READER CONNECTED !", 160, 36);
  }

  // Scan button
  tft.fillRect(40, 60, 240, 56, COLOR_CARD);
  tft.drawRect(40, 60, 240, 56, COLOR_ORANGE);
  tft.setTextColor(COLOR_ORANGE, COLOR_CARD);
  tft.setTextSize(2);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("SCAN TAG", 160, 88);

  // Hint
  tft.setTextSize(1);
  tft.setTextColor(COLOR_MUTED, COLOR_BG);
  tft.drawString("Hold tag near reader", 160, 140);
  tft.drawString("then tap SCAN TAG", 160, 154);

  // Corner ticks
  drawCornerTicks(4, 4, 312, 232, COLOR_ORANGE_DIM);

  // Footer
  tft.setTextColor(COLOR_MUTED, COLOR_BG);
  tft.drawString("v0.3 - Built by Joe the Builder", 160, 226);
}

// ============================================================
//  SCANNING SCREEN
// ============================================================

void drawScanningScreen() {
  tft.fillScreen(COLOR_BG);

  tft.fillRect(0, 0, 320, 24, COLOR_CARD);
  tft.drawFastHLine(0, 24, 320, COLOR_ORANGE_DIM);
  tft.setTextColor(COLOR_ORANGE, COLOR_CARD);
  tft.setTextSize(1);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("K.9 RFID SPOOL MANAGEMENT", 160, 12);

  tft.setTextColor(COLOR_ORANGE, COLOR_BG);
  tft.setTextSize(2);
  tft.drawString("SCANNING...", 160, 90);

  tft.setTextSize(1);
  tft.setTextColor(COLOR_MUTED, COLOR_BG);
  tft.drawString("Hold tag near reader", 160, 120);

  // Concentric rings
  for (int r = 20; r <= 60; r += 20) {
    tft.drawCircle(160, 185, r, COLOR_ORANGE_DIM);
  }

  drawCornerTicks(4, 4, 312, 232, COLOR_ORANGE_DIM);
}

// ============================================================
//  RESULT SCREEN
// ============================================================

void drawResultScreen() {
  tft.fillScreen(COLOR_BG);

  tft.fillRect(0, 0, 320, 24, COLOR_CARD);
  tft.drawFastHLine(0, 24, 320, COLOR_ORANGE_DIM);
  tft.setTextColor(COLOR_ORANGE, COLOR_CARD);
  tft.setTextSize(1);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("K.9 RFID SPOOL MANAGEMENT", 160, 12);

  tft.setTextDatum(TL_DATUM);

  tft.setTextColor(COLOR_MUTED, COLOR_BG);
  tft.drawString("TAG TYPE:", 20, 40);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.drawString(tagType, 20, 54);

  tft.setTextColor(COLOR_MUTED, COLOR_BG);
  tft.drawString("UID:", 20, 78);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.drawString(tagUID, 20, 92);

  tft.setTextColor(COLOR_MUTED, COLOR_BG);
  tft.drawString("COMPATIBLE WITH:", 20, 116);
  tft.setTextColor(COLOR_ORANGE, COLOR_BG);
  tft.drawString(tagCompat, 20, 130);

  // Back button
  tft.fillRect(40, 175, 240, 48, COLOR_CARD);
  tft.drawRect(40, 175, 240, 48, COLOR_ORANGE_DIM);
  tft.setTextColor(COLOR_TEXT, COLOR_CARD);
  tft.setTextSize(2);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("BACK", 160, 199);

  drawCornerTicks(4, 4, 312, 232, COLOR_ORANGE_DIM);
}

// ============================================================
//  NO READER SCREEN
// ============================================================

void drawNoReaderScreen() {
  tft.fillScreen(COLOR_BG);
  drawCornerTicks(4, 4, 312, 232, COLOR_ORANGE_DIM);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COLOR_ORANGE, COLOR_BG);
  tft.setTextSize(2);
  tft.drawString("NO READER", 160, 90);
  tft.drawString("CONNECTED", 160, 112);

  tft.setTextSize(1);
  tft.setTextColor(COLOR_MUTED, COLOR_BG);
  tft.drawString("Connect PN532 and reboot", 160, 148);
  tft.drawString("GPIO 27 -> SDA", 160, 164);
  tft.drawString("GPIO 22 -> SCL", 160, 178);

  tft.setTextColor(COLOR_ORANGE, COLOR_BG);
  tft.drawString("Tap anywhere to continue", 160, 210);
}

// ============================================================
//  SETUP
// ============================================================

void setup() {
  Serial.begin(115200);

  // Display
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(COLOR_BG);

  // Touch
  touch.begin();

  // Boot screen first — display test before NFC
  drawBootScreen();

  // PN532 init — graceful, does not hang
  Wire.begin(PN532_SDA, PN532_SCL);
  nfc.begin();
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("[WARN] PN532 not found — running without reader");
    nfcReady = false;
    currentScreen = NO_READER;
    drawNoReaderScreen();
  } else {
    nfc.SAMConfig();
    Serial.println("[OK] PN532 ready");
    nfcReady = true;
    currentScreen = HOME;
    drawHomeScreen();
  }
}

// ============================================================
//  LOOP
// ============================================================

void loop() {
  if (touch.tirqTouched() && touch.touched()) {
    TS_Point p = touch.getPoint();
    int x = map(p.x, 200, 3700, 0, 320);
    int y = map(p.y, 200, 3700, 0, 240);

    if (currentScreen == NO_READER) {
      // Tap anywhere to go to home (reader-less mode)
      currentScreen = HOME;
      drawHomeScreen();
    }

    if (currentScreen == HOME) {
      // SCAN button
      if (x > 40 && x < 280 && y > 60 && y < 116) {
        if (nfcReady) {
          currentScreen = SCANNING;
          drawScanningScreen();
          scanTag();
        } else {
          tft.setTextColor(COLOR_RED, COLOR_BG);
          tft.setTextDatum(MC_DATUM);
          tft.setTextSize(1);
          tft.drawString("No reader connected!", 160, 170);
          delay(1500);
          drawHomeScreen();
        }
      }
    }

    if (currentScreen == RESULT) {
      // BACK button
      if (x > 40 && x < 280 && y > 175 && y < 223) {
        currentScreen = HOME;
        drawHomeScreen();
      }
    }

    delay(200);
  }
}

// ============================================================
//  TAG SCAN
// ============================================================

void scanTag() {
  uint8_t uid[7];
  uint8_t uidLength;

  bool found = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 3000);

  if (found) {
    tagUID = "";
    for (uint8_t i = 0; i < uidLength; i++) {
      if (uid[i] < 0x10) tagUID += "0";
      tagUID += String(uid[i], HEX);
      if (i < uidLength - 1) tagUID += ":";
    }
    tagUID.toUpperCase();

    if (uidLength == 4) {
      tagType  = "MIFARE Classic 1K";
      tagCompat = "QIDI / Snapmaker U1";
    } else if (uidLength == 7) {
      tagType  = "NTAG215 / NTAG213";
      tagCompat = "Anycubic ACE / OpenSpool";
    } else {
      tagType  = "Unknown (" + String(uidLength) + " byte UID)";
      tagCompat = "Unknown";
    }

    Serial.println("Tag found: " + tagUID + " | " + tagType);
    currentScreen = RESULT;
    drawResultScreen();

  } else {
    tft.setTextColor(COLOR_RED, COLOR_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(1);
    tft.drawString("No tag detected. Try again.", 160, 200);
    delay(1500);
    currentScreen = HOME;
    drawHomeScreen();
  }
}
