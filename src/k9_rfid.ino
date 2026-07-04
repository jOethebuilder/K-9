// ============================================================
//  K-9 Mark 1 — Spool Manager
//  Built by Joe the Builder
//  v0.0.0
//
//  Hardware: ESP32-2432S028R (CYD)
//  Display:  ILI9341 320x240 via TFT_eSPI (HSPI)
//  Touch:    XPT2046 on VSPI bus
//  NFC:      PN532 via I2C
//    GPIO 27 -> SDA
//    GPIO 22 -> SCL
//
//  Libraries: TFT_eSPI, XPT2046_Touchscreen, Adafruit PN532
// ============================================================

#include <Wire.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <Adafruit_PN532.h>

// ── Display ─────────────────────────────────────────────────
TFT_eSPI tft = TFT_eSPI();
int W, H;

// ── Backlight ───────────────────────────────────────────────
#define BL_PIN 21

// ── Touch (VSPI) ────────────────────────────────────────────
#define XPT2046_CLK  25
#define XPT2046_MISO 39
#define XPT2046_MOSI 32
#define XPT2046_CS   33
#define XPT2046_IRQ  36
SPIClass touchSPI(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

// ── PN532 ───────────────────────────────────────────────────
#define PN532_SDA 27
#define PN532_SCL 22
Adafruit_PN532 nfc(PN532_SDA, PN532_SCL);
bool nfcReady = false;

// ── Colors (RGB565) ─────────────────────────────────────────
#define C_BG       0x10A2
#define C_CARD     0x2082
#define C_ORANGE   0xFB60
#define C_ORANGE_D 0x7940
#define C_TEXT     0xEF7B
#define C_MUTED    0x7529
#define C_GREEN    0x07E0
#define C_RED      0xF800
#define C_WHITE    0xFFFF
#define C_BLACK    0x0000

// ── Screens ─────────────────────────────────────────────────
enum Screen {
  SCR_SPLASH,
  SCR_MAIN,
  SCR_QIDI,
  SCR_OPENSPOOL,
  SCR_ANYCUBIC,
  SCR_SETTINGS,
  SCR_NO_READER
};
Screen currentScreen = SCR_SPLASH;

// ── Tag status ──────────────────────────────────────────────
enum TagStatus {
  TAG_NONE,
  TAG_BLANK,
  TAG_READ,
  TAG_WRITE_OK,
  TAG_WRITE_FAIL,
  TAG_CLEAR_OK,
  TAG_CLEAR_FAIL
};
TagStatus tagStatus = TAG_NONE;

// ── Tag data ────────────────────────────────────────────────
struct TagData {
  char manufacturer[17];
  char material[17];
  char color[17];
  uint8_t r, g, b;
  int extMin, extMax;
  int bedMin, bedMax;
  uint8_t uid[7];
  uint8_t uidLen;
  bool hasData;
};
TagData tagData;

// ── ACE tag data ────────────────────────────────────────────
struct AceTagData {
  char brand[17];
  char material[17];
  char sku[17];
  uint8_t r, g, b;
  int extMin, extMax;
  int bedMin, bedMax;
  int diameter100;
  int lengthM;
};
AceTagData aceData;
// ============================================================
//  QIDI LOOKUP TABLES
// ============================================================
const char* qidiMaterialName(uint8_t code) {
  switch(code) {
    case 1:  return "PLA";
    case 2:  return "PLA Matte";
    case 3:  return "PLA Metal";
    case 4:  return "PLA Silk";
    case 5:  return "PLA-CF";
    case 6:  return "PLA-Wood";
    case 7:  return "PLA Basic";
    case 8:  return "PLA Matte Basic";
    case 11: return "ABS";
    case 12: return "ABS-GF";
    case 13: return "ABS-Metal";
    case 14: return "ABS-Odorless";
    case 18: return "ASA";
    case 19: return "ASA-AERO";
    case 24: return "UltraPA";
    case 25: return "PA-CF";
    case 26: return "UltraPA-CF25";
    case 27: return "PA12-CF";
    case 30: return "PAHT-CF";
    case 31: return "PAHT-GF";
    case 32: return "Support PAHT";
    case 33: return "Support PET/PA";
    case 34: return "PC/ABS-FR";
    case 37: return "PET-CF";
    case 38: return "PET-GF";
    case 39: return "PETG Basic";
    case 40: return "PETG Tough";
    case 41: return "PETG Rapido";
    case 42: return "PETG-CF";
    case 43: return "PETG-GF";
    case 44: return "PPS-CF";
    case 45: return "PETG Trans.";
    case 47: return "PVA";
    case 49: return "TPU-Aero";
    case 50: return "TPU";
    default: return "Unknown";
  }
}

const char* qidiManufacturerName(uint8_t code) {
  switch(code) {
    case 0:  return "Generic";
    case 1:  return "QIDI";
    default: return "Unknown";
  }
}

struct QidiColor {
  uint8_t r, g, b;
  const char* name;
};

const QidiColor QIDI_COLORS[] = {
  { 0,   0,   0,   "Unknown"    },
  { 250, 250, 250, "White"      },
  { 6,   6,   6,   "Black"      },
  { 217, 227, 237, "Light Blue" },
  { 92,  243, 15,  "Lime"       },
  { 99,  228, 146, "Mint"       },
  { 40,  80,  255, "Blue"       },
  { 254, 152, 254, "Pink"       },
  { 223, 214, 40,  "Yellow"     },
  { 34,  131, 50,  "Green"      },
  { 153, 222, 255, "Sky Blue"   },
  { 23,  20,  176, "Dark Blue"  },
  { 206, 192, 254, "Lavender"   },
  { 202, 222, 75,  "Yellow-Grn" },
  { 19,  83,  171, "Navy"       },
  { 94,  169, 253, "Cornflower" },
  { 168, 120, 255, "Purple"     },
  { 254, 113, 122, "Salmon"     },
  { 255, 54,  45,  "Red"        },
  { 226, 223, 205, "Beige"      },
  { 137, 143, 155, "Gray"       },
  { 110, 56,  18,  "Brown"      },
  { 202, 197, 159, "Khaki"      },
  { 242, 134, 54,  "Orange"     },
  { 184, 127, 43,  "Gold"       },
};
const char* nearestColorName(uint8_t r, uint8_t g, uint8_t b) {
  int bestIdx = 1;
  long bestDist = 99999999;
  for (int i = 1; i < 25; i++) {
    long dr = (long)r - QIDI_COLORS[i].r;
    long dg = (long)g - QIDI_COLORS[i].g;
    long db = (long)b - QIDI_COLORS[i].b;
    long dist = dr*dr + dg*dg + db*db;
    if (dist < bestDist) { bestDist = dist; bestIdx = i; }
  }
  return QIDI_COLORS[bestIdx].name;
}
// ============================================================
//  NFC HELPERS
// ============================================================

void intToByteLE(int v, uint8_t* out) {
  out[0] = v & 0xFF;
  out[1] = (v >> 8) & 0xFF;
}

int byteToIntLE(uint8_t* d) {
  return (int)d[0] | ((int)d[1] << 8);
}

bool readNfcPage(uint8_t page, uint8_t* out) {
  for (uint8_t i = 0; i < 3; i++) {
    if (nfc.ntag2xx_ReadPage(page, out)) return true;
    delay(20);
  }
  return false;
}

bool writeNfcPage(uint8_t page, uint8_t* data) {
  uint8_t tmp[4];
  memcpy(tmp, data, 4);
  for (uint8_t i = 0; i < 3; i++) {
    if (nfc.ntag2xx_WritePage(page, tmp)) return true;
    delay(20);
  }
  return false;
}

// ============================================================
//  DRAW HELPERS
// ============================================================
bool hit(int bx, int by, int bw, int bh, int tx, int ty) {
  return tx >= bx && tx <= bx + bw && ty >= by && ty <= by + bh;
}

void drawCornerTicks(uint16_t color) {
  int t = 10;
  tft.drawFastHLine(4,    4,   t, color);
  tft.drawFastVLine(4,    4,   t, color);
  tft.drawFastHLine(W-14, 4,   t, color);
  tft.drawFastVLine(W-5,  4,   t, color);
  tft.drawFastHLine(4,    H-5, t, color);
  tft.drawFastVLine(4,    H-14,t, color);
  tft.drawFastHLine(W-14, H-5, t, color);
  tft.drawFastVLine(W-5,  H-14,t, color);
}

void drawHeader(const char* title) {
  tft.fillRect(0, 0, W, 26, C_CARD);
  tft.drawFastHLine(0, 26, W, C_ORANGE_D);
  tft.setTextColor(C_ORANGE, C_CARD);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(title, W/2, 13, 2);
}

void drawFooter(const char* msg, uint16_t color) {
  tft.fillRect(0, H-18, W, 18, C_CARD);
  tft.drawFastHLine(0, H-18, W, C_ORANGE_D);
  tft.setTextColor(color, C_CARD);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(msg, W/2, H-9, 1);
}

void drawButton(int x, int y, int w, int h, uint16_t bg, const char* label, uint16_t fg) {
  tft.fillRect(x, y, w, h, bg);
  tft.drawRect(x, y, w, h, C_ORANGE_D);
  tft.setTextColor(fg, bg);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(label, x + w/2, y + h/2, 2);
}

// ============================================================
//  SPLASH SCREEN
// ============================================================
void drawSplash() {
  tft.fillScreen(C_BG);
  drawCornerTicks(C_ORANGE_D);

  // K-9 large logo
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(C_ORANGE, C_BG);
  tft.setTextSize(5);
  tft.drawString("K-9", W/2, 10);
  tft.setTextSize(1);

  // Thin divider
  tft.drawFastHLine(20, 93, W-40, C_ORANGE_D);

  // Affirmative
  tft.setTextColor(C_ORANGE, C_BG);
  tft.drawString("Affirmative! K-9 ready.", W/2, 103, 1);

  // Thick orange bar
  tft.fillRect(20, 114, W-40, 7, C_ORANGE);

  // Bylines
  tft.setTextColor(C_MUTED, C_BG);
  tft.drawString("BUILT BY JOE THE BUILDER", W/2, 130, 1);
  tft.drawString("spool manager  mark 1  v0.0.0", W/2, 144, 1);

  // Animated loading bar
  tft.drawRect(20, 160, W-40, 6, C_ORANGE_D);
  for (int i = 0; i <= W-42; i += 4) {
    tft.fillRect(21, 161, i, 4, C_ORANGE);
    delay(8);
  }
}

// ============================================================
//  MAIN MENU  — 4 vertical buttons
// ============================================================
void drawMain() {
  tft.fillScreen(C_BG);
  drawHeader("K-9 mark 1");
  drawCornerTicks(C_ORANGE_D);

  if (!nfcReady) {
    tft.setTextColor(C_RED, C_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(1);
    tft.drawString("! NO READER CONNECTED !", W/2, 36, 1);
  }

  drawButton(10, 40,  300, 42, C_CARD, "QIDI",          C_ORANGE);
  drawButton(10, 88,  300, 42, C_CARD, "OPENSPOOL U1",  C_ORANGE);
  drawButton(10, 136, 300, 42, C_CARD, "ANYCUBIC",      C_ORANGE);
  drawButton(10, 184, 300, 34, C_CARD, "SETTINGS",      C_MUTED);

  drawFooter("K-9  mark 1  -  Built by Joe the Builder", C_MUTED);
}

// ============================================================
//  SUB MENU — shared layout for QIDI / OpenSpool / Anycubic
// ============================================================
void drawSubMenu(const char* title) {
  tft.fillScreen(C_BG);
  drawHeader(title);

  // Tag data area
  tft.fillRect(10, 30, W-20, 134, C_CARD);
  tft.drawRect(10, 30, W-20, 134, C_ORANGE_D);

  if (!tagData.hasData) {
    tft.setTextColor(C_MUTED, C_CARD);
    tft.setTextDatum(MC_DATUM);
    if (tagStatus == TAG_BLANK)
      tft.drawString("Blank tag detected", W/2, 97, 2);
    else
      tft.drawString("Hold tag near reader...", W/2, 97, 2);
  } else {
    tft.setTextDatum(TL_DATUM);
    tft.setTextSize(1);

    tft.setTextColor(C_MUTED, C_CARD);
    tft.drawString("MANUFACTURER", 18, 36, 1);
    tft.setTextColor(C_ORANGE, C_CARD);
    tft.drawString(tagData.manufacturer[0] ? tagData.manufacturer : "--", 18, 48, 2);

    tft.setTextColor(C_MUTED, C_CARD);
    tft.drawString("MATERIAL", 170, 36, 1);
    tft.setTextColor(C_ORANGE, C_CARD);
    tft.drawString(tagData.material[0] ? tagData.material : "--", 170, 48, 2);

    tft.setTextColor(C_MUTED, C_CARD);
    tft.drawString("COLOR", 18, 72, 1);
    uint16_t sw = tft.color565(tagData.r, tagData.g, tagData.b);
    tft.fillRect(18, 82, 70, 18, sw);
    tft.drawRect(18, 82, 70, 18, C_ORANGE_D);
    tft.setTextColor(C_BLACK, sw);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(tagData.color[0] ? tagData.color : "--", 53, 91, 1);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_MUTED, C_CARD);
    tft.drawString("NOZZLE / BED", 170, 72, 1);
    char buf[24];
    snprintf(buf, sizeof(buf), "%d-%d C", tagData.extMin, tagData.extMax);
    tft.setTextColor(C_TEXT, C_CARD);
    tft.drawString(buf, 170, 84, 1);
    snprintf(buf, sizeof(buf), "%d-%d C", tagData.bedMin, tagData.bedMax);
    tft.drawString(buf, 170, 98, 1);

    // UID
    tft.setTextColor(C_MUTED, C_CARD);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("UID", 18, 116, 1);
    String uid = "";
    for (uint8_t i = 0; i < tagData.uidLen; i++) {
      if (tagData.uid[i] < 0x10) uid += "0";
      uid += String(tagData.uid[i], HEX);
      if (i < tagData.uidLen - 1) uid += ":";
    }
    uid.toUpperCase();
    tft.setTextColor(C_TEXT, C_CARD);
    tft.drawString(uid.c_str(), 18, 128, 1);
  }

  // Action buttons with color feedback
  uint16_t writeBg = C_ORANGE_D;
  uint16_t writeFg = C_TEXT;
  if (tagStatus == TAG_WRITE_OK)   { writeBg = C_GREEN; writeFg = C_BLACK; }
  if (tagStatus == TAG_WRITE_FAIL) { writeBg = C_RED;   writeFg = C_WHITE; }

  uint16_t clearBg = tagData.hasData ? C_ORANGE_D : 0x4208;
  uint16_t clearFg = tagData.hasData ? C_TEXT      : C_MUTED;
  if (tagStatus == TAG_CLEAR_OK)   { clearBg = C_GREEN; clearFg = C_BLACK; }
  if (tagStatus == TAG_CLEAR_FAIL) { clearBg = C_RED;   clearFg = C_WHITE; }

  drawButton(10,  170, 90, 36, C_CARD,   "BACK",  C_TEXT);
  drawButton(115, 170, 90, 36, writeBg,  "WRITE", writeFg);
  drawButton(220, 170, 90, 36, clearBg,  "CLEAR", clearFg);

  drawFooter("K-9  mark 1  -  Built by Joe the Builder", C_MUTED);
}

// ============================================================
//  NO READER SCREEN
// ============================================================
void drawNoReader() {
  tft.fillScreen(C_BG);
  drawCornerTicks(C_ORANGE_D);
  tft.setTextColor(C_ORANGE, C_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  tft.drawString("NO READER", W/2, 80);
  tft.drawString("CONNECTED", W/2, 106);
  tft.setTextSize(1);
  tft.setTextColor(C_MUTED, C_BG);
  tft.drawString("GPIO 27 -> SDA  |  GPIO 22 -> SCL", W/2, 150, 1);
  tft.setTextColor(C_ORANGE, C_BG);
  tft.drawString("Tap anywhere to continue", W/2, 180, 1);
}

// ============================================================
//  ACE TAG READ
// ============================================================
bool aceReadTag() {
  uint8_t page[4];
  if (!readNfcPage(4, page)) return false;
  if (page[0] != 0x7B) return false;

  memset(tagData.manufacturer, 0, sizeof(tagData.manufacturer));
  for (uint8_t p = 0; p < 4; p++) {
    if (!readNfcPage(10 + p, page)) break;
    for (uint8_t i = 0; i < 4 && (p*4+i) < 16; i++)
      if (page[i]) tagData.manufacturer[p*4+i] = (char)page[i];
  }

  memset(tagData.material, 0, sizeof(tagData.material));
  for (uint8_t p = 0; p < 4; p++) {
    if (!readNfcPage(15 + p, page)) break;
    for (uint8_t i = 0; i < 4 && (p*4+i) < 16; i++)
      if (page[i]) tagData.material[p*4+i] = (char)page[i];
  }

  if (readNfcPage(20, page)) {
    tagData.r = page[3]; tagData.g = page[2]; tagData.b = page[1];
  }
strncpy(tagData.color, nearestColorName(tagData.r, tagData.g, tagData.b), sizeof(tagData.color));

  if (readNfcPage(24, page)) { tagData.extMin = byteToIntLE(page); tagData.extMax = byteToIntLE(page+2); }
  if (readNfcPage(29, page)) { tagData.bedMin = byteToIntLE(page); tagData.bedMax = byteToIntLE(page+2); }

  tagData.hasData = true;
  return true;
}

// ============================================================
//  ACE TAG WRITE  (stub — needs NTAG215 tags to test)
// ============================================================
bool aceWriteTag() {
  // TODO: implement when NTAG215 tags available
  return false;
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);

  tft.init();
  tft.setRotation(1);
  W = tft.width();
  H = tft.height();

  ledcAttach(BL_PIN, 5000, 8);
  ledcWrite(BL_PIN, 255);

  tft.fillScreen(C_BG);

  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  drawSplash();
  delay(500);

  Wire.begin(PN532_SDA, PN532_SCL);
  nfc.begin();
  uint32_t ver = nfc.getFirmwareVersion();
  if (!ver) {
    Serial.println("[WARN] PN532 not found");
    nfcReady = false;
    currentScreen = SCR_NO_READER;
    drawNoReader();
  } else {
    nfc.SAMConfig();
    Serial.println("[OK] PN532 ready");
    nfcReady = true;
    currentScreen = SCR_MAIN;
    drawMain();
  }

  memset(&tagData, 0, sizeof(tagData));
  tagStatus = TAG_NONE;
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  // Auto scan on Anycubic screen
  if (nfcReady && currentScreen == SCR_ANYCUBIC &&
      tagStatus != TAG_WRITE_OK && tagStatus != TAG_WRITE_FAIL) {
    uint8_t uid[7];
    uint8_t uidLen;
    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 300)) {
      memcpy(tagData.uid, uid, uidLen);
      tagData.uidLen = uidLen;
      if (aceReadTag()) {
        tagStatus = TAG_READ;
      } else {
        tagData.hasData = false;
        tagStatus = TAG_BLANK;
      }
      drawSubMenu("K-9 — Anycubic");
    }
  }
// Auto scan on QIDI screen
  if (nfcReady && currentScreen == SCR_QIDI &&
      tagStatus != TAG_WRITE_OK && tagStatus != TAG_WRITE_FAIL) {
    uint8_t uid[7];
    uint8_t uidLen;
    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 300)) {
      memcpy(tagData.uid, uid, uidLen);
      tagData.uidLen = uidLen;
      if (uidLen == 4) {
        // Mifare Classic — try to auth and read block 4
        uint8_t keya[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        if (nfc.mifareclassic_AuthenticateBlock(uid, uidLen, 4, 0, keya)) {
          uint8_t data[16] = {0};
          if (nfc.mifareclassic_ReadDataBlock(4, data)) {
            // data[0]=matID, data[1]=colID, data[2]=mfgID
           uint8_t matCode = data[0];
uint8_t colCode = data[1];
uint8_t mfgCode = data[2];
strncpy(tagData.manufacturer, qidiManufacturerName(mfgCode), sizeof(tagData.manufacturer));
strncpy(tagData.material, qidiMaterialName(matCode), sizeof(tagData.material));
if (colCode >= 1 && colCode <= 24) {
  strncpy(tagData.color, QIDI_COLORS[colCode].name, sizeof(tagData.color));
  tagData.r = QIDI_COLORS[colCode].r;
  tagData.g = QIDI_COLORS[colCode].g;
  tagData.b = QIDI_COLORS[colCode].b;
} else {
  strcpy(tagData.color, "Unknown");
  tagData.r = tagData.g = tagData.b = 128;
}
            tagData.extMin = 0; tagData.extMax = 0;
            tagData.bedMin = 0; tagData.bedMax = 0;
            tagData.hasData = true;
            tagStatus = TAG_READ;
          }
        } else {
          tagData.hasData = false;
          tagStatus = TAG_BLANK;
        }
        drawSubMenu("K-9 — QIDI");
      }
    }
  }
  // Touch
  if (ts.tirqTouched() && ts.touched()) {
    TS_Point p = ts.getPoint();
    int tx = map(p.x, 200, 3700, 0, W);
    int ty = map(p.y, 200, 3700, 0, H);

    switch (currentScreen) {

      case SCR_NO_READER:
        currentScreen = SCR_MAIN;
        drawMain();
        break;

      case SCR_MAIN:
        if (hit(10, 40,  300, 42, tx, ty)) {
          currentScreen = SCR_QIDI;
          tagStatus = TAG_NONE;
          memset(&tagData, 0, sizeof(tagData));
          drawSubMenu("K-9 — QIDI");
        }
        else if (hit(10, 88,  300, 42, tx, ty)) {
          currentScreen = SCR_OPENSPOOL;
          tagStatus = TAG_NONE;
          memset(&tagData, 0, sizeof(tagData));
          drawSubMenu("K-9 — OpenSpool U1");
        }
        else if (hit(10, 136, 300, 42, tx, ty)) {
          currentScreen = SCR_ANYCUBIC;
          tagStatus = TAG_NONE;
          memset(&tagData, 0, sizeof(tagData));
          drawSubMenu("K-9 — Anycubic");
        }
        else if (hit(10, 184, 300, 34, tx, ty)) {
          currentScreen = SCR_SETTINGS;
          // Settings screen coming soon-do nothng
        }
        break;

      case SCR_QIDI:
      case SCR_OPENSPOOL:
      case SCR_ANYCUBIC: {
        const char* title =
          (currentScreen == SCR_QIDI)      ? "K-9 — QIDI" :
          (currentScreen == SCR_OPENSPOOL)  ? "K-9 — OpenSpool U1" :
                                              "K-9 — Anycubic";
        if (hit(10, 170, 90, 36, tx, ty)) {
          // Back
          currentScreen = SCR_MAIN;
          tagStatus = TAG_NONE;
          memset(&tagData, 0, sizeof(tagData));
          drawMain();
        }
        else if (hit(115, 170, 90, 36, tx, ty)) {
          // Write
          if (!nfcReady) {
            drawFooter("No reader connected!", C_RED);
            delay(1500);
            drawSubMenu(title);
          } else {
            drawFooter("Hold tag to write...", C_ORANGE);
            bool ok = false;
            if (currentScreen == SCR_ANYCUBIC) ok = aceWriteTag();
            // QIDI and OpenSpool write coming soon
            tagStatus = ok ? TAG_WRITE_OK : TAG_WRITE_FAIL;
            drawSubMenu(title);
            delay(2000);
            tagStatus = TAG_NONE;
            drawSubMenu(title);
          }
        }
        else if (hit(220, 170, 90, 36, tx, ty) && tagData.hasData) {
          // Clear
          drawFooter("Clearing tag...", C_ORANGE);
          // Clear stub — to be implemented
          tagStatus = TAG_CLEAR_FAIL;
          drawSubMenu(title);
          delay(2000);
          tagStatus = TAG_NONE;
          memset(&tagData, 0, sizeof(tagData));
          drawSubMenu(title);
        }
        break;
      }

      default:
        break;
    }

    delay(250);
  }
}
