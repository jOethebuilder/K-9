// ============================================================
//  K.9 RFID SPOOL MANAGEMENT
//  Built by Joe the Builder
//  Batch 2 — Boot Screen + Branding + Tag Detection
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
//  IMPORTANT: After installing TFT_eSPI you MUST edit
//  User_Setup.h in the TFT_eSPI library folder.
//  See User_Setup.h file in this repo's /src folder.
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

// --- Colors ---
#define COLOR_BG        0x0000   // Black
#define COLOR_PRIMARY   0x1C9F   // Blue  (#1a6aff approx)
#define COLOR_DIM       0x18C3   // Dim blue
#define COLOR_WHITE     0xFFFF
#define COLOR_GRAY      0x4208   // Dark gray

// --- Screen state ---
enum Screen { BOOT, HOME, SCANNING, RESULT };
Screen currentScreen = BOOT;

// --- Tag result storage ---
String tagType     = "";
String tagUID      = "";
String tagCompat   = "";

// ============================================================
//  DRAW FUNCTIONS
// ============================================================

void drawBootScreen() {
  tft.fillScreen(COLOR_BG);

  // --- Outer ring ---
  tft.drawCircle(160, 100, 50, COLOR_DIM);

  // --- Dashed inner ring (simulated with arcs) ---
  for (int i = 0; i < 360; i += 20) {
    float r1 = (i) * 0.01745;
    float r2 = (i + 10) * 0.01745;
    tft.drawLine(
      160 + 38 * cos(r1), 100 + 38 * sin(r1),
      160 + 38 * cos(r2), 100 + 38 * sin(r2),
      COLOR_DIM
    );
  }

  // --- Center dot ---
  tft.fillCircle(160, 100, 10, COLOR_DIM);
  tft.fillCircle(160, 100, 6,  COLOR_PRIMARY);
  tft.fillCircle(160, 100, 2,  COLOR_WHITE);

  // --- Cardinal tick marks ---
  tft.fillRect(158, 50, 4, 10, COLOR_PRIMARY);   // top
  tft.fillRect(158, 140, 4, 10, COLOR_PRIMARY);  // bottom
  tft.fillRect(108, 98, 10, 4, COLOR_PRIMARY);   // left
  tft.fillRect(202, 98, 10, 4, COLOR_PRIMARY);   // right

  // --- Corner dots ---
  tft.fillCircle(160, 50, 3, COLOR_PRIMARY);
  tft.fillCircle(160, 150, 3, COLOR_PRIMARY);
  tft.fillCircle(110, 100, 3, COLOR_PRIMARY);
  tft.fillCircle(210, 100, 3, COLOR_PRIMARY);

  // --- K.9 Title ---
  tft.setTextColor(COLOR_PRIMARY, COLOR_BG);
  tft.setTextSize(4);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("K.9", 160, 168);

  // --- Subtitle ---
  tft.setTextSize(1);
  tft.setTextColor(COLOR_DIM, COLOR_BG);
  tft.drawString("RFID SPOOL MANAGEMENT", 160, 202);

  // --- Loading bar background ---
  tft.drawRect(60, 218, 200, 6, COLOR_GRAY);

  // --- Animate loading bar ---
  for (int i = 0; i <= 200; i += 4) {
    tft.fillRect(60, 219, i, 4, COLOR_PRIMARY);
    delay(15);
  }

  // --- Built by ---
  tft.setTextColor(COLOR_GRAY, COLOR_BG);
  tft.drawString("BUILT BY JOE THE BUILDER", 160, 232);

  delay(1500);
}

void drawHomeScreen() {
  tft.fillScreen(COLOR_BG);

  // --- Header bar ---
  tft.fillRect(0, 0, 320, 28, COLOR_PRIMARY);
  tft.setTextColor(COLOR_WHITE, COLOR_PRIMARY);
  tft.setTextSize(1);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("K.9 RFID SPOOL MANAGEMENT", 160, 14);

  // --- Scan button ---
  tft.fillRoundRect(60, 60, 200, 60, 8, COLOR_PRIMARY);
  tft.setTextColor(COLOR_WHITE, COLOR_PRIMARY);
  tft.setTextSize(2);
  tft.drawString("SCAN TAG", 160, 90);

  // --- Status area ---
  tft.setTextColor(COLOR_DIM, COLOR_BG);
  tft.setTextSize(1);
  tft.drawString("Hold tag near reader", 160, 160);
  tft.drawString("then tap SCAN TAG", 160, 175);

  // --- Footer ---
  tft.setTextColor(COLOR_GRAY, COLOR_BG);
  tft.drawString("v0.2 - Batch 2", 160, 230);
}

void drawScanningScreen() {
  tft.fillScreen(COLOR_BG);
  tft.fillRect(0, 0, 320, 28, COLOR_PRIMARY);
  tft.setTextColor(COLOR_WHITE, COLOR_PRIMARY);
  tft.setTextSize(1);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("K.9 RFID SPOOL MANAGEMENT", 160, 14);

  tft.setTextColor(COLOR_PRIMARY, COLOR_BG);
  tft.setTextSize(2);
  tft.drawString("SCANNING...", 160, 100);
  tft.setTextSize(1);
  tft.setTextColor(COLOR_DIM, COLOR_BG);
  tft.drawString("Hold tag near reader", 160, 140);

  // --- Animated rings ---
  for (int r = 20; r <= 50; r += 15) {
    tft.drawCircle(160, 190, r, COLOR_DIM);
  }
}

void drawResultScreen() {
  tft.fillScreen(COLOR_BG);
  tft.fillRect(0, 0, 320, 28, COLOR_PRIMARY);
  tft.setTextColor(COLOR_WHITE, COLOR_PRIMARY);
  tft.setTextSize(1);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("K.9 RFID SPOOL MANAGEMENT", 160, 14);

  tft.setTextColor(COLOR_WHITE, COLOR_BG);
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);

  tft.setTextColor(COLOR_DIM, COLOR_BG);
  tft.drawString("TAG TYPE:", 20, 45);
  tft.setTextColor(COLOR_WHITE, COLOR_BG);
  tft.drawString(tagType, 20, 60);

  tft.setTextColor(COLOR_DIM, COLOR_BG);
  tft.drawString("UID:", 20, 85);
  tft.setTextColor(COLOR_WHITE, COLOR_BG);
  tft.drawString(tagUID, 20, 100);

  tft.setTextColor(COLOR_DIM, COLOR_BG);
  tft.drawString("COMPATIBLE WITH:", 20, 125);
  tft.setTextColor(COLOR_PRIMARY, COLOR_BG);
  tft.drawString(tagCompat, 20, 140);

  // --- Back button ---
  tft.fillRoundRect(60, 180, 200, 45, 8, COLOR_DIM);
  tft.setTextColor(COLOR_WHITE, COLOR_DIM);
  tft.setTextSize(2);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("BACK", 160, 202);
}

// ============================================================
//  SETUP
// ============================================================

void setup() {
  Serial.begin(115200);

  // Display init
  tft.init();
  tft.setRotation(1); // Landscape
  tft.fillScreen(COLOR_BG);

  // Touch init
  touch.begin();

  // PN532 init
  Wire.begin(PN532_SDA, PN532_SCL);
  nfc.begin();

  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    tft.setTextColor(TFT_RED, COLOR_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(1);
    tft.drawString("PN532 NOT FOUND", 160, 120);
    tft.drawString("Check wiring & I2C switches", 160, 140);
    Serial.println("[ERROR] PN532 not found!");
    while (1);
  }

  nfc.SAMConfig();
  Serial.println("[OK] PN532 ready");

  // Show boot screen
  drawBootScreen();

  // Show home screen
  currentScreen = HOME;
  drawHomeScreen();
}

// ============================================================
//  LOOP
// ============================================================

void loop() {
  // Check for touch
  if (touch.tirqTouched() && touch.touched()) {
    TS_Point p = touch.getPoint();

    // Map touch coords to screen (landscape)
    int x = map(p.x, 200, 3700, 0, 320);
    int y = map(p.y, 200, 3700, 0, 240);

    if (currentScreen == HOME) {
      // SCAN button area
      if (x > 60 && x < 260 && y > 60 && y < 120) {
        currentScreen = SCANNING;
        drawScanningScreen();
        scanTag();
      }
    }

    if (currentScreen == RESULT) {
      // BACK button area
      if (x > 60 && x < 260 && y > 180 && y < 225) {
        currentScreen = HOME;
        drawHomeScreen();
      }
    }

    delay(200); // debounce
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
    // Build UID string
    tagUID = "";
    for (uint8_t i = 0; i < uidLength; i++) {
      if (uid[i] < 0x10) tagUID += "0";
      tagUID += String(uid[i], HEX);
      if (i < uidLength - 1) tagUID += ":";
    }
    tagUID.toUpperCase();

    // Identify type
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
    // No tag found — back to home
    tft.setTextColor(TFT_RED, COLOR_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(1);
    tft.drawString("No tag detected. Try again.", 160, 200);
    delay(1500);
    currentScreen = HOME;
    drawHomeScreen();
  }
}
