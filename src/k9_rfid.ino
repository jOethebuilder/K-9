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
#include <ArduinoJson.h>

// Compatibility with newer ESP32 board packages (core 3.x) that dropped these
#ifndef VSPI
#define VSPI 2
#endif
#ifndef HSPI
#define HSPI 1
#endif

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
   SCR_QIDI_ENTRY,
  SCR_OPENSPOOL,
  SCR_OPENSPOOL_ENTRY,
  SCR_ANYCUBIC,
   SCR_ANYCUBIC_ENTRY,
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
  char brand[20];
  char material[20];
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
const uint8_t QIDI_MATERIAL_CODES[] = {
  1, 2, 3, 4, 5, 6, 7, 8, 11, 12, 13, 14, 18, 19, 24, 25, 26, 27,
  30, 31, 32, 33, 34, 37, 38, 39, 40, 41, 42, 43, 44, 45, 47, 49, 50
};
const uint8_t QIDI_MATERIAL_COUNT = sizeof(QIDI_MATERIAL_CODES) / sizeof(QIDI_MATERIAL_CODES[0]);

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
//  OPENSPOOL LOOKUP TABLES
// ============================================================
struct OsMaterial {
  const char* name;
  int nozzleMin;
  int nozzleMax;
  int bedMin;
  int bedMax;
};

const OsMaterial OS_MATERIALS[] = {
  { "PLA",     190, 220,  40,  60 },
  { "PETG",    220, 250,  70,  90 },
  { "ABS",     230, 260,  90, 110 },
  { "ASA",     240, 270,  90, 110 },
  { "TPU",     210, 230,  30,  60 },
  { "PA",      240, 270,  70, 100 },
  { "PA12",    240, 270,  70, 100 },
  { "PC",      270, 310, 100, 120 },
  { "PEEK",    360, 400, 100, 140 },
  { "PVA",     190, 220,  45,  60 },
  { "HIPS",    230, 250,  90, 110 },
  { "PCTG",    220, 250,  70,  85 },
  { "PLA-CF",  190, 220,  45,  60 },
  { "PETG-CF", 230, 260,  70,  90 },
  { "PA-CF",   250, 280,  70, 100 },
};
const uint8_t OS_MATERIALS_COUNT = sizeof(OS_MATERIALS) / sizeof(OS_MATERIALS[0]);
const char* OS_MANUFACTURERS[] = {
  "Generic", "Snapmaker", "SUNLU", "eSun", "Jayo", "QIDI", "Bambu Lab",
  "Polymaker", "TECBEARS", "GIANTARM", "HATCHBOX", "Overture", "Prusament",
  "TINMORRY", "Kingroon", "Elegoo", "Creality", "Deeplee", "ANYCUBIC",
  "FLASHFORGE", "CC3D", "ZIRO"
};
const uint8_t OS_MANUFACTURERS_COUNT = sizeof(OS_MANUFACTURERS) / sizeof(OS_MANUFACTURERS[0]);

const char* ACE_WEIGHT_LABELS[] = { "1 KG", "750 G", "500 G", "250 G" };
const int   ACE_WEIGHT_LENGTHS[] = { 330, 247, 165, 82 };
const uint8_t ACE_WEIGHT_COUNT = 4;

// ── OpenSpool manual entry state ───────────────────────────
uint8_t osEntryMatIdx = 0;   // index into OS_MATERIALS
uint8_t osEntryMfgIdx = 0;   // index into OS_MANUFACTURERS
uint8_t osEntryColIdx = 1;   // index into QIDI_COLORS (1..24, matches nearestColorName range)
int     osEntryBedMin = OS_MATERIALS[0].bedMin;
int     osEntryBedMax = OS_MATERIALS[0].bedMax;
int     osEntryNozMin = OS_MATERIALS[0].nozzleMin;
int     osEntryNozMax = OS_MATERIALS[0].nozzleMax;
bool    osEntryShowingRead = false;   // true = entry screen is showing a READ result instead of edit fields
uint8_t aceEntryMatIdx = 0;
uint8_t aceEntrySizeIdx = 0;  
int     aceEntryBedMin = OS_MATERIALS[0].bedMin;
int     aceEntryBedMax = OS_MATERIALS[0].bedMax;
int     aceEntryNozMin = OS_MATERIALS[0].nozzleMin;
int     aceEntryNozMax = OS_MATERIALS[0].nozzleMax;
uint8_t aceEntryColIdx = 1;
bool    aceEntryShowingRead = false;
uint8_t qidiEntryMatCodeIdx = 0;   // index into QIDI_MATERIAL_CODES
uint8_t qidiEntryMfgCode = 0;      // 0=Generic, 1=QIDI
uint8_t qidiEntryColIdx = 1;       // index into QIDI_COLORS (1..24)
bool    qidiEntryShowingRead = false;
bool    qidiTagPresent = false;    // tracks whether a tag is currently on the reader (SCR_QIDI screen)
bool    aceTagPresent = false;     // tracks whether a tag is currently 
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
  for (uint8_t i = 0; i < 5; i++) {
    if (nfc.ntag2xx_ReadPage(page, out)) return true;
    delay(20);
  }
  return false;
}

bool writeNfcPage(uint8_t page, uint8_t* data) {
  uint8_t tmp[4];
  memcpy(tmp, data, 4);
  for (uint8_t i = 0; i < 5; i++) {
    if (nfc.ntag2xx_WritePage(page, tmp)) return true;
    delay(20);
  }
  return false;
}


bool waitForTag(uint8_t* uid, uint8_t* uidLen, uint32_t timeoutMs) {
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, uidLen, 100)) {
      return true;
    }
  }
  return false;
}
// ── OpenSpool Capability Container ──────────────────────────
#define NTAG_CC_PAGE 3
static const uint8_t NTAG_CC[4] = {0xE1, 0x10, 0x3E, 0x00};

bool ensureOpenSpoolCC() {
  uint8_t page[4];
  if (!readNfcPage(NTAG_CC_PAGE, page)) return false;
  if (memcmp(page, NTAG_CC, 4) == 0) return true;

  uint8_t cc[4];
  memcpy(cc, NTAG_CC, 4);
  if (!writeNfcPage(NTAG_CC_PAGE, cc)) return false;

  if (!readNfcPage(NTAG_CC_PAGE, page)) return false;
  return memcmp(page, NTAG_CC, 4) == 0;
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
if (currentScreen != SCR_OPENSPOOL) {
    drawButton(220, 170, 90, 36, clearBg,  "CLEAR", clearFg);
  }

  drawFooter("K-9  mark 1  -  Built by Joe the Builder", C_MUTED);
}

// ============================================================
//  OPENSPOOL MANUAL ENTRY SCREEN
// ============================================================
void drawOpenSpoolEntry() {
  tft.fillScreen(C_BG);
  drawHeader("K-9 — Manual Entry");

  if (osEntryShowingRead) {
    // ---- Read-result view ----
    tft.fillRect(10, 30, W-20, 140, C_CARD);
    tft.drawRect(10, 30, W-20, 140, C_ORANGE_D);
    if (!tagData.hasData) {
      tft.setTextColor(C_MUTED, C_CARD);
      tft.setTextDatum(MC_DATUM);
      tft.drawString("Blank / unreadable tag", W/2, 100, 2);
    } else {
      tft.setTextDatum(TL_DATUM);
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

      tft.setTextColor(C_GREEN, C_CARD);
      tft.setTextDatum(MC_DATUM);
      tft.drawString("READ FROM TAG", W/2, 130, 1);
    }
  } else {
    // ---- Editable entry fields ----
    auto field = [&](int y, const char* label, const char* value, uint16_t sw = 0xFFFF, bool showSwatch = false) {
      tft.fillRect(10, y, W-20, 34, C_CARD);
      tft.drawRect(10, y, W-20, 34, C_ORANGE_D);
      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(C_MUTED, C_CARD);
      tft.drawString(label, 44, y + 4, 1);
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(C_ORANGE, C_CARD);
      tft.drawString(value, W/2, y + 22, 2);
      if (showSwatch) {
        tft.fillRect(W-46, y+8, 20, 18, sw);
        tft.drawRect(W-46, y+8, 20, 18, C_ORANGE_D);
      }
      drawButton(10, y, 26, 34, C_ORANGE_D, "<", C_TEXT);
      drawButton(W-36, y, 26, 34, C_ORANGE_D, ">", C_TEXT);
    };

    field(30, "MANUFACTURER", OS_MANUFACTURERS[osEntryMfgIdx]);
    field(68, "MATERIAL", OS_MATERIALS[osEntryMatIdx].name);

    uint16_t colorSw = tft.color565(QIDI_COLORS[osEntryColIdx].r,
                                     QIDI_COLORS[osEntryColIdx].g,
                                     QIDI_COLORS[osEntryColIdx].b);
    field(106, "COLOR", QIDI_COLORS[osEntryColIdx].name, colorSw, true);

    tft.fillRect(10, 144, W-20, 26, C_CARD);
    tft.drawRect(10, 144, W-20, 26, C_ORANGE_D);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_MUTED, C_CARD);
    tft.drawString("NOZZLE", 44, 148, 1);
    char buf[24];
    snprintf(buf, sizeof(buf), "%d-%d C", osEntryNozMin, osEntryNozMax);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_TEXT, C_CARD);
    tft.drawString(buf, W/2, 157, 2);
    drawButton(10, 144, 26, 26, C_ORANGE_D, "-", C_TEXT);
    drawButton(W-36, 144, 26, 26, C_ORANGE_D, "+", C_TEXT);
  }

  drawFooter("K-9  mark 1  -  Built by Joe the Builder", C_MUTED);

  uint16_t saveBg = C_ORANGE;
  uint16_t saveFg = C_BLACK;
  if (tagStatus == TAG_WRITE_OK)   { saveBg = C_GREEN; saveFg = C_BLACK; }
  if (tagStatus == TAG_WRITE_FAIL) { saveBg = C_RED;   saveFg = C_WHITE; }

  drawButton(10,  176, 90, 34, C_CARD,     "BACK", C_TEXT);
  drawButton(115, 176, 90, 34, saveBg,     "SAVE", saveFg);
  drawButton(220, 176, 90, 34, C_ORANGE_D, "READ", C_TEXT);
}
// ============================================================
//  ANYCUBIC MANUAL ENTRY SCREEN
// ============================================================
void drawAnycubicEntry() {
  tft.fillScreen(C_BG);
  drawHeader("K-9 — Anycubic Entry");

  if (aceEntryShowingRead) {
    tft.fillRect(10, 30, W-20, 140, C_CARD);
    tft.drawRect(10, 30, W-20, 140, C_ORANGE_D);
    if (!tagData.hasData) {
      tft.setTextColor(C_MUTED, C_CARD);
      tft.setTextDatum(MC_DATUM);
      tft.drawString("Blank / unreadable tag", W/2, 100, 2);
    } else {
      tft.setTextDatum(TL_DATUM);
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

      tft.setTextColor(C_GREEN, C_CARD);
      tft.setTextDatum(MC_DATUM);
      tft.drawString("READ FROM TAG", W/2, 130, 1);
 }
  } else {
    auto field = [&](int y, const char* label, const char* value) {
      tft.fillRect(10, y, W-20, 34, C_CARD);
      tft.drawRect(10, y, W-20, 34, C_ORANGE_D);
      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(C_MUTED, C_CARD);
      tft.drawString(label, 44, y + 4, 1);
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(C_ORANGE, C_CARD);
      tft.drawString(value, W/2, y + 22, 2);
      drawButton(10, y, 26, 34, C_ORANGE_D, "<", C_TEXT);
      drawButton(W-36, y, 26, 34, C_ORANGE_D, ">", C_TEXT);
    };

    field(30, "MATERIAL", OS_MATERIALS[aceEntryMatIdx].name);
    field(68, "SIZE", ACE_WEIGHT_LABELS[aceEntrySizeIdx]);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_MUTED, C_CARD);
    tft.drawString("COLOR — tap to select", 12, 108, 1);

    for (uint8_t i = 1; i <= 24; i++) {
      uint8_t col = (i - 1) % 6;
      uint8_t row = (i - 1) / 6;
      int x = 10 + col * 50;
      int y = 120 + row * 14;
      uint16_t sw = tft.color565(QIDI_COLORS[i].r, QIDI_COLORS[i].g, QIDI_COLORS[i].b);
      tft.fillRect(x, y, 48, 12, sw);
      if (i == aceEntryColIdx) {
        tft.drawRect(x, y, 48, 12, C_WHITE);
      } else {
        tft.drawRect(x, y, 48, 12, C_ORANGE_D);
      }
    }
  }
  drawFooter("K-9  mark 1  -  Built by Joe the Builder", C_MUTED);

  uint16_t saveBg = C_ORANGE;
  uint16_t saveFg = C_BLACK;
  if (tagStatus == TAG_WRITE_OK)   { saveBg = C_GREEN; saveFg = C_BLACK; }
  if (tagStatus == TAG_WRITE_FAIL) { saveBg = C_RED;   saveFg = C_WHITE; }

  drawButton(10,  176, 90, 34, C_CARD,     "BACK", C_TEXT);
  drawButton(115, 176, 90, 34, saveBg,     "SAVE", saveFg);
  drawButton(220, 176, 90, 34, C_ORANGE_D, "READ", C_TEXT);
}
// ============================================================
//  QIDI MANUAL ENTRY SCREEN
// ============================================================
void drawQidiEntry() {
  tft.fillScreen(C_BG);
  drawHeader("K-9 — QIDI Entry");

  if (qidiEntryShowingRead) {
    tft.fillRect(10, 30, W-20, 140, C_CARD);
    tft.drawRect(10, 30, W-20, 140, C_ORANGE_D);
    if (!tagData.hasData) {
      tft.setTextColor(C_MUTED, C_CARD);
      tft.setTextDatum(MC_DATUM);
      tft.drawString("Blank / unreadable tag", W/2, 100, 2);
    } else {
      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(C_MUTED, C_CARD);
      tft.drawString("MANUFACTURER", 18, 40, 1);
      tft.setTextColor(C_ORANGE, C_CARD);
      tft.drawString(tagData.manufacturer[0] ? tagData.manufacturer : "--", 18, 52, 2);

      tft.setTextColor(C_MUTED, C_CARD);
      tft.drawString("MATERIAL", 18, 80, 1);
      tft.setTextColor(C_ORANGE, C_CARD);
      tft.drawString(tagData.material[0] ? tagData.material : "--", 18, 92, 2);

      tft.setTextColor(C_MUTED, C_CARD);
      tft.drawString("COLOR", 18, 120, 1);
      uint16_t sw = tft.color565(tagData.r, tagData.g, tagData.b);
      tft.fillRect(18, 132, 70, 18, sw);
      tft.drawRect(18, 132, 70, 18, C_ORANGE_D);
      tft.setTextColor(C_BLACK, sw);
      tft.setTextDatum(MC_DATUM);
      tft.drawString(tagData.color[0] ? tagData.color : "--", 53, 141, 1);

      tft.setTextColor(C_GREEN, C_CARD);
      tft.setTextDatum(MC_DATUM);
      tft.drawString("READ FROM TAG", W/2, 160, 1);
    }
  } else {
    auto field = [&](int y, const char* label, const char* value, uint16_t sw = 0xFFFF, bool showSwatch = false) {
      tft.fillRect(10, y, W-20, 34, C_CARD);
      tft.drawRect(10, y, W-20, 34, C_ORANGE_D);
      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(C_MUTED, C_CARD);
      tft.drawString(label, 44, y + 4, 1);
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(C_ORANGE, C_CARD);
      tft.drawString(value, W/2, y + 22, 2);
      if (showSwatch) {
        tft.fillRect(W-46, y+8, 20, 18, sw);
        tft.drawRect(W-46, y+8, 20, 18, C_ORANGE_D);
      }
      drawButton(10, y, 26, 34, C_ORANGE_D, "<", C_TEXT);
      drawButton(W-36, y, 26, 34, C_ORANGE_D, ">", C_TEXT);
    };

    field(30, "MANUFACTURER", qidiManufacturerName(qidiEntryMfgCode));
    field(78, "MATERIAL", qidiMaterialName(QIDI_MATERIAL_CODES[qidiEntryMatCodeIdx]));

    uint16_t colorSw = tft.color565(QIDI_COLORS[qidiEntryColIdx].r,
                                     QIDI_COLORS[qidiEntryColIdx].g,
                                     QIDI_COLORS[qidiEntryColIdx].b);
    field(126, "COLOR", QIDI_COLORS[qidiEntryColIdx].name, colorSw, true);
  }

  drawFooter("K-9  mark 1  -  Built by Joe the Builder", C_MUTED);

  uint16_t saveBg = C_ORANGE;
  uint16_t saveFg = C_BLACK;
  if (tagStatus == TAG_WRITE_OK)   { saveBg = C_GREEN; saveFg = C_BLACK; }
  if (tagStatus == TAG_WRITE_FAIL) { saveBg = C_RED;   saveFg = C_WHITE; }

  drawButton(10,  176, 90, 34, C_CARD,     "BACK", C_TEXT);
  drawButton(115, 176, 90, 34, saveBg,     "SAVE", saveFg);
  drawButton(220, 176, 90, 34, C_ORANGE_D, "READ", C_TEXT);
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
  for (uint8_t p = 0; p < 5; p++) {
    if (!readNfcPage(10 + p, page)) break;
    for (uint8_t i = 0; i < 4 && (p*4+i) < 20; i++)
      if (page[i]) tagData.manufacturer[p*4+i] = (char)page[i];
  }

  memset(tagData.material, 0, sizeof(tagData.material));
  for (uint8_t p = 0; p < 5; p++) {
    if (!readNfcPage(15 + p, page)) break;
    for (uint8_t i = 0; i < 4 && (p*4+i) < 20; i++)
      if (page[i]) tagData.material[p*4+i] = (char)page[i];
  }

  if (readNfcPage(20, page)) {
    // page = [Alpha, Blue, Green, Red]
    tagData.b = page[1];
    tagData.g = page[2];
    tagData.r = page[3];
  }
  strncpy(tagData.color, nearestColorName(tagData.r, tagData.g, tagData.b), sizeof(tagData.color));

  if (readNfcPage(24, page)) { tagData.extMin = byteToIntLE(page); tagData.extMax = byteToIntLE(page+2); }
  if (readNfcPage(29, page)) { tagData.bedMin = byteToIntLE(page); tagData.bedMax = byteToIntLE(page+2); }

  tagData.hasData = true;
  return true;
}
// ============================================================
//  ACE SKU LOOKUP
// ============================================================
void GetSku(const char* materialName, uint8_t* skuData /* 20 bytes */) {
  memset(skuData, 0, 20);
  const char* sku = nullptr;
  if      (strcmp(materialName, "ABS")            == 0) sku = "SHABBK-102";
  else if (strcmp(materialName, "PLA High Speed") == 0) sku = "AHHSBK-103";
  else if (strcmp(materialName, "PLA Matte")      == 0) sku = "HYGBK-102";
  else if (strcmp(materialName, "PLA Silk")       == 0) sku = "AHSCWH-102";
  else if (strcmp(materialName, "TPU")            == 0) sku = "STPBK-101";
  else if (strcmp(materialName, "PLA+")           == 0) sku = "AHPLPBK-102";
  else if (strcmp(materialName, "PLA")            == 0) sku = "AHPLBK-101";
  if (sku) strncpy((char*)skuData, sku, 20);
}
// ============================================================
//  ACE TAG WRITE  (stub — needs NTAG215 tags to test)
// ============================================================
int aceWriteLengthM = 330;
bool aceWriteTag() {
  uint8_t uid[7]; uint8_t uidLen;
  if (!waitForTag(uid, &uidLen, 5000)) return false;

  uint8_t page[4];

  page[0] = 0x7B; page[1] = 0x00; page[2] = 0x65; page[3] = 0x00;
  if (!writeNfcPage(4, page)) return false;

   uint8_t skuBuf[20];
  GetSku(tagData.material, skuBuf);
  for (uint8_t p = 0; p < 5; p++) {
    memcpy(page, skuBuf + p*4, 4);
    if (!writeNfcPage(5 + p, page)) return false;
  }

  char brandBuf[20] = {0};
  strncpy(brandBuf, "AC", 20);
  for (uint8_t p = 0; p < 5; p++) {
    memcpy(page, brandBuf + p*4, 4);
    if (!writeNfcPage(10 + p, page)) return false;
  }

  char matBuf[20] = {0};
  strncpy(matBuf, tagData.material, 20);
  for (uint8_t p = 0; p < 5; p++) {
    memcpy(page, matBuf + p*4, 4);
    if (!writeNfcPage(15 + p, page)) return false;
  }

  page[0] = 0xFF;
  page[1] = tagData.b;
  page[2] = tagData.g;
  page[3] = tagData.r;
  if (!writeNfcPage(20, page)) return false;

  intToByteLE(tagData.extMin, page);
  intToByteLE(tagData.extMax, page + 2);
  if (!writeNfcPage(24, page)) return false;

  intToByteLE(tagData.bedMin, page);
  intToByteLE(tagData.bedMax, page + 2);
  if (!writeNfcPage(29, page)) return false;

  intToByteLE(175, page);
  intToByteLE(aceWriteLengthM, page + 2);
  if (!writeNfcPage(30, page)) return false;

  page[0] = 0xE8; page[1] = 0x03; page[2] = 0x00; page[3] = 0x00;
  if (!writeNfcPage(31, page)) return false;

  return true;
}
bool qidiWriteTag() {
  uint8_t uid[7]; uint8_t uidLen;
  if (!waitForTag(uid, &uidLen, 5000)) return false;
  if (uidLen != 4) return false;   // must be Mifare Classic

  uint8_t keya[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  if (!nfc.mifareclassic_AuthenticateBlock(uid, uidLen, 4, 0, keya)) return false;

  uint8_t data[16] = {0};
  data[0] = QIDI_MATERIAL_CODES[qidiEntryMatCodeIdx];
  data[1] = qidiEntryColIdx;
  data[2] = qidiEntryMfgCode;

  return nfc.mifareclassic_WriteDataBlock(4, data);
}
// ============================================================
//  OPENSPOOL TAG READ
// ============================================================

// Find NDEF TLV record in buffer (TinkerBarn compatible)
bool findNdefTlv(const uint8_t* buf, size_t len, size_t& ndefOffset, size_t& ndefLen) {
  size_t i = 0;
  while (i < len) {
    uint8_t t = buf[i];
    if (t == 0x00) { i++; continue; }
    if (t == 0xFE || i + 1 >= len) return false;
    if (t == 0x03) {
      uint8_t l = buf[i + 1];
      if (l == 0xFF) {
        if (i + 3 >= len) return false;
        ndefLen = ((size_t)buf[i + 2] << 8) | buf[i + 3];
        ndefOffset = i + 4;
      } else {
        ndefLen = l;
        ndefOffset = i + 2;
      }
      return (ndefOffset + ndefLen <= len);
    }
    uint8_t l = buf[i + 1];
    if (l == 0xFF) {
      if (i + 3 >= len) return false;
      size_t longLen = ((size_t)buf[i + 2] << 8) | buf[i + 3];
      i += 4 + longLen;
    } else {
      i += 2 + l;
    }
  }
  return false;
}

// Parse MIME record from NDEF
bool parseMimeRecord(const uint8_t* ndef, size_t ndefLen, String& mime, String& payload) {
  if (ndefLen < 3) return false;
  uint8_t hdr = ndef[0];
  bool sr = hdr & 0x10;
  bool il = hdr & 0x08;
  uint8_t tnf = hdr & 0x07;
  if (tnf != 0x02) return false;
  size_t p = 1;
  uint8_t typeLen = ndef[p++];
  uint32_t payloadLen = 0;
  if (sr) {
    if (p >= ndefLen) return false;
    payloadLen = ndef[p++];
  } else {
    if (p + 3 >= ndefLen) return false;
    payloadLen = ((uint32_t)ndef[p] << 24) | ((uint32_t)ndef[p+1] << 16) | ((uint32_t)ndef[p+2] << 8) | ndef[p+3];
    p += 4;
  }
  uint8_t idLen = 0;
  if (il) { if (p >= ndefLen) return false; idLen = ndef[p++]; }
  if (p + typeLen + idLen + payloadLen > ndefLen) return false;
  mime = ""; payload = "";
  for (uint8_t i = 0; i < typeLen; i++) mime += (char)ndef[p + i];
  p += typeLen + idLen;
  for (uint32_t i = 0; i < payloadLen; i++) payload += (char)ndef[p + i];
  return true;
}

bool buildOpenSpoolNdefFromJson(const String& json, uint8_t* out, size_t outMax, size_t& outLen) {
  const char* mime = "application/json";
  const uint8_t typeLen = (uint8_t)strlen(mime);
  const size_t payloadLen = json.length();
  const bool shortRecord = payloadLen <= 255;
  const size_t recordLen = 1 + 1 + (shortRecord ? 1 : 4) + typeLen + payloadLen;
  const bool shortTlv = recordLen <= 254;
  const size_t totalLen = 1 + (shortTlv ? 1 : 3) + recordLen + 1;
  if (totalLen > outMax) return false;
  size_t p = 0;
  out[p++] = 0x03;
  if (shortTlv) {
    out[p++] = (uint8_t)recordLen;
  } else {
    out[p++] = 0xFF;
    out[p++] = (uint8_t)((recordLen >> 8) & 0xFF);
    out[p++] = (uint8_t)(recordLen & 0xFF);
  }
  out[p++] = shortRecord ? 0xD2 : 0xC2;
  out[p++] = typeLen;
  if (shortRecord) {
    out[p++] = (uint8_t)payloadLen;
  } else {
    out[p++] = (uint8_t)((payloadLen >> 24) & 0xFF);
    out[p++] = (uint8_t)((payloadLen >> 16) & 0xFF);
    out[p++] = (uint8_t)((payloadLen >> 8) & 0xFF);
    out[p++] = (uint8_t)(payloadLen & 0xFF);
  }
  memcpy(out + p, mime, typeLen); p += typeLen;
  memcpy(out + p, json.c_str(), payloadLen); p += payloadLen;
  out[p++] = 0xFE;
  outLen = p;
  return true;
}

bool openSpoolReadTag() {
  // Read NTAG user pages (pages 4 onwards)
  const uint8_t FIRST_PAGE = 4;
  const uint8_t MAX_PAGES  = 120;
  static uint8_t buf[480];
  memset(buf, 0, sizeof(buf));
  size_t offset = 0;
  for (uint8_t p = FIRST_PAGE; p < FIRST_PAGE + MAX_PAGES; p++) {
    uint8_t page[4];
    if (!readNfcPage(p, page)) {
      if (p < FIRST_PAGE + 4) return false;
      break;
    }
    memcpy(buf + offset, page, 4);
    offset += 4;
    delay(5);   // let the PN532/tag settle between page reads
    // Check if we found a complete NDEF record already
    size_t ndefOff = 0, ndefLen = 0;
    if (findNdefTlv(buf, offset, ndefOff, ndefLen)) {
      if (ndefOff + ndefLen <= offset) break; // got it all
    }
  }
  Serial.print("OS pages read: "); Serial.println(offset/4);
  Serial.print("findNdef: ");
  size_t dbgOff=0, dbgLen=0;
  Serial.println(findNdefTlv(buf, offset, dbgOff, dbgLen));
  size_t ndefOffset = 0, ndefLen = 0;
  if (!findNdefTlv(buf, offset, ndefOffset, ndefLen)) return false;

  String mime, payload;
  if (!parseMimeRecord(buf + ndefOffset, ndefLen, mime, payload)) return false;
  if (mime != "application/json") return false;

  JsonDocument doc;
  if (deserializeJson(doc, payload)) return false;
  if (String((const char*)doc["protocol"]) != "openspool") return false;

  strncpy(tagData.manufacturer, doc["brand"] | "Generic", sizeof(tagData.manufacturer));
  strncpy(tagData.material,     doc["type"]  | "PLA",     sizeof(tagData.material));

  const char* hex = doc["color_hex"] | "808080";
  String hexStr = String(hex);
  hexStr.replace("#", "");
  long color = strtol(hexStr.c_str(), nullptr, 16);
  tagData.r = (color >> 16) & 0xFF;
  tagData.g = (color >> 8)  & 0xFF;
  tagData.b =  color        & 0xFF;
  strncpy(tagData.color, nearestColorName(tagData.r, tagData.g, tagData.b), sizeof(tagData.color));

  tagData.extMin = doc["min_temp"]     | 190;
  tagData.extMax = doc["max_temp"]     | 220;
  tagData.bedMin = doc["bed_min_temp"] | 0;
  tagData.bedMax = doc["bed_max_temp"] | 60;

  tagData.hasData = true;
  return true;
}

// ============================================================
//  OPENSPOOL TAG WRITE
// ============================================================
bool openSpoolWriteTag() {
  uint8_t uid[7]; uint8_t uidLen;
  if (!waitForTag(uid, &uidLen, 5000)) return false;

  if (!ensureOpenSpoolCC()) return false;

  // 1. Build JSON payload from current tagData
  char hexColor[8];
  snprintf(hexColor, sizeof(hexColor), "#%02X%02X%02X", tagData.r, tagData.g, tagData.b);
  JsonDocument doc;
  doc["protocol"]     = "openspool";
  doc["brand"]        = tagData.manufacturer[0] ? tagData.manufacturer : "Generic";
  doc["type"]         = tagData.material[0] ? tagData.material : "PLA";
  doc["color_hex"]    = hexColor;
  doc["min_temp"]     = tagData.extMin;
  doc["max_temp"]     = tagData.extMax;
  doc["bed_min_temp"] = tagData.bedMin;
  doc["bed_max_temp"] = tagData.bedMax;
  String payload;
  serializeJson(doc, payload);
  size_t payloadLen = payload.length();
  // 2. Build NDEF MIME record: header, typeLen, payloadLen, type, payload
  const char* mimeType = "application/json";
  uint8_t typeLen = strlen(mimeType);
  bool shortRecord = payloadLen < 256;
  uint8_t ndef[512];
  size_t n = 0;
  ndef[n++] = shortRecord ? 0xD2 : 0xC2;   // TNF=2 (MIME), MB+ME+SR(if short)
  ndef[n++] = typeLen;
  if (shortRecord) {
    ndef[n++] = (uint8_t)payloadLen;
  } else {
    ndef[n++] = (payloadLen >> 24) & 0xFF;
    ndef[n++] = (payloadLen >> 16) & 0xFF;
    ndef[n++] = (payloadLen >> 8)  & 0xFF;
    ndef[n++] = payloadLen & 0xFF;
  }
  memcpy(ndef + n, mimeType, typeLen); n += typeLen;
  memcpy(ndef + n, payload.c_str(), payloadLen); n += payloadLen;
  // 3. Wrap in NDEF TLV: tag 0x03, length, ...ndef..., terminator 0xFE
  uint8_t buf[520];
  size_t offset = 0;
  buf[offset++] = 0x03;
  if (n < 255) {
    buf[offset++] = (uint8_t)n;
  } else {
    buf[offset++] = 0xFF;
    buf[offset++] = (n >> 8) & 0xFF;
    buf[offset++] = n & 0xFF;
  }
  memcpy(buf + offset, ndef, n); offset += n;
  buf[offset++] = 0xFE;   // terminator TLV
  // 4. Pad to 4-byte page boundary, write sequentially from page 4
  size_t totalPages = (offset + 3) / 4;
  if (totalPages > 120) return false;   // won't fit (see openSpoolReadTag MAX_PAGES)
  for (size_t p = 0; p < totalPages; p++) {
    uint8_t page[4] = {0, 0, 0, 0};
    size_t remaining = offset - (p * 4);
    size_t copyLen = remaining < 4 ? remaining : 4;
    memcpy(page, buf + (p * 4), copyLen);
    if (!writeNfcPage(4 + p, page)) return false;
    delay(5);
  }
  return true;
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

  // Auto scan on QIDI screen
  // Tracks tag presence (qidiTagPresent) so it: (1) reads once per tag placement
  // instead of flooding drawSubMenu() every loop while a tag sits still (the old
  // BACK/WRITE freeze bug), AND (2) detects when the tag is lifted off and a new
  // one is placed, so it doesn't get stuck only ever reading the first tag.
  if (nfcReady && currentScreen == SCR_QIDI) {
    uint8_t uid[7];
    uint8_t uidLen;
    bool found = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 100);

    if (found && !qidiTagPresent) {
      // New tag just placed — read it once
      qidiTagPresent = true;
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
    } else if (!found && qidiTagPresent) {
      // Tag was lifted off — reset so the next tag placed gets read
      qidiTagPresent = false;
      tagStatus = TAG_NONE;
      tagData.hasData = false;
      drawSubMenu("K-9 — QIDI");
    }
    // else: tag still resting there (found && qidiTagPresent) or still absent
    // (!found && !qidiTagPresent) — do nothing, no redraw, no reprocessing.
  }

 // Auto scan on Anycubic screen
  // Same presence-tracking pattern as QIDI: read once per tag placement,
  // and reset to "waiting" when the tag is lifted so the next one gets read.
  if (nfcReady && currentScreen == SCR_ANYCUBIC &&
      tagStatus != TAG_WRITE_OK && tagStatus != TAG_WRITE_FAIL) {
    uint8_t uid[7];
    uint8_t uidLen;
    bool found = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 150);

    if (found && !aceTagPresent) {
      // New tag just placed — read it once
      aceTagPresent = true;
      memcpy(tagData.uid, uid, uidLen);
      tagData.uidLen = uidLen;
      if (aceReadTag()) {
        tagStatus = TAG_READ;
      } else {
        tagData.hasData = false;
        tagStatus = TAG_BLANK;
      }
      drawSubMenu("K-9 — Anycubic");
    } else if (!found && aceTagPresent) {
      // Tag was lifted off — reset so the next tag placed gets read
      aceTagPresent = false;
      tagStatus = TAG_NONE;
      tagData.hasData = false;
      drawSubMenu("K-9 — Anycubic");
    }
  }

  // Auto scan on OpenSpool screen
  if (nfcReady && currentScreen == SCR_OPENSPOOL &&
      tagStatus != TAG_WRITE_OK && tagStatus != TAG_WRITE_FAIL) {
    uint8_t uid[7];
    uint8_t uidLen;
    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 150)) {
      memcpy(tagData.uid, uid, uidLen);
      tagData.uidLen = uidLen;
      if (openSpoolReadTag()) {
        tagStatus = TAG_READ;
      } else {
        tagData.hasData = false;
        tagStatus = TAG_BLANK;
      }
      drawSubMenu("K-9 — OpenSpool U1");
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
          qidiTagPresent = false;
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
          aceTagPresent = false;
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
          qidiTagPresent = false;
          memset(&tagData, 0, sizeof(tagData));
          drawMain();
        }
        else if (hit(115, 170, 90, 36, tx, ty) && currentScreen == SCR_OPENSPOOL) {
          currentScreen = SCR_OPENSPOOL_ENTRY;
          drawOpenSpoolEntry();
        }
        else if (hit(115, 170, 90, 36, tx, ty) && currentScreen == SCR_ANYCUBIC) {
          currentScreen = SCR_ANYCUBIC_ENTRY;
          drawAnycubicEntry();
        }
        else if (hit(115, 170, 90, 36, tx, ty) && currentScreen == SCR_QIDI) {
          currentScreen = SCR_QIDI_ENTRY;
          drawQidiEntry();
        }
        else if (hit(220, 170, 90, 36, tx, ty) && tagData.hasData) {
          // Clear (leftover stub, no longer used by OpenSpool/Anycubic/QIDI
          // since they all have their own entry screens now)
          drawFooter("Clearing tag...", C_ORANGE);
          tagStatus = TAG_CLEAR_FAIL;
          drawSubMenu(title);
          delay(2000);
          tagStatus = TAG_NONE;
          memset(&tagData, 0, sizeof(tagData));
          drawSubMenu(title);
        }
        break;
      }
    case SCR_ANYCUBIC_ENTRY: {
  if (hit(10, 30, 26, 34, tx, ty)) {
    aceEntryShowingRead = false;
    aceEntryMatIdx = (aceEntryMatIdx == 0) ? OS_MATERIALS_COUNT - 1 : aceEntryMatIdx - 1;
    aceEntryNozMin = OS_MATERIALS[aceEntryMatIdx].nozzleMin;
    aceEntryNozMax = OS_MATERIALS[aceEntryMatIdx].nozzleMax;
    aceEntryBedMin = OS_MATERIALS[aceEntryMatIdx].bedMin;
    aceEntryBedMax = OS_MATERIALS[aceEntryMatIdx].bedMax;
    drawAnycubicEntry();
  } else if (hit(W-36, 30, 26, 34, tx, ty)) {
    aceEntryShowingRead = false;
    aceEntryMatIdx = (aceEntryMatIdx + 1) % OS_MATERIALS_COUNT;
    aceEntryNozMin = OS_MATERIALS[aceEntryMatIdx].nozzleMin;
    aceEntryNozMax = OS_MATERIALS[aceEntryMatIdx].nozzleMax;
    aceEntryBedMin = OS_MATERIALS[aceEntryMatIdx].bedMin;
    aceEntryBedMax = OS_MATERIALS[aceEntryMatIdx].bedMax;
    drawAnycubicEntry();
  }
  else if (hit(10, 68, 26, 34, tx, ty)) {
    aceEntryShowingRead = false;
    aceEntrySizeIdx = (aceEntrySizeIdx == 0) ? ACE_WEIGHT_COUNT - 1 : aceEntrySizeIdx - 1;
    drawAnycubicEntry();
  } else if (hit(W-36, 68, 26, 34, tx, ty)) {
    aceEntryShowingRead = false;
    aceEntrySizeIdx = (aceEntrySizeIdx + 1) % ACE_WEIGHT_COUNT;
    drawAnycubicEntry();
  }
  else if (hit(10, 120, 300, 56, tx, ty)) {
    aceEntryShowingRead = false;
    int col = (tx - 10) / 50;
    int row = (ty - 120) / 14;
    if (col >= 0 && col < 6 && row >= 0 && row < 4) {
      uint8_t idx = row * 6 + col + 1;
      if (idx >= 1 && idx <= 24) aceEntryColIdx = idx;
    }
    drawAnycubicEntry();
  }
  else if (hit(10, 176, 90, 34, tx, ty)) {
    aceEntryShowingRead = false;
    currentScreen = SCR_ANYCUBIC;
    aceTagPresent = false;
    drawSubMenu("K-9 — Anycubic");
  }
  else if (hit(115, 176, 90, 34, tx, ty)) {
    aceEntryShowingRead = false;
    strncpy(tagData.manufacturer, "AC", sizeof(tagData.manufacturer));
    strncpy(tagData.material, OS_MATERIALS[aceEntryMatIdx].name, sizeof(tagData.material));
    tagData.r = QIDI_COLORS[aceEntryColIdx].r;
    tagData.g = QIDI_COLORS[aceEntryColIdx].g;
    tagData.b = QIDI_COLORS[aceEntryColIdx].b;
    strncpy(tagData.color, QIDI_COLORS[aceEntryColIdx].name, sizeof(tagData.color));
    tagData.extMin = aceEntryNozMin; tagData.extMax = aceEntryNozMax;
    tagData.bedMin = aceEntryBedMin; tagData.bedMax = aceEntryBedMax;
    tagData.hasData = true;

    aceWriteLengthM = ACE_WEIGHT_LENGTHS[aceEntrySizeIdx];

    drawFooter("Hold tag near reader...", C_ORANGE);
    bool ok = aceWriteTag();
    tagStatus = ok ? TAG_WRITE_OK : TAG_WRITE_FAIL;
    drawAnycubicEntry();
    delay(1500);
    tagStatus = TAG_NONE;
    drawAnycubicEntry();
  }
  else if (hit(220, 176, 90, 34, tx, ty)) {
    drawFooter("Hold tag to read...", C_ORANGE);
    uint8_t uid[7]; uint8_t uidLen;
    if (waitForTag(uid, &uidLen, 5000)) {
      drawFooter("Tag found — reading...", C_ORANGE);
      delay(450);
      memcpy(tagData.uid, uid, uidLen);
      tagData.uidLen = uidLen;
      if (aceReadTag()) {
        tagStatus = TAG_READ;
      } else {
        tagData.hasData = false;
        tagStatus = TAG_BLANK;
      }
      aceEntryShowingRead = true;
    } else {
      drawFooter("No tag detected", C_RED);
      delay(1200);
      aceEntryShowingRead = false;
    }
    drawAnycubicEntry();
    if (aceEntryShowingRead) {
      delay(1800);
      aceEntryShowingRead = false;
      drawAnycubicEntry();
    }
  }
  break;
}
      case SCR_OPENSPOOL_ENTRY: {
        if (hit(10, 30, 26, 34, tx, ty)) {
          osEntryShowingRead = false;
          osEntryMfgIdx = (osEntryMfgIdx == 0) ? OS_MANUFACTURERS_COUNT - 1 : osEntryMfgIdx - 1;
          drawOpenSpoolEntry();
        } else if (hit(W-36, 30, 26, 34, tx, ty)) {
          osEntryShowingRead = false;
          osEntryMfgIdx = (osEntryMfgIdx + 1) % OS_MANUFACTURERS_COUNT;
          drawOpenSpoolEntry();
        }
        else if (hit(10, 68, 26, 34, tx, ty)) {
          osEntryShowingRead = false;
          osEntryMatIdx = (osEntryMatIdx == 0) ? OS_MATERIALS_COUNT - 1 : osEntryMatIdx - 1;
          osEntryNozMin = OS_MATERIALS[osEntryMatIdx].nozzleMin;
          osEntryNozMax = OS_MATERIALS[osEntryMatIdx].nozzleMax;
          osEntryBedMin = OS_MATERIALS[osEntryMatIdx].bedMin;
          osEntryBedMax = OS_MATERIALS[osEntryMatIdx].bedMax;
          drawOpenSpoolEntry();
        } else if (hit(W-36, 68, 26, 34, tx, ty)) {
          osEntryShowingRead = false;
          osEntryMatIdx = (osEntryMatIdx + 1) % OS_MATERIALS_COUNT;
          osEntryNozMin = OS_MATERIALS[osEntryMatIdx].nozzleMin;
          osEntryNozMax = OS_MATERIALS[osEntryMatIdx].nozzleMax;
          osEntryBedMin = OS_MATERIALS[osEntryMatIdx].bedMin;
          osEntryBedMax = OS_MATERIALS[osEntryMatIdx].bedMax;
          drawOpenSpoolEntry();
        }
        else if (hit(10, 106, 26, 34, tx, ty)) {
          osEntryShowingRead = false;
          osEntryColIdx = (osEntryColIdx <= 1) ? 24 : osEntryColIdx - 1;
          drawOpenSpoolEntry();
        } else if (hit(W-36, 106, 26, 34, tx, ty)) {
          osEntryShowingRead = false;
          osEntryColIdx = (osEntryColIdx >= 24) ? 1 : osEntryColIdx + 1;
          drawOpenSpoolEntry();
        }
        else if (hit(10, 144, 26, 26, tx, ty)) {
          osEntryShowingRead = false;
          osEntryNozMin -= 5; osEntryNozMax -= 5;
          drawOpenSpoolEntry();
        } else if (hit(W-36, 144, 26, 26, tx, ty)) {
          osEntryShowingRead = false;
          osEntryNozMin += 5; osEntryNozMax += 5;
          drawOpenSpoolEntry();
        }
        else if (hit(10, 176, 90, 34, tx, ty)) {
          osEntryShowingRead = false;
          currentScreen = SCR_OPENSPOOL;
          drawSubMenu("K-9 — OpenSpool U1");
        }
        else if (hit(115, 176, 90, 34, tx, ty)) {
          osEntryShowingRead = false;
          strncpy(tagData.manufacturer, OS_MANUFACTURERS[osEntryMfgIdx], sizeof(tagData.manufacturer));
          strncpy(tagData.material, OS_MATERIALS[osEntryMatIdx].name, sizeof(tagData.material));
          strncpy(tagData.color, QIDI_COLORS[osEntryColIdx].name, sizeof(tagData.color));
          tagData.r = QIDI_COLORS[osEntryColIdx].r;
          tagData.g = QIDI_COLORS[osEntryColIdx].g;
          tagData.b = QIDI_COLORS[osEntryColIdx].b;
          tagData.extMin = osEntryNozMin; tagData.extMax = osEntryNozMax;
          tagData.bedMin = osEntryBedMin; tagData.bedMax = osEntryBedMax;
          tagData.hasData = true;

          drawFooter("Hold tag near reader...", C_ORANGE);
          bool ok = openSpoolWriteTag();
          tagStatus = ok ? TAG_WRITE_OK : TAG_WRITE_FAIL;
          drawOpenSpoolEntry();
          delay(1500);
          tagStatus = TAG_NONE;
          drawOpenSpoolEntry();
        }
        else if (hit(220, 176, 90, 34, tx, ty)) {
          drawFooter("Hold tag to read...", C_ORANGE);
          uint8_t uid[7]; uint8_t uidLen;
          if (waitForTag(uid, &uidLen, 5000)) {
            drawFooter("Tag found — reading...", C_ORANGE);
            delay(450);
            memcpy(tagData.uid, uid, uidLen);
            tagData.uidLen = uidLen;
            if (openSpoolReadTag()) {
              tagStatus = TAG_READ;
            } else {
              tagData.hasData = false;
              tagStatus = TAG_BLANK;
            }
            osEntryShowingRead = true;
          } else {
            drawFooter("No tag detected", C_RED);
            delay(1200);
            osEntryShowingRead = false;
          }
          drawOpenSpoolEntry();
        }
        break;
      }
      case SCR_QIDI_ENTRY: {
        if (hit(10, 30, 26, 34, tx, ty)) {
          qidiEntryShowingRead = false;
          qidiEntryMfgCode = (qidiEntryMfgCode == 0) ? 1 : 0;
          drawQidiEntry();
        } else if (hit(W-36, 30, 26, 34, tx, ty)) {
          qidiEntryShowingRead = false;
          qidiEntryMfgCode = (qidiEntryMfgCode == 0) ? 1 : 0;
          drawQidiEntry();
        }
        else if (hit(10, 78, 26, 34, tx, ty)) {
          qidiEntryShowingRead = false;
          qidiEntryMatCodeIdx = (qidiEntryMatCodeIdx == 0) ? QIDI_MATERIAL_COUNT - 1 : qidiEntryMatCodeIdx - 1;
          drawQidiEntry();
        } else if (hit(W-36, 78, 26, 34, tx, ty)) {
          qidiEntryShowingRead = false;
          qidiEntryMatCodeIdx = (qidiEntryMatCodeIdx + 1) % QIDI_MATERIAL_COUNT;
          drawQidiEntry();
        }
        else if (hit(10, 126, 26, 34, tx, ty)) {
          qidiEntryShowingRead = false;
          qidiEntryColIdx = (qidiEntryColIdx <= 1) ? 24 : qidiEntryColIdx - 1;
          drawQidiEntry();
        } else if (hit(W-36, 126, 26, 34, tx, ty)) {
          qidiEntryShowingRead = false;
          qidiEntryColIdx = (qidiEntryColIdx >= 24) ? 1 : qidiEntryColIdx + 1;
          drawQidiEntry();
        }
        else if (hit(10, 176, 90, 34, tx, ty)) {
          qidiEntryShowingRead = false;
          currentScreen = SCR_QIDI;
          qidiTagPresent = false;
          drawSubMenu("K-9 — QIDI");
        }
        else if (hit(115, 176, 90, 34, tx, ty)) {
          qidiEntryShowingRead = false;
          strncpy(tagData.manufacturer, qidiManufacturerName(qidiEntryMfgCode), sizeof(tagData.manufacturer));
          strncpy(tagData.material, qidiMaterialName(QIDI_MATERIAL_CODES[qidiEntryMatCodeIdx]), sizeof(tagData.material));
          strncpy(tagData.color, QIDI_COLORS[qidiEntryColIdx].name, sizeof(tagData.color));
          tagData.r = QIDI_COLORS[qidiEntryColIdx].r;
          tagData.g = QIDI_COLORS[qidiEntryColIdx].g;
          tagData.b = QIDI_COLORS[qidiEntryColIdx].b;
          tagData.hasData = true;

          drawFooter("Hold tag near reader...", C_ORANGE);
          bool ok = qidiWriteTag();
          tagStatus = ok ? TAG_WRITE_OK : TAG_WRITE_FAIL;
          drawQidiEntry();
          delay(1500);
          tagStatus = TAG_NONE;
          drawQidiEntry();
        }
        else if (hit(220, 176, 90, 34, tx, ty)) {
          drawFooter("Hold tag to read...", C_ORANGE);
          uint8_t uid[7]; uint8_t uidLen;
          if (waitForTag(uid, &uidLen, 5000)) {
            drawFooter("Tag found — reading...", C_ORANGE);
            delay(450);
            memcpy(tagData.uid, uid, uidLen);
            tagData.uidLen = uidLen;
            bool ok = false;
            if (uidLen == 4) {
              uint8_t keya[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
              if (nfc.mifareclassic_AuthenticateBlock(uid, uidLen, 4, 0, keya)) {
                uint8_t data[16] = {0};
                if (nfc.mifareclassic_ReadDataBlock(4, data)) {
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
                  ok = true;
                }
              }
            }
            if (ok) {
              tagData.hasData = true;
              tagStatus = TAG_READ;
            } else {
              tagData.hasData = false;
              tagStatus = TAG_BLANK;
            }
            qidiEntryShowingRead = true;
          } else {
            drawFooter("No tag detected", C_RED);
            delay(1200);
            qidiEntryShowingRead = false;
          }
          drawQidiEntry();
          if (qidiEntryShowingRead) {
            delay(1800);
            qidiEntryShowingRead = false;
            drawQidiEntry();
          }
        }
        break;
      }
      default:
        break;
    }

    delay(250);
  }
}
