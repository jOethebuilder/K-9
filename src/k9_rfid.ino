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
#include <WiFi.h>
#include <esp_wifi.h>
#include <Preferences.h>

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
uint8_t backlightLevel = 255;   // 0-255, current PWM duty

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
uint32_t nfcFirmwareVersion = 0;

enum NfcTestResult { NFC_TEST_NONE, NFC_TEST_FOUND, NFC_TEST_NOT_FOUND };
NfcTestResult nfcTestResult = NFC_TEST_NONE;
uint8_t nfcTestUid[7];
uint8_t nfcTestUidLen = 0;

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
   SCR_ANYCUBIC_MATERIAL_PICKER,
   SCR_ANYCUBIC_COLOR_PICKER,
     SCR_ANYCUBIC_CUSTOM_COLOR,
   SCR_OPENSPOOL_MATERIAL_PICKER,
   SCR_OPENSPOOL_MANUFACTURER_PICKER,
   SCR_OPENSPOOL_COLOR_PICKER,
   SCR_QIDI_MATERIAL_PICKER,
   SCR_QIDI_COLOR_PICKER,
  SCR_SETTINGS,
  SCR_WIFI,
  SCR_WIFI_SCAN,
  SCR_WIFI_KEYBOARD,
   SCR_SCREENSAVER,
   SCR_NFC_STATUS,
   SCR_BACKLIGHT,
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

const uint8_t ANYCUBIC_COLORS[][3] = {
  {0x25,0xC4,0xDA}, {0x00,0x99,0xA7}, {0x0B,0x35,0x9A}, {0x0A,0x4A,0xB6},
  {0x11,0xB6,0xEE}, {0x90,0xC6,0xF5}, {0xFA,0x7C,0x0C}, {0xF7,0xB3,0x0F},
  {0xE5,0xC2,0x0F}, {0xB1,0x8F,0x2E}, {0x8D,0x76,0x6D}, {0x6C,0x4E,0x43},
  {0xE6,0x2E,0x2E}, {0xEE,0x28,0x62}, {0xEA,0x2A,0x2B}, {0xE8,0x3D,0x89},
  {0xAE,0x2E,0x65}, {0x61,0x1C,0x8B}, {0x8D,0x60,0xC7}, {0xB2,0x87,0xC9},
  {0x00,0x67,0x64}, {0x01,0x8D,0x80}, {0x42,0xB5,0xAE}, {0x1D,0x82,0x2D},
  {0x54,0xB3,0x51}, {0x72,0xE1,0x15}, {0x47,0x47,0x47}, {0x66,0x87,0x98},
  {0xB1,0xBE,0xC6}, {0x58,0x63,0x6E}, {0xF8,0xE9,0x11}, {0xF6,0xD3,0x11},
  {0xF2,0xEF,0xCE}, {0xFF,0xFF,0xFF}, {0x00,0x00,0x00}
};
const uint8_t ANYCUBIC_COLOR_COUNT = 35;


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
uint8_t aceEntryColIdx = 0;   // index into ANYCUBIC_COLORS (0..34) - currently highlighted preset
bool    aceEntryIsCustom = false;  // true if using aceCustomR/G/B/A instead of a preset
uint8_t aceCustomR = 0, aceCustomG = 0, aceCustomB = 255;
uint8_t aceWriteAlpha = 255;  // actual alpha byte written to the tag
bool    aceEntryShowingRead = false;
uint8_t aceMaterialPickerPage = 0;
uint8_t aceColorPickerPage = 0;
uint8_t osMaterialPickerPage = 0;
uint8_t osManufacturerPickerPage = 0;
uint8_t osColorPickerPage = 0;
uint8_t qidiMaterialPickerPage = 0;
uint8_t qidiColorPickerPage = 0;
uint8_t qidiEntryMatCodeIdx = 0;   // index into QIDI_MATERIAL_CODES
uint8_t qidiEntryMfgCode = 0;      // 0=Generic, 1=QIDI
uint8_t qidiEntryColIdx = 1;       // index into QIDI_COLORS (1..24)
bool    qidiEntryShowingRead = false;
bool    qidiTagPresent = false;    // tracks whether a tag is currently on the reader (SCR_QIDI screen)
bool  osTagPresent = false;      // tracks whether a tag is currently on the reader (SCR_OPENSPOOL screen)
bool    aceTagPresent = false;     // tracks whether a tag is currently 
// ── WiFi state ──────────────────────────────────────────────
Preferences prefs;
bool   wifiConnected    = false;
String wifiCurrentSSID  = "";
String wifiCurrentIP    = "";

#define WIFI_SCAN_MAX 20
String  wifiScanSSIDs[WIFI_SCAN_MAX];
bool    wifiScanOpen[WIFI_SCAN_MAX];
int     wifiScanCount = 0;
uint8_t wifiScanPage  = 0;

// ── On-screen keyboard state ────────────────────────────────
String kbBuffer     = "";
String kbTargetSSID = "";
bool   kbIsPassword = false;
bool   kbShift      = false;
bool   kbSymbols    = false;

// ── Screensaver ─────────────────────────────────────────────
unsigned long lastActivityMs = 0;
const unsigned long SCREENSAVER_TIMEOUT_MS = 60000; // 60 seconds
Screen screensaverPrevScreen = SCR_MAIN;
int ssX = 40, ssY = 40, ssVX = 2, ssVY = 2;
unsigned long lastSSFrameMs = 0;
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
//  WIFI HELPERS
// ============================================================
void saveWifiCreds(const String& ssid, const String& pass) {
  prefs.begin("k9wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
}

bool loadWifiCreds(String& ssid, String& pass) {
  prefs.begin("k9wifi", true);
  ssid = prefs.getString("ssid", "");
  pass = prefs.getString("pass", "");
  prefs.end();
  return ssid.length() > 0;
}

void clearWifiCreds() {
  prefs.begin("k9wifi", false);
  prefs.clear();
  prefs.end();
}

void wifiEventHandler(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    Serial.print("[WiFi] Disconnect reason: ");
    Serial.println(info.wifi_sta_disconnected.reason);
  }
}

bool attemptWifiConnect(const String& ssid, const String& pass, uint32_t timeoutMs) {
  Serial.print("[WiFi] Connecting to SSID: ["); Serial.print(ssid); Serial.println("]");
  Serial.print("[WiFi] Password: ["); Serial.print(pass); Serial.println("]");

  WiFi.disconnect(true, true);
  delay(100);
  WiFi.mode(WIFI_STA);
  esp_wifi_set_ps(WIFI_PS_NONE);
  delay(100);

  if (pass.length() == 0) {
    WiFi.begin(ssid.c_str());
  } else {
    WiFi.begin(ssid.c_str(), pass.c_str());
  }

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(250);
    Serial.print("[WiFi] status: "); Serial.println(WiFi.status());
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected   = true;
    wifiCurrentSSID = ssid;
    wifiCurrentIP   = WiFi.localIP().toString();
    Serial.print("[WiFi] Connected, IP: "); Serial.println(wifiCurrentIP);
    return true;
  }

  Serial.print("[WiFi] FAILED, final status: "); Serial.println(WiFi.status());
  wifiConnected   = false;
  wifiCurrentSSID = "";
  wifiCurrentIP   = "";
  return false;
}
// ============================================================
//  DRAW HELPERS
// ============================================================
uint16_t getContrastColorLocal(uint16_t bg565) {
  uint8_t r = (bg565 >> 11) & 0x1F;
  uint8_t g = (bg565 >> 5)  & 0x3F;
  uint8_t b = bg565 & 0x1F;
  float luminance = (0.299f * (r << 3) + 0.587f * (g << 2) + 0.114f * (b << 3)) / 255.0f;
  return (luminance > 0.5f) ? C_BLACK : C_WHITE;
}

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

    tft.fillRect(10, 30, W-20, 34, C_CARD);
    tft.drawRect(10, 30, W-20, 34, C_ORANGE_D);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_MUTED, C_CARD);
    tft.drawString("MANUFACTURER — tap to change", 14, 34, 1);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_ORANGE, C_CARD);
    tft.drawString(OS_MANUFACTURERS[osEntryMfgIdx], W/2, 52, 2);

    tft.fillRect(10, 68, W-20, 34, C_CARD);
    tft.drawRect(10, 68, W-20, 34, C_ORANGE_D);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_MUTED, C_CARD);
    tft.drawString("MATERIAL — tap to change", 14, 72, 1);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_ORANGE, C_CARD);
    tft.drawString(OS_MATERIALS[osEntryMatIdx].name, W/2, 90, 2);

    tft.fillRect(10, 106, W-20, 34, C_CARD);
    tft.drawRect(10, 106, W-20, 34, C_ORANGE_D);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_MUTED, C_CARD);
    tft.drawString("COLOR — tap to change", 54, 110, 1);
    uint16_t colorSw = tft.color565(QIDI_COLORS[osEntryColIdx].r, QIDI_COLORS[osEntryColIdx].g, QIDI_COLORS[osEntryColIdx].b);
    tft.fillRect(14, 114, 32, 22, colorSw);
    tft.drawRect(14, 114, 32, 22, C_ORANGE_D);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_ORANGE, C_CARD);
    tft.drawString(QIDI_COLORS[osEntryColIdx].name, W/2 + 20, 128, 1);

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
      char aceHex[10];
      snprintf(aceHex, sizeof(aceHex), "FF%02X%02X%02X", tagData.r, tagData.g, tagData.b);
      tft.drawString(aceHex, 53, 91, 1);

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
    // MATERIAL — tap the whole field to open the paged grid picker
    tft.fillRect(10, 30, W-20, 34, C_CARD);
    tft.drawRect(10, 30, W-20, 34, C_ORANGE_D);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_MUTED, C_CARD);
    tft.drawString("MATERIAL — tap to change", 14, 34, 1);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_ORANGE, C_CARD);
    tft.drawString(OS_MATERIALS[aceEntryMatIdx].name, W/2, 52, 2);

    // SIZE — unchanged, still a stepper (only 4 options)
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
    field(68, "SIZE", ACE_WEIGHT_LABELS[aceEntrySizeIdx]);

    // COLOR — tap the whole field to open the paged grid picker
    tft.fillRect(10, 108, W-20, 34, C_CARD);
    tft.drawRect(10, 108, W-20, 34, C_ORANGE_D);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_MUTED, C_CARD);
    tft.drawString("COLOR — tap to change", 54, 112, 1);
    uint8_t curR = aceEntryIsCustom ? aceCustomR : ANYCUBIC_COLORS[aceEntryColIdx][0];
    uint8_t curG = aceEntryIsCustom ? aceCustomG : ANYCUBIC_COLORS[aceEntryColIdx][1];
    uint8_t curB = aceEntryIsCustom ? aceCustomB : ANYCUBIC_COLORS[aceEntryColIdx][2];
    uint16_t curSw = tft.color565(curR, curG, curB);
    tft.fillRect(14, 116, 32, 22, curSw);
    tft.drawRect(14, 116, 32, 22, C_ORANGE_D);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_ORANGE, C_CARD);
    char aceEntryHex[10];
    snprintf(aceEntryHex, sizeof(aceEntryHex), "%02X%02X%02X%02X", aceWriteAlpha, curR, curG, curB);
    tft.drawString(aceEntryHex, W/2 + 20, 130, 1);
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
//  ANYCUBIC MATERIAL PICKER — paged grid, tap tile to select
// ============================================================
// ============================================================
//  ANYCUBIC CUSTOM COLOR — A/R/G/B steppers, live swatch + hex
// ============================================================
void drawAnycubicCustomColor() {
  tft.fillScreen(C_BG);
  drawHeader("K-9 — Customize Color");

  uint16_t sw = tft.color565(aceCustomR, aceCustomG, aceCustomB);
  tft.fillRect(90, 28, 140, 30, sw);
  tft.drawRect(90, 28, 140, 30, C_ORANGE_D);
  char hexBuf[10];
  snprintf(hexBuf, sizeof(hexBuf), "FF%02X%02X%02X", aceCustomR, aceCustomG, aceCustomB);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(getContrastColorLocal(sw), sw);
  tft.drawString(hexBuf, W/2, 43, 2);

  auto channelRow = [&](int y, const char* label, uint8_t value) {
    tft.fillRect(10, y, W-20, 28, C_CARD);
    tft.drawRect(10, y, W-20, 28, C_ORANGE_D);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_MUTED, C_CARD);
    tft.drawString(label, 44, y + 2, 1);
    char valBuf[8];
    snprintf(valBuf, sizeof(valBuf), "%d", value);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_ORANGE, C_CARD);
    tft.drawString(valBuf, W/2, y + 18, 2);
    drawButton(10, y, 26, 28, C_ORANGE_D, "-", C_TEXT);
    drawButton(W-36, y, 26, 28, C_ORANGE_D, "+", C_TEXT);
  };

  
  channelRow(96,  "RED",   aceCustomR);
  channelRow(128, "GREEN", aceCustomG);
  channelRow(160, "BLUE",  aceCustomB);

  drawButton(10,  198, 145, 32, C_CARD,  "BACK", C_TEXT);
  drawButton(165, 198, 145, 32, C_GREEN, "OK",   C_BLACK);
}
void drawAnycubicMaterialPicker() {
  tft.fillScreen(C_BG);
  drawHeader("K-9 — Select Material");

  const uint8_t cols = 3, rows = 3;
  const int tileW = 96, tileH = 40, gap = 4, x0 = 10, y0 = 30;
  const uint8_t perPage = cols * rows;
  uint8_t totalPages = (OS_MATERIALS_COUNT + perPage - 1) / perPage;
  if (aceMaterialPickerPage >= totalPages) aceMaterialPickerPage = totalPages - 1;

  uint8_t startIdx = aceMaterialPickerPage * perPage;
  for (uint8_t i = 0; i < perPage; i++) {
    uint8_t idx = startIdx + i;
    if (idx >= OS_MATERIALS_COUNT) continue;
    uint8_t col = i % cols;
    uint8_t row = i / cols;
    int x = x0 + col * (tileW + gap);
    int y = y0 + row * (tileH + gap);
    bool selected = (idx == aceEntryMatIdx);
    tft.fillRect(x, y, tileW, tileH, selected ? C_ORANGE : C_CARD);
    tft.drawRect(x, y, tileW, tileH, C_ORANGE_D);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(selected ? C_BLACK : C_TEXT, selected ? C_ORANGE : C_CARD);
    tft.drawString(OS_MATERIALS[idx].name, x + tileW/2, y + tileH/2, 1);
  }

  char pageBuf[24];
  snprintf(pageBuf, sizeof(pageBuf), "Page %d / %d", aceMaterialPickerPage + 1, totalPages);
  drawFooter(pageBuf, C_MUTED);

  drawButton(10,  176, 90, 34, C_CARD,     "BACK", C_TEXT);
  drawButton(115, 176, 90, 34, C_ORANGE_D, "<",    C_TEXT);
  drawButton(220, 176, 90, 34, C_ORANGE_D, ">",    C_TEXT);
}

// ============================================================
//  ANYCUBIC COLOR PICKER — paged grid, tap swatch to select
// ============================================================

void drawAnycubicColorPicker() {
  tft.fillScreen(C_BG);
  drawHeader("K-9 — Select Color");

  const uint8_t cols = 4, rows = 3;
  const int tileW = 70, tileH = 40, gap = 6, x0 = 10, y0 = 30;
  const uint8_t perPage = cols * rows;
  const uint8_t colorCount = ANYCUBIC_COLOR_COUNT;
  uint8_t totalPages = (colorCount + perPage - 1) / perPage;
  if (aceColorPickerPage >= totalPages) aceColorPickerPage = totalPages - 1;

  uint8_t startIdx = aceColorPickerPage * perPage;
  for (uint8_t i = 0; i < perPage; i++) {
    uint8_t colIdx = startIdx + i;
    if (colIdx >= colorCount) continue;
    uint8_t col = i % cols;
    uint8_t row = i / cols;
    int x = x0 + col * (tileW + gap);
    int y = y0 + row * (tileH + gap);
    uint16_t sw = tft.color565(ANYCUBIC_COLORS[colIdx][0], ANYCUBIC_COLORS[colIdx][1], ANYCUBIC_COLORS[colIdx][2]);
    tft.fillRect(x, y, tileW, tileH, sw);
    if (!aceEntryIsCustom && colIdx == aceEntryColIdx) {
      tft.drawRect(x, y, tileW, tileH, C_WHITE);
      tft.drawRect(x+1, y+1, tileW-2, tileH-2, C_WHITE);
    } else {
      tft.drawRect(x, y, tileW, tileH, C_ORANGE_D);
    }
  }

  char pageBuf[24];
  snprintf(pageBuf, sizeof(pageBuf), "Page %d / %d", aceColorPickerPage + 1, totalPages);
  drawFooter(pageBuf, C_MUTED);

  drawButton(10,  176, 58, 34, C_CARD,     "BACK", C_TEXT);
  drawButton(70,  176, 58, 34, C_ORANGE_D, "<",    C_TEXT);
  drawButton(130, 176, 58, 34, C_ORANGE_D, ">",    C_TEXT);
  drawButton(190, 176, 58, 34, C_GREEN,    "WRITE", C_BLACK);
  drawButton(250, 176, 60, 34, C_ORANGE,   "CUSTOM", C_BLACK);
}
void drawOpenSpoolMaterialPicker() {
  tft.fillScreen(C_BG);
  drawHeader("K-9 — Select Material");
  const uint8_t cols = 3, rows = 3;
  const int tileW = 96, tileH = 40, gap = 4, x0 = 10, y0 = 30;
  const uint8_t perPage = cols * rows;
  uint8_t totalPages = (OS_MATERIALS_COUNT + perPage - 1) / perPage;
  if (osMaterialPickerPage >= totalPages) osMaterialPickerPage = totalPages - 1;
  uint8_t startIdx = osMaterialPickerPage * perPage;
  for (uint8_t i = 0; i < perPage; i++) {
    uint8_t idx = startIdx + i;
    if (idx >= OS_MATERIALS_COUNT) continue;
    uint8_t col = i % cols; uint8_t row = i / cols;
    int x = x0 + col * (tileW + gap); int y = y0 + row * (tileH + gap);
    bool selected = (idx == osEntryMatIdx);
    tft.fillRect(x, y, tileW, tileH, selected ? C_ORANGE : C_CARD);
    tft.drawRect(x, y, tileW, tileH, C_ORANGE_D);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(selected ? C_BLACK : C_TEXT, selected ? C_ORANGE : C_CARD);
    tft.drawString(OS_MATERIALS[idx].name, x + tileW/2, y + tileH/2, 1);
  }
  char pageBuf[24];
  snprintf(pageBuf, sizeof(pageBuf), "Page %d / %d", osMaterialPickerPage + 1, totalPages);
  drawFooter(pageBuf, C_MUTED);
  drawButton(10,  176, 90, 34, C_CARD,     "BACK", C_TEXT);
  drawButton(115, 176, 90, 34, C_ORANGE_D, "<",    C_TEXT);
  drawButton(220, 176, 90, 34, C_ORANGE_D, ">",    C_TEXT);
}
void drawOpenSpoolManufacturerPicker() {
  tft.fillScreen(C_BG);
  drawHeader("K-9 — Select Manufacturer");
  const uint8_t cols = 3, rows = 3;
  const int tileW = 96, tileH = 40, gap = 4, x0 = 10, y0 = 30;
  const uint8_t perPage = cols * rows;
  uint8_t totalPages = (OS_MANUFACTURERS_COUNT + perPage - 1) / perPage;
  if (osManufacturerPickerPage >= totalPages) osManufacturerPickerPage = totalPages - 1;
  uint8_t startIdx = osManufacturerPickerPage * perPage;
  for (uint8_t i = 0; i < perPage; i++) {
    uint8_t idx = startIdx + i;
    if (idx >= OS_MANUFACTURERS_COUNT) continue;
    uint8_t col = i % cols; uint8_t row = i / cols;
    int x = x0 + col * (tileW + gap); int y = y0 + row * (tileH + gap);
    bool selected = (idx == osEntryMfgIdx);
    tft.fillRect(x, y, tileW, tileH, selected ? C_ORANGE : C_CARD);
    tft.drawRect(x, y, tileW, tileH, C_ORANGE_D);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(selected ? C_BLACK : C_TEXT, selected ? C_ORANGE : C_CARD);
    tft.drawString(OS_MANUFACTURERS[idx], x + tileW/2, y + tileH/2, 1);
  }
  char pageBuf[24];
  snprintf(pageBuf, sizeof(pageBuf), "Page %d / %d", osManufacturerPickerPage + 1, totalPages);
  drawFooter(pageBuf, C_MUTED);
  drawButton(10,  176, 90, 34, C_CARD,     "BACK", C_TEXT);
  drawButton(115, 176, 90, 34, C_ORANGE_D, "<",    C_TEXT);
  drawButton(220, 176, 90, 34, C_ORANGE_D, ">",    C_TEXT);
}
void drawOpenSpoolColorPicker() {
  tft.fillScreen(C_BG);
  drawHeader("K-9 — Select Color");
  const uint8_t cols = 4, rows = 3;
  const int tileW = 70, tileH = 40, gap = 6, x0 = 10, y0 = 30;
  const uint8_t perPage = cols * rows;
  const uint8_t colorCount = 24;
  uint8_t totalPages = (colorCount + perPage - 1) / perPage;
  if (osColorPickerPage >= totalPages) osColorPickerPage = totalPages - 1;
  uint8_t startIdx = osColorPickerPage * perPage;
  for (uint8_t i = 0; i < perPage; i++) {
    uint8_t offsetIdx = startIdx + i;
    if (offsetIdx >= colorCount) continue;
    uint8_t colIdx = offsetIdx + 1;
    uint8_t col = i % cols; uint8_t row = i / cols;
    int x = x0 + col * (tileW + gap); int y = y0 + row * (tileH + gap);
    uint16_t sw = tft.color565(QIDI_COLORS[colIdx].r, QIDI_COLORS[colIdx].g, QIDI_COLORS[colIdx].b);
    tft.fillRect(x, y, tileW, tileH, sw);
    if (colIdx == osEntryColIdx) {
      tft.drawRect(x, y, tileW, tileH, C_WHITE);
      tft.drawRect(x+1, y+1, tileW-2, tileH-2, C_WHITE);
    } else {
      tft.drawRect(x, y, tileW, tileH, C_ORANGE_D);
    }
  }
  char pageBuf[24];
  snprintf(pageBuf, sizeof(pageBuf), "Page %d / %d", osColorPickerPage + 1, totalPages);
  drawFooter(pageBuf, C_MUTED);
  drawButton(10,  176, 90, 34, C_CARD,     "BACK", C_TEXT);
  drawButton(115, 176, 90, 34, C_ORANGE_D, "<",    C_TEXT);
  drawButton(220, 176, 90, 34, C_ORANGE_D, ">",    C_TEXT);
}
void drawQidiMaterialPicker() {
  tft.fillScreen(C_BG);
  drawHeader("K-9 — Select Material");

  const uint8_t cols = 3, rows = 3;
  const int tileW = 96, tileH = 40, gap = 4, x0 = 10, y0 = 30;
  const uint8_t perPage = cols * rows;
  uint8_t totalPages = (QIDI_MATERIAL_COUNT + perPage - 1) / perPage;
  if (qidiMaterialPickerPage >= totalPages) qidiMaterialPickerPage = totalPages - 1;

  uint8_t startIdx = qidiMaterialPickerPage * perPage;
  for (uint8_t i = 0; i < perPage; i++) {
    uint8_t idx = startIdx + i;
    if (idx >= QIDI_MATERIAL_COUNT) continue;
    uint8_t col = i % cols;
    uint8_t row = i / cols;
    int x = x0 + col * (tileW + gap);
    int y = y0 + row * (tileH + gap);
    bool selected = (idx == qidiEntryMatCodeIdx);
    tft.fillRect(x, y, tileW, tileH, selected ? C_ORANGE : C_CARD);
    tft.drawRect(x, y, tileW, tileH, C_ORANGE_D);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(selected ? C_BLACK : C_TEXT, selected ? C_ORANGE : C_CARD);
    tft.drawString(qidiMaterialName(QIDI_MATERIAL_CODES[idx]), x + tileW/2, y + tileH/2, 1);
  }

  char pageBuf[24];
  snprintf(pageBuf, sizeof(pageBuf), "Page %d / %d", qidiMaterialPickerPage + 1, totalPages);
  drawFooter(pageBuf, C_MUTED);

  drawButton(10,  176, 90, 34, C_CARD,     "BACK", C_TEXT);
  drawButton(115, 176, 90, 34, C_ORANGE_D, "<",    C_TEXT);
  drawButton(220, 176, 90, 34, C_ORANGE_D, ">",    C_TEXT);
}

void drawQidiColorPicker() {
  tft.fillScreen(C_BG);
  drawHeader("K-9 — Select Color");

  const uint8_t cols = 4, rows = 3;
  const int tileW = 70, tileH = 40, gap = 6, x0 = 10, y0 = 30;
  const uint8_t perPage = cols * rows;
  const uint8_t colorCount = 24;
  uint8_t totalPages = (colorCount + perPage - 1) / perPage;
  if (qidiColorPickerPage >= totalPages) qidiColorPickerPage = totalPages - 1;

  uint8_t startIdx = qidiColorPickerPage * perPage;
  for (uint8_t i = 0; i < perPage; i++) {
    uint8_t offsetIdx = startIdx + i;
    if (offsetIdx >= colorCount) continue;
    uint8_t colIdx = offsetIdx + 1;
    uint8_t col = i % cols;
    uint8_t row = i / cols;
    int x = x0 + col * (tileW + gap);
    int y = y0 + row * (tileH + gap);
    uint8_t cr = QIDI_COLORS[colIdx].r;
    uint8_t cg = QIDI_COLORS[colIdx].g;
    uint8_t cb = QIDI_COLORS[colIdx].b;
    uint16_t sw = tft.color565(cr, cg, cb);
    tft.fillRect(x, y, tileW, tileH, sw);
    if (colIdx == qidiEntryColIdx) {
      tft.drawRect(x, y, tileW, tileH, C_WHITE);
      tft.drawRect(x+1, y+1, tileW-2, tileH-2, C_WHITE);
    } else {
      tft.drawRect(x, y, tileW, tileH, C_ORANGE_D);
    }
  }

  char pageBuf[24];
  snprintf(pageBuf, sizeof(pageBuf), "Page %d / %d", qidiColorPickerPage + 1, totalPages);
  drawFooter(pageBuf, C_MUTED);

  drawButton(10,  176, 90, 34, C_CARD,     "BACK", C_TEXT);
  drawButton(115, 176, 90, 34, C_ORANGE_D, "<",    C_TEXT);
  drawButton(220, 176, 90, 34, C_ORANGE_D, ">",    C_TEXT);
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

    tft.fillRect(10, 78, W-20, 34, C_CARD);
    tft.drawRect(10, 78, W-20, 34, C_ORANGE_D);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_MUTED, C_CARD);
    tft.drawString("MATERIAL — tap to change", 14, 82, 1);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_ORANGE, C_CARD);
    tft.drawString(qidiMaterialName(QIDI_MATERIAL_CODES[qidiEntryMatCodeIdx]), W/2, 100, 2);

    tft.fillRect(10, 126, W-20, 34, C_CARD);
    tft.drawRect(10, 126, W-20, 34, C_ORANGE_D);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_MUTED, C_CARD);
    tft.drawString("COLOR — tap to change", 54, 130, 1);
    uint16_t colorSw = tft.color565(QIDI_COLORS[qidiEntryColIdx].r, QIDI_COLORS[qidiEntryColIdx].g, QIDI_COLORS[qidiEntryColIdx].b);
    tft.fillRect(14, 134, 32, 22, colorSw);
    tft.drawRect(14, 134, 32, 22, C_ORANGE_D);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_ORANGE, C_CARD);
    tft.drawString(QIDI_COLORS[qidiEntryColIdx].name, W/2 + 20, 148, 1);
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
//  SETTINGS SCREEN
// ============================================================
void drawSettings() {
  tft.fillScreen(C_BG);
  drawHeader("K-9 — Settings");

  const int x = 10, w = W - 20, h = 24, gap = 4;
  int y = 30;

  drawButton(x, y, w, h, C_CARD, "WIFI", C_ORANGE);              y += h + gap;
  drawButton(x, y, w, h, C_CARD, "BACKLIGHT", C_MUTED);          y += h + gap;
  drawButton(x, y, w, h, C_CARD, "NFC STATUS", C_MUTED);         y += h + gap;
  drawButton(x, y, w, h, C_CARD, "TOUCH CALIBRATION", C_MUTED);  y += h + gap;
  drawButton(x, y, w, h, C_CARD, "FIRMWARE INFO", C_MUTED);      y += h + gap;
  drawButton(x, y, w, h, C_CARD, "FACTORY RESET", C_MUTED);      y += h + gap;
  drawButton(x, y, w, h, C_ORANGE_D, "BACK", C_TEXT);

  drawFooter("K-9  mark 1  -  Built by Joe the Builder", C_MUTED);
}

// ============================================================
//  BACKLIGHT SCREEN
// ============================================================
void drawBacklight() {
  tft.fillScreen(C_BG);
  drawHeader("K-9 — Backlight");

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_MUTED, C_BG);
  char curBuf[24];
  int pct = (backlightLevel * 100) / 255;
  snprintf(curBuf, sizeof(curBuf), "Current: %d%%", pct);
  tft.drawString(curBuf, W/2, 50, 2);

  const int btnW = 140, btnH = 40, gapX = 20, gapY = 10, x0 = 20, y0 = 72;
  auto levelButton = [&](int x, int y, uint8_t level, const char* label) {
    bool active = (backlightLevel == level);
    drawButton(x, y, btnW, btnH, active ? C_ORANGE : C_CARD, label, active ? C_BLACK : C_TEXT);
  };

  levelButton(x0,             y0,             64,  "25%");
  levelButton(x0 + btnW+gapX, y0,             128, "50%");
  levelButton(x0,             y0 + btnH+gapY, 191, "75%");
  levelButton(x0 + btnW+gapX, y0 + btnH+gapY, 255, "100%");

  drawFooter("K-9  mark 1  -  Built by Joe the Builder", C_MUTED);

  drawButton(10, 180, 300, 34, C_ORANGE_D, "BACK", C_TEXT);
}

// ============================================================
//  NFC STATUS SCREEN
// ============================================================
void drawNfcStatus() {
  tft.fillScreen(C_BG);
  drawHeader("K-9 — NFC Status");

  tft.fillRect(10, 30, W-20, 100, C_CARD);
  tft.drawRect(10, 30, W-20, 100, C_ORANGE_D);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_MUTED, C_CARD);
  tft.drawString("READER", 18, 38, 1);

  tft.setTextDatum(MC_DATUM);
  if (nfcReady) {
    tft.setTextColor(C_GREEN, C_CARD);
    tft.drawString("CONNECTED", W/2, 58, 2);
    char verBuf[32];
    uint8_t chip = (nfcFirmwareVersion >> 24) & 0xFF;
    uint8_t fwMaj = (nfcFirmwareVersion >> 16) & 0xFF;
    uint8_t fwMin = (nfcFirmwareVersion >> 8) & 0xFF;
    snprintf(verBuf, sizeof(verBuf), "PN5%02X  FW %d.%d", chip, fwMaj, fwMin);
    tft.setTextColor(C_TEXT, C_CARD);
    tft.drawString(verBuf, W/2, 80, 1);
  } else {
    tft.setTextColor(C_RED, C_CARD);
    tft.drawString("NOT CONNECTED", W/2, 70, 2);
  }

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_MUTED, C_CARD);
  tft.drawString("SCAN TEST RESULT", 18, 100, 1);
  tft.setTextDatum(MC_DATUM);
  if (nfcTestResult == NFC_TEST_FOUND) {
    tft.setTextColor(C_GREEN, C_CARD);
    String uid = "";
    for (uint8_t i = 0; i < nfcTestUidLen; i++) {
      if (nfcTestUid[i] < 0x10) uid += "0";
      uid += String(nfcTestUid[i], HEX);
      if (i < nfcTestUidLen - 1) uid += ":";
    }
    uid.toUpperCase();
    char foundBuf[40];
    snprintf(foundBuf, sizeof(foundBuf), "TAG FOUND  %s", uid.c_str());
    tft.drawString(foundBuf, W/2, 118, 1);
  } else if (nfcTestResult == NFC_TEST_NOT_FOUND) {
    tft.setTextColor(C_RED, C_CARD);
    tft.drawString("NO TAG DETECTED", W/2, 118, 1);
  } else {
    tft.setTextColor(C_MUTED, C_CARD);
    tft.drawString("Not tested yet", W/2, 118, 1);
  }

  drawFooter("K-9  mark 1  -  Built by Joe the Builder", C_MUTED);

  drawButton(10,  176, 145, 34, C_CARD,     "BACK",      C_TEXT);
  drawButton(165, 176, 145, 34, C_ORANGE_D, "SCAN TEST", C_TEXT);
}

// ============================================================
//  WIFI STATUS SCREEN
// ============================================================
void drawWifi() {
  tft.fillScreen(C_BG);
  drawHeader("K-9 — WiFi");

  tft.fillRect(10, 30, W-20, 100, C_CARD);
  tft.drawRect(10, 30, W-20, 100, C_ORANGE_D);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_MUTED, C_CARD);
  tft.drawString("STATUS", 18, 38, 1);

  tft.setTextDatum(MC_DATUM);
  if (wifiConnected) {
    tft.setTextColor(C_GREEN, C_CARD);
    tft.drawString("CONNECTED", W/2, 62, 2);
    tft.setTextColor(C_ORANGE, C_CARD);
    tft.drawString(wifiCurrentSSID.c_str(), W/2, 86, 2);
    tft.setTextColor(C_TEXT, C_CARD);
    tft.drawString(wifiCurrentIP.c_str(), W/2, 108, 1);
  } else {
    tft.setTextColor(C_RED, C_CARD);
    tft.drawString("NOT CONNECTED", W/2, 80, 2);
  }

  drawFooter("K-9  mark 1  -  Built by Joe the Builder", C_MUTED);

  drawButton(10,  176, 90, 34, C_CARD,     "BACK",  C_TEXT);
  drawButton(115, 176, 90, 34, C_ORANGE_D, "SCAN",  C_TEXT);
  uint16_t discBg = wifiConnected ? C_ORANGE_D : 0x4208;
  uint16_t discFg = wifiConnected ? C_TEXT     : C_MUTED;
  drawButton(220, 176, 90, 34, discBg, "FORGET", discFg);
}

// ============================================================
//  WIFI NETWORK SCAN LIST
// ============================================================
void drawWifiScan() {
  tft.fillScreen(C_BG);
  drawHeader("K-9 — Select Network");

  const int rowH = 26, gap = 2, x0 = 10, y0 = 30, rowW = W - 20;
  const uint8_t perPage = 6;
  uint8_t totalPages = wifiScanCount == 0 ? 1 : (wifiScanCount + perPage - 1) / perPage;
  if (wifiScanPage >= totalPages) wifiScanPage = totalPages - 1;

  if (wifiScanCount == 0) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_MUTED, C_BG);
    tft.drawString("No networks found", W/2, 100, 2);
  } else {
    uint8_t startIdx = wifiScanPage * perPage;
    for (uint8_t i = 0; i < perPage; i++) {
      uint8_t idx = startIdx + i;
      if (idx >= wifiScanCount) continue;
      int y = y0 + i * (rowH + gap);
      tft.fillRect(x0, y, rowW, rowH, C_CARD);
      tft.drawRect(x0, y, rowW, rowH, C_ORANGE_D);
      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(C_ORANGE, C_CARD);
      tft.drawString(wifiScanSSIDs[idx].c_str(), x0 + 8, y + 7, 2);
      tft.setTextDatum(TR_DATUM);
      tft.setTextColor(C_MUTED, C_CARD);
      tft.drawString(wifiScanOpen[idx] ? "OPEN" : "LOCK", x0 + rowW - 8, y + 8, 1);
    }
  }

  char pageBuf[24];
  snprintf(pageBuf, sizeof(pageBuf), "Page %d / %d", wifiScanPage + 1, totalPages);
  drawFooter(pageBuf, C_MUTED);

  drawButton(10,  176, 90, 34, C_CARD,     "BACK", C_TEXT);
  drawButton(115, 176, 90, 34, C_ORANGE_D, "<",    C_TEXT);
  drawButton(220, 176, 90, 34, C_ORANGE_D, ">",    C_TEXT);
}

// ============================================================
//  ON-SCREEN KEYBOARD (SSID / PASSWORD ENTRY)
// ============================================================
void drawWifiKeyboard() {
  tft.fillScreen(C_BG);
  drawHeader(kbIsPassword ? "K-9 — Enter Password" : "K-9 — Enter SSID");

  tft.fillRect(10, 30, W-20, 24, C_CARD);
  tft.drawRect(10, 30, W-20, 24, C_ORANGE_D);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_ORANGE, C_CARD);
  String preview = kbBuffer;
  if (preview.length() == 0) preview = kbIsPassword ? "(password)" : "(network name)";
  tft.drawString(preview.c_str(), W/2, 42, 2);

  const char* row1  = "1234567890";
  const char* row2  = "QWERTYUIOP";
  const char* row3  = "ASDFGHJKL";
  const char* row4L = "ZXCVBNM";
  const char* row2S = "!@#$%^&*()";
  const char* row3S = "-_=+[]{};:";
  const char* row4S = ",.<>/?~`";

  const char* r2 = kbSymbols ? row2S : row2;
  const char* r3 = kbSymbols ? row3S : row3;
  const char* r4 = kbSymbols ? row4S : row4L;

  auto drawRow = [&](const char* chars, int y) {
    int count = strlen(chars);
    int tileW = (W - 20) / count;
    for (int i = 0; i < count; i++) {
      int x = 10 + i * tileW;
      char c = chars[i];
      if (!kbSymbols) c = kbShift ? toupper(c) : tolower(c);
      char label[2] = { c, 0 };
      drawButton(x, y, tileW - 1, 24, C_CARD, label, C_TEXT);
    }
  };

  drawRow(row1, 60);
  drawRow(r2, 86);
  drawRow(r3, 112);
  drawRow(r4, 138);

  drawButton(10,  176, 48, 34, C_CARD, "CANCEL", C_TEXT);
  drawButton(60,  176, 48, 34, kbShift ? C_ORANGE : C_ORANGE_D, "SHIFT", kbShift ? C_BLACK : C_TEXT);
  drawButton(110, 176, 48, 34, C_ORANGE_D, kbSymbols ? "ABC" : "123", C_TEXT);
  drawButton(160, 176, 80, 34, C_ORANGE_D, "SPACE", C_TEXT);
  drawButton(242, 176, 34, 34, C_ORANGE_D, "<-",    C_TEXT);
  drawButton(278, 176, 32, 34, C_GREEN,    "OK",    C_BLACK);

  drawFooter("K-9  mark 1  -  Built by Joe the Builder", C_MUTED);
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
  snprintf(tagData.color, sizeof(tagData.color), "FF%02X%02X%02X", tagData.r, tagData.g, tagData.b);

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

  page[0] = aceWriteAlpha;
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
//  SCREENSAVER
// ============================================================
void enterScreensaver() {
  ssX = random(20, W - 60);
  ssY = random(20, H - 40);
  ssVX = (random(0, 2) == 0) ? 2 : -2;
  ssVY = (random(0, 2) == 0) ? 2 : -2;
  tft.fillScreen(C_BLACK);
  lastSSFrameMs = millis();
}

void drawScreensaverFrame() {
  ssX += ssVX;
  ssY += ssVY;
  if (ssX <= 0 || ssX >= W - 60) ssVX = -ssVX;
  if (ssY <= 0 || ssY >= H - 40) ssVY = -ssVY;
  tft.fillScreen(C_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_ORANGE, C_BLACK);
  tft.setTextSize(3);
  tft.drawString("K-9", ssX, ssY);
  tft.setTextSize(1);
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  WiFi.onEvent(wifiEventHandler);

  tft.init();
  tft.setRotation(1);
  W = tft.width();
  H = tft.height();

  ledcAttach(BL_PIN, 5000, 8);
  prefs.begin("k9settings", true);
  backlightLevel = prefs.getUChar("backlight", 255);
  prefs.end();
  ledcWrite(BL_PIN, backlightLevel);

  tft.fillScreen(C_BG);

  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  drawSplash();
  delay(500);

  Wire.begin(PN532_SDA, PN532_SCL);
  nfc.begin();
  uint32_t ver = nfc.getFirmwareVersion();
  nfcFirmwareVersion = ver;
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

  String savedSsid, savedPass;
  if (loadWifiCreds(savedSsid, savedPass)) {
    attemptWifiConnect(savedSsid, savedPass, 8000);
  }

  lastActivityMs = millis();
}

// ============================================================
//  LOOP
// ============================================================
void loop() {

  // Screensaver: enter after inactivity, animate while active
  if (currentScreen != SCR_SCREENSAVER && currentScreen != SCR_SPLASH && currentScreen != SCR_NO_READER &&
      millis() - lastActivityMs > SCREENSAVER_TIMEOUT_MS) {
    screensaverPrevScreen = currentScreen;
    currentScreen = SCR_SCREENSAVER;
    enterScreensaver();
  }
  if (currentScreen == SCR_SCREENSAVER && millis() - lastSSFrameMs > 40) {
    drawScreensaverFrame();
    lastSSFrameMs = millis();
  }

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
    bool found = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 150);

    if (found && !osTagPresent) {
      // New tag just placed — read it once
      osTagPresent = true;
      memcpy(tagData.uid, uid, uidLen);
      tagData.uidLen = uidLen;
      if (openSpoolReadTag()) {
        tagStatus = TAG_READ;
      } else {
        tagData.hasData = false;
        tagStatus = TAG_BLANK;
      }
      drawSubMenu("K-9 — OpenSpool U1");
    } else if (!found && osTagPresent) {
      // Tag was lifted off — reset so the next tag placed gets read
      osTagPresent = false;
      tagStatus = TAG_NONE;
      tagData.hasData = false;
      drawSubMenu("K-9 — OpenSpool U1");
    }
  }

  // Touch
  if (ts.tirqTouched() && ts.touched()) {
    TS_Point p = ts.getPoint();
    int tx = map(p.x, 200, 3700, 0, W);
    int ty = map(p.y, 200, 3700, 0, H);
    lastActivityMs = millis();

    if (currentScreen == SCR_SCREENSAVER) {
      currentScreen = screensaverPrevScreen;
      switch (currentScreen) {
        case SCR_MAIN:            drawMain(); break;
        case SCR_QIDI:            drawSubMenu("K-9 — QIDI"); break;
        case SCR_OPENSPOOL:       drawSubMenu("K-9 — OpenSpool U1"); break;
        case SCR_ANYCUBIC:        drawSubMenu("K-9 — Anycubic"); break;
        case SCR_QIDI_ENTRY:      drawQidiEntry(); break;
        case SCR_OPENSPOOL_ENTRY: drawOpenSpoolEntry(); break;
        case SCR_ANYCUBIC_ENTRY:  drawAnycubicEntry(); break;
        case SCR_SETTINGS:        drawSettings(); break;
        case SCR_WIFI:            drawWifi(); break;
        default:                  currentScreen = SCR_MAIN; drawMain(); break;
      }
      delay(250);
      return;
    }

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
            osTagPresent = false;
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
          drawSettings();
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
  if (hit(10, 30, W-20, 34, tx, ty)) {
    // Tap MATERIAL field — jump to the picker page containing the current selection
    aceEntryShowingRead = false;
    aceMaterialPickerPage = aceEntryMatIdx / 9;
    currentScreen = SCR_ANYCUBIC_MATERIAL_PICKER;
    drawAnycubicMaterialPicker();
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
  else if (hit(10, 108, W-20, 34, tx, ty)) {
    // Tap COLOR field — jump to the picker page containing the current selection
    aceEntryShowingRead = false;
    aceColorPickerPage = aceEntryColIdx / 12;
    currentScreen = SCR_ANYCUBIC_COLOR_PICKER;
    drawAnycubicColorPicker();
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
    
    snprintf(tagData.color, sizeof(tagData.color), "FF%02X%02X%02X", tagData.r, tagData.g, tagData.b);
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
      uint32_t holdStart = millis();
      uint8_t stillUid[7]; uint8_t stillLen;
      while (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, stillUid, &stillLen, 150) &&
             millis() - holdStart < 30000) {
        delay(150);
      }
      aceEntryShowingRead = false;
      drawAnycubicEntry();
    }
  }
break;
}
      case SCR_ANYCUBIC_MATERIAL_PICKER: {
        const uint8_t cols = 3, rows = 3;
        const int tileW = 96, tileH = 40, gap = 4, x0 = 10, y0 = 30;
        const uint8_t perPage = cols * rows;
        uint8_t totalPages = (OS_MATERIALS_COUNT + perPage - 1) / perPage;

        bool tileTapped = false;
        for (uint8_t i = 0; i < perPage && !tileTapped; i++) {
          uint8_t idx = aceMaterialPickerPage * perPage + i;
          if (idx >= OS_MATERIALS_COUNT) continue;
          uint8_t col = i % cols;
          uint8_t row = i / cols;
          int x = x0 + col * (tileW + gap);
          int y = y0 + row * (tileH + gap);
          if (hit(x, y, tileW, tileH, tx, ty)) {
            aceEntryMatIdx = idx;
            aceEntryNozMin = OS_MATERIALS[idx].nozzleMin;
            aceEntryNozMax = OS_MATERIALS[idx].nozzleMax;
            aceEntryBedMin = OS_MATERIALS[idx].bedMin;
            aceEntryBedMax = OS_MATERIALS[idx].bedMax;
            currentScreen = SCR_ANYCUBIC_ENTRY;
            drawAnycubicEntry();
            tileTapped = true;
          }
        }

        if (!tileTapped) {
          if (hit(10, 176, 90, 34, tx, ty)) {
            currentScreen = SCR_ANYCUBIC_ENTRY;
            drawAnycubicEntry();
          } else if (hit(115, 176, 90, 34, tx, ty)) {
            aceMaterialPickerPage = (aceMaterialPickerPage == 0) ? totalPages - 1 : aceMaterialPickerPage - 1;
            drawAnycubicMaterialPicker();
          } else if (hit(220, 176, 90, 34, tx, ty)) {
            aceMaterialPickerPage = (aceMaterialPickerPage + 1) % totalPages;
            drawAnycubicMaterialPicker();
          }
        }
        break;
      }
      case SCR_ANYCUBIC_COLOR_PICKER: {
        const uint8_t cols = 4, rows = 3;
        const int tileW = 70, tileH = 40, gap = 6, x0 = 10, y0 = 30;
        const uint8_t perPage = cols * rows;
        const uint8_t colorCount = ANYCUBIC_COLOR_COUNT;
        uint8_t totalPages = (colorCount + perPage - 1) / perPage;

        bool tileTapped = false;
        for (uint8_t i = 0; i < perPage && !tileTapped; i++) {
          uint8_t colIdx = aceColorPickerPage * perPage + i;
          if (colIdx >= colorCount) continue;
          uint8_t col = i % cols;
          uint8_t row = i / cols;
          int x = x0 + col * (tileW + gap);
          int y = y0 + row * (tileH + gap);
          if (hit(x, y, tileW, tileH, tx, ty)) {
            aceEntryColIdx = colIdx;
            aceEntryIsCustom = false;
            drawAnycubicColorPicker();
            tileTapped = true;
          }
        }

        if (!tileTapped) {
          if (hit(10, 176, 58, 34, tx, ty)) {
            currentScreen = SCR_ANYCUBIC_ENTRY;
            drawAnycubicEntry();
          } else if (hit(70, 176, 58, 34, tx, ty)) {
            aceColorPickerPage = (aceColorPickerPage == 0) ? totalPages - 1 : aceColorPickerPage - 1;
            drawAnycubicColorPicker();
          } else if (hit(130, 176, 58, 34, tx, ty)) {
            aceColorPickerPage = (aceColorPickerPage + 1) % totalPages;
            drawAnycubicColorPicker();
          } else if (hit(190, 176, 58, 34, tx, ty)) {
            aceEntryIsCustom = false;
            aceWriteAlpha = 255;
            currentScreen = SCR_ANYCUBIC_ENTRY;
            drawAnycubicEntry();
          } else if (hit(250, 176, 60, 34, tx, ty)) {
            aceCustomR = ANYCUBIC_COLORS[aceEntryColIdx][0];
            aceCustomG = ANYCUBIC_COLORS[aceEntryColIdx][1];
            aceCustomB = ANYCUBIC_COLORS[aceEntryColIdx][2];
            currentScreen = SCR_ANYCUBIC_CUSTOM_COLOR;
            drawAnycubicCustomColor();
          }
        }
        break;
      }
      case SCR_ANYCUBIC_CUSTOM_COLOR: {
        if (hit(10, 96, 26, 28, tx, ty)) {
          aceCustomR = (aceCustomR >= 8) ? aceCustomR - 8 : 0;
          drawAnycubicCustomColor();
        } else if (hit(W-36, 96, 26, 28, tx, ty)) {
          aceCustomR = (aceCustomR <= 247) ? aceCustomR + 8 : 255;
          drawAnycubicCustomColor();
        }
        else if (hit(10, 128, 26, 28, tx, ty)) {
          aceCustomG = (aceCustomG >= 8) ? aceCustomG - 8 : 0;
          drawAnycubicCustomColor();
        } else if (hit(W-36, 128, 26, 28, tx, ty)) {
          aceCustomG = (aceCustomG <= 247) ? aceCustomG + 8 : 255;
          drawAnycubicCustomColor();
        }
        else if (hit(10, 160, 26, 28, tx, ty)) {
          aceCustomB = (aceCustomB >= 8) ? aceCustomB - 8 : 0;
          drawAnycubicCustomColor();
        } else if (hit(W-36, 160, 26, 28, tx, ty)) {
          aceCustomB = (aceCustomB <= 247) ? aceCustomB + 8 : 255;
          drawAnycubicCustomColor();
        }
        else if (hit(10, 198, 145, 32, tx, ty)) {
          currentScreen = SCR_ANYCUBIC_COLOR_PICKER;
          drawAnycubicColorPicker();
        }
        else if (hit(165, 198, 145, 32, tx, ty)) {
          aceEntryIsCustom = true;
          aceWriteAlpha = 255;
          currentScreen = SCR_ANYCUBIC_ENTRY;
          drawAnycubicEntry();
        }
        break;
      }
      case SCR_OPENSPOOL_MATERIAL_PICKER: {
        const uint8_t cols = 3, rows = 3;
        const int tileW = 96, tileH = 40, gap = 4, x0 = 10, y0 = 30;
        const uint8_t perPage = cols * rows;
        uint8_t totalPages = (OS_MATERIALS_COUNT + perPage - 1) / perPage;
        bool tileTapped = false;
        for (uint8_t i = 0; i < perPage && !tileTapped; i++) {
          uint8_t idx = osMaterialPickerPage * perPage + i;
          if (idx >= OS_MATERIALS_COUNT) continue;
          uint8_t col = i % cols; uint8_t row = i / cols;
          int x = x0 + col * (tileW + gap); int y = y0 + row * (tileH + gap);
          if (hit(x, y, tileW, tileH, tx, ty)) {
            osEntryMatIdx = idx;
            osEntryNozMin = OS_MATERIALS[idx].nozzleMin;
            osEntryNozMax = OS_MATERIALS[idx].nozzleMax;
            osEntryBedMin = OS_MATERIALS[idx].bedMin;
            osEntryBedMax = OS_MATERIALS[idx].bedMax;
            currentScreen = SCR_OPENSPOOL_ENTRY;
            drawOpenSpoolEntry();
            tileTapped = true;
          }
        }
        if (!tileTapped) {
          if (hit(10, 176, 90, 34, tx, ty)) {
            currentScreen = SCR_OPENSPOOL_ENTRY;
            drawOpenSpoolEntry();
          } else if (hit(115, 176, 90, 34, tx, ty)) {
            osMaterialPickerPage = (osMaterialPickerPage == 0) ? totalPages - 1 : osMaterialPickerPage - 1;
            drawOpenSpoolMaterialPicker();
          } else if (hit(220, 176, 90, 34, tx, ty)) {
            osMaterialPickerPage = (osMaterialPickerPage + 1) % totalPages;
            drawOpenSpoolMaterialPicker();
          }
        }
        break;
      }
      case SCR_OPENSPOOL_MANUFACTURER_PICKER: {
        const uint8_t cols = 3, rows = 3;
        const int tileW = 96, tileH = 40, gap = 4, x0 = 10, y0 = 30;
        const uint8_t perPage = cols * rows;
        uint8_t totalPages = (OS_MANUFACTURERS_COUNT + perPage - 1) / perPage;
        bool tileTapped = false;
        for (uint8_t i = 0; i < perPage && !tileTapped; i++) {
          uint8_t idx = osManufacturerPickerPage * perPage + i;
          if (idx >= OS_MANUFACTURERS_COUNT) continue;
          uint8_t col = i % cols; uint8_t row = i / cols;
          int x = x0 + col * (tileW + gap); int y = y0 + row * (tileH + gap);
          if (hit(x, y, tileW, tileH, tx, ty)) {
            osEntryMfgIdx = idx;
            currentScreen = SCR_OPENSPOOL_ENTRY;
            drawOpenSpoolEntry();
            tileTapped = true;
          }
        }
        if (!tileTapped) {
          if (hit(10, 176, 90, 34, tx, ty)) {
            currentScreen = SCR_OPENSPOOL_ENTRY;
            drawOpenSpoolEntry();
          } else if (hit(115, 176, 90, 34, tx, ty)) {
            osManufacturerPickerPage = (osManufacturerPickerPage == 0) ? totalPages - 1 : osManufacturerPickerPage - 1;
            drawOpenSpoolManufacturerPicker();
          } else if (hit(220, 176, 90, 34, tx, ty)) {
            osManufacturerPickerPage = (osManufacturerPickerPage + 1) % totalPages;
            drawOpenSpoolManufacturerPicker();
          }
        }
        break;
      }
      case SCR_OPENSPOOL_COLOR_PICKER: {
        const uint8_t cols = 4, rows = 3;
        const int tileW = 70, tileH = 40, gap = 6, x0 = 10, y0 = 30;
        const uint8_t perPage = cols * rows;
        const uint8_t colorCount = 24;
        uint8_t totalPages = (colorCount + perPage - 1) / perPage;
        bool tileTapped = false;
        for (uint8_t i = 0; i < perPage && !tileTapped; i++) {
          uint8_t offsetIdx = osColorPickerPage * perPage + i;
          if (offsetIdx >= colorCount) continue;
          uint8_t colIdx = offsetIdx + 1;
          uint8_t col = i % cols; uint8_t row = i / cols;
          int x = x0 + col * (tileW + gap); int y = y0 + row * (tileH + gap);
          if (hit(x, y, tileW, tileH, tx, ty)) {
            osEntryColIdx = colIdx;
            currentScreen = SCR_OPENSPOOL_ENTRY;
            drawOpenSpoolEntry();
            tileTapped = true;
          }
        }
        if (!tileTapped) {
          if (hit(10, 176, 90, 34, tx, ty)) {
            currentScreen = SCR_OPENSPOOL_ENTRY;
            drawOpenSpoolEntry();
          } else if (hit(115, 176, 90, 34, tx, ty)) {
            osColorPickerPage = (osColorPickerPage == 0) ? totalPages - 1 : osColorPickerPage - 1;
            drawOpenSpoolColorPicker();
          } else if (hit(220, 176, 90, 34, tx, ty)) {
            osColorPickerPage = (osColorPickerPage + 1) % totalPages;
            drawOpenSpoolColorPicker();
          }
        }
        break;
      }
      case SCR_OPENSPOOL_ENTRY: {
        if (hit(10, 30, W-20, 34, tx, ty)) {
          osEntryShowingRead = false;
          osManufacturerPickerPage = osEntryMfgIdx / 9;
          currentScreen = SCR_OPENSPOOL_MANUFACTURER_PICKER;
          drawOpenSpoolManufacturerPicker();
        }
        else if (hit(10, 68, W-20, 34, tx, ty)) {
          osEntryShowingRead = false;
          osMaterialPickerPage = osEntryMatIdx / 9;
          currentScreen = SCR_OPENSPOOL_MATERIAL_PICKER;
          drawOpenSpoolMaterialPicker();
        }
        else if (hit(10, 106, W-20, 34, tx, ty)) {
          osEntryShowingRead = false;
          osColorPickerPage = (osEntryColIdx - 1) / 12;
          currentScreen = SCR_OPENSPOOL_COLOR_PICKER;
          drawOpenSpoolColorPicker();
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
          osTagPresent = false;
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
          if (osEntryShowingRead) {
            uint32_t holdStart = millis();
            uint8_t stillUid[7]; uint8_t stillLen;
            while (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, stillUid, &stillLen, 150) &&
                   millis() - holdStart < 30000) {
              delay(150);
            }
            osEntryShowingRead = false;
            drawOpenSpoolEntry();
          }
        }
        break;
      }
      case SCR_QIDI_MATERIAL_PICKER: {
        const uint8_t cols = 3, rows = 3;
        const int tileW = 96, tileH = 40, gap = 4, x0 = 10, y0 = 30;
        const uint8_t perPage = cols * rows;
        uint8_t totalPages = (QIDI_MATERIAL_COUNT + perPage - 1) / perPage;
        bool tileTapped = false;
        for (uint8_t i = 0; i < perPage && !tileTapped; i++) {
          uint8_t idx = qidiMaterialPickerPage * perPage + i;
          if (idx >= QIDI_MATERIAL_COUNT) continue;
          uint8_t col = i % cols; uint8_t row = i / cols;
          int x = x0 + col * (tileW + gap); int y = y0 + row * (tileH + gap);
          if (hit(x, y, tileW, tileH, tx, ty)) {
            qidiEntryMatCodeIdx = idx;
            currentScreen = SCR_QIDI_ENTRY;
            drawQidiEntry();
            tileTapped = true;
          }
        }
        if (!tileTapped) {
          if (hit(10, 176, 90, 34, tx, ty)) {
            currentScreen = SCR_QIDI_ENTRY;
            drawQidiEntry();
          } else if (hit(115, 176, 90, 34, tx, ty)) {
            qidiMaterialPickerPage = (qidiMaterialPickerPage == 0) ? totalPages - 1 : qidiMaterialPickerPage - 1;
            drawQidiMaterialPicker();
          } else if (hit(220, 176, 90, 34, tx, ty)) {
            qidiMaterialPickerPage = (qidiMaterialPickerPage + 1) % totalPages;
            drawQidiMaterialPicker();
          }
        }
        break;
      }
      case SCR_QIDI_COLOR_PICKER: {
        const uint8_t cols = 4, rows = 3;
        const int tileW = 70, tileH = 40, gap = 6, x0 = 10, y0 = 30;
        const uint8_t perPage = cols * rows;
        const uint8_t colorCount = 24;
        uint8_t totalPages = (colorCount + perPage - 1) / perPage;
        bool tileTapped = false;
        for (uint8_t i = 0; i < perPage && !tileTapped; i++) {
          uint8_t offsetIdx = qidiColorPickerPage * perPage + i;
          if (offsetIdx >= colorCount) continue;
          uint8_t colIdx = offsetIdx + 1;
          uint8_t col = i % cols; uint8_t row = i / cols;
          int x = x0 + col * (tileW + gap); int y = y0 + row * (tileH + gap);
          if (hit(x, y, tileW, tileH, tx, ty)) {
            qidiEntryColIdx = colIdx;
            currentScreen = SCR_QIDI_ENTRY;
            drawQidiEntry();
            tileTapped = true;
          }
        }
        if (!tileTapped) {
          if (hit(10, 176, 90, 34, tx, ty)) {
            currentScreen = SCR_QIDI_ENTRY;
            drawQidiEntry();
          } else if (hit(115, 176, 90, 34, tx, ty)) {
            qidiColorPickerPage = (qidiColorPickerPage == 0) ? totalPages - 1 : qidiColorPickerPage - 1;
            drawQidiColorPicker();
          } else if (hit(220, 176, 90, 34, tx, ty)) {
            qidiColorPickerPage = (qidiColorPickerPage + 1) % totalPages;
            drawQidiColorPicker();
          }
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
        else if (hit(10, 78, W-20, 34, tx, ty)) {
          qidiEntryShowingRead = false;
          qidiMaterialPickerPage = qidiEntryMatCodeIdx / 9;
          currentScreen = SCR_QIDI_MATERIAL_PICKER;
          drawQidiMaterialPicker();
        }
        else if (hit(10, 126, W-20, 34, tx, ty)) {
          qidiEntryShowingRead = false;
          qidiColorPickerPage = (qidiEntryColIdx - 1) / 12;
          currentScreen = SCR_QIDI_COLOR_PICKER;
          drawQidiColorPicker();
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
            uint32_t holdStart = millis();
            uint8_t stillUid[7]; uint8_t stillLen;
            while (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, stillUid, &stillLen, 150) &&
                   millis() - holdStart < 30000) {
              delay(150);
            }
            qidiEntryShowingRead = false;
            drawQidiEntry();
          } 
        }
     break;
      }
     case SCR_SETTINGS: {
        const int x = 10, w = W - 20, h = 24, gap = 4;
        int y0 = 30;
        if (hit(x, y0 + 0*(h+gap), w, h, tx, ty)) {
          currentScreen = SCR_WIFI;
          drawWifi();
        }
        else if (hit(x, y0 + 1*(h+gap), w, h, tx, ty)) {
          currentScreen = SCR_BACKLIGHT;
          drawBacklight();
        }
       else if (hit(x, y0 + 1*(h+gap), w, h, tx, ty)) {
          currentScreen = SCR_BACKLIGHT;
          drawBacklight();
        }
        else if (hit(x, y0 + 2*(h+gap), w, h, tx, ty)) {
          nfcTestResult = NFC_TEST_NONE;
          currentScreen = SCR_NFC_STATUS;
          drawNfcStatus();
        }
        else if (hit(x, y0 + 6*(h+gap), w, h, tx, ty)) {
          currentScreen = SCR_MAIN;
          drawMain();
        }
        // Backlight / Touch Calibration / Firmware Info / Factory
        // Reset rows are stubs for now — no action on tap yet.
        break;
      }
     case SCR_BACKLIGHT: {
        const int btnW = 140, btnH = 40, gapX = 20, gapY = 10, x0 = 20, y0 = 72;
        auto setLevel = [&](uint8_t level) {
          backlightLevel = level;
          ledcWrite(BL_PIN, backlightLevel);
          prefs.begin("k9settings", false);
          prefs.putUChar("backlight", backlightLevel);
          prefs.end();
          drawBacklight();
        };

        if (hit(x0, y0, btnW, btnH, tx, ty)) {
          setLevel(64);
        } else if (hit(x0 + btnW+gapX, y0, btnW, btnH, tx, ty)) {
          setLevel(128);
        } else if (hit(x0, y0 + btnH+gapY, btnW, btnH, tx, ty)) {
          setLevel(191);
        } else if (hit(x0 + btnW+gapX, y0 + btnH+gapY, btnW, btnH, tx, ty)) {
          setLevel(255);
        } else if (hit(10, 180, 300, 34, tx, ty)) {
          currentScreen = SCR_SETTINGS;
          drawSettings();
        }
        break;
      }
      case SCR_NFC_STATUS: {
        if (hit(10, 176, 145, 34, tx, ty)) {
          currentScreen = SCR_SETTINGS;
          drawSettings();
        }
        else if (hit(165, 176, 145, 34, tx, ty)) {
          drawFooter("Hold tag near reader...", C_ORANGE);
          uint8_t uid[7]; uint8_t uidLen;
          if (nfcReady && waitForTag(uid, &uidLen, 3000)) {
            memcpy(nfcTestUid, uid, uidLen);
            nfcTestUidLen = uidLen;
            nfcTestResult = NFC_TEST_FOUND;
          } else {
            nfcTestResult = NFC_TEST_NOT_FOUND;
          }
          drawNfcStatus();
        }
        break;
      }
      case SCR_WIFI: {
        if (hit(10, 176, 90, 34, tx, ty)) {
          currentScreen = SCR_SETTINGS;
          drawSettings();
        }
        else if (hit(115, 176, 90, 34, tx, ty)) {
          drawFooter("Scanning...", C_ORANGE);
          WiFi.mode(WIFI_STA);
          int n = WiFi.scanNetworks();
          wifiScanCount = 0;
          for (int i = 0; i < n && wifiScanCount < WIFI_SCAN_MAX; i++) {
            wifiScanSSIDs[wifiScanCount] = WiFi.SSID(i);
            wifiScanOpen[wifiScanCount] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
            wifiScanCount++;
          }
          wifiScanPage = 0;
          currentScreen = SCR_WIFI_SCAN;
          drawWifiScan();
        }
        else if (hit(220, 176, 90, 34, tx, ty) && wifiConnected) {
          WiFi.disconnect(true);
          clearWifiCreds();
          wifiConnected = false;
          wifiCurrentSSID = "";
          wifiCurrentIP = "";
          drawWifi();
        }
        break;
      }
      case SCR_WIFI_SCAN: {
        const int rowH = 26, gap = 2, x0 = 10, y0 = 30, rowW = W - 20;
        const uint8_t perPage = 6;
        uint8_t totalPages = wifiScanCount == 0 ? 1 : (wifiScanCount + perPage - 1) / perPage;

        bool rowTapped = false;
        for (uint8_t i = 0; i < perPage && !rowTapped; i++) {
          uint8_t idx = wifiScanPage * perPage + i;
          if (idx >= wifiScanCount) continue;
          int y = y0 + i * (rowH + gap);
          if (hit(x0, y, rowW, rowH, tx, ty)) {
            kbTargetSSID = wifiScanSSIDs[idx];
            if (wifiScanOpen[idx]) {
              drawFooter("Connecting...", C_ORANGE);
              bool ok = attemptWifiConnect(kbTargetSSID, "", 10000);
              if (ok) saveWifiCreds(kbTargetSSID, "");
              currentScreen = SCR_WIFI;
              drawWifi();
            } else {
              kbBuffer = "";
              kbIsPassword = true;
              kbShift = false;
              kbSymbols = false;
              currentScreen = SCR_WIFI_KEYBOARD;
              drawWifiKeyboard();
            }
            rowTapped = true;
          }
        }
        if (!rowTapped) {
          if (hit(10, 176, 90, 34, tx, ty)) {
            currentScreen = SCR_WIFI;
            drawWifi();
          } else if (hit(115, 176, 90, 34, tx, ty)) {
            wifiScanPage = (wifiScanPage == 0) ? totalPages - 1 : wifiScanPage - 1;
            drawWifiScan();
          } else if (hit(220, 176, 90, 34, tx, ty)) {
            wifiScanPage = (wifiScanPage + 1) % totalPages;
            drawWifiScan();
          }
        }
        break;
      }
      case SCR_WIFI_KEYBOARD: {
        const char* row1  = "1234567890";
        const char* row2  = "QWERTYUIOP";
        const char* row3  = "ASDFGHJKL";
        const char* row4L = "ZXCVBNM";
        const char* row2S = "!@#$%^&*()";
        const char* row3S = "-_=+[]{};:";
        const char* row4S = ",.<>/?~`";
        const char* r2 = kbSymbols ? row2S : row2;
        const char* r3 = kbSymbols ? row3S : row3;
        const char* r4 = kbSymbols ? row4S : row4L;

        bool keyTapped = false;
        auto tryRow = [&](const char* chars, int y) {
          if (keyTapped) return;
          int count = strlen(chars);
          int tileW = (W - 20) / count;
          for (int i = 0; i < count; i++) {
            int x = 10 + i * tileW;
            if (hit(x, y, tileW - 1, 24, tx, ty)) {
              char c = chars[i];
              if (!kbSymbols) c = kbShift ? toupper(c) : tolower(c);
              kbBuffer += c;
              keyTapped = true;
              return;
            }
          }
        };
        tryRow(row1, 60);
        tryRow(r2, 86);
        tryRow(r3, 112);
        tryRow(r4, 138);

        if (keyTapped) {
          drawWifiKeyboard();
        }
        else if (hit(10, 176, 48, 34, tx, ty)) {
          kbBuffer = "";
          if (kbIsPassword) {
            currentScreen = SCR_WIFI_SCAN;
            drawWifiScan();
          } else {
            currentScreen = SCR_WIFI;
            drawWifi();
          }
        }
        else if (hit(60, 176, 48, 34, tx, ty)) {
          kbShift = !kbShift;
          drawWifiKeyboard();
        }
        else if (hit(110, 176, 48, 34, tx, ty)) {
          kbSymbols = !kbSymbols;
          drawWifiKeyboard();
        }
        else if (hit(160, 176, 80, 34, tx, ty)) {
          kbBuffer += " ";
          drawWifiKeyboard();
        }
        else if (hit(242, 176, 34, 34, tx, ty)) {
          if (kbBuffer.length() > 0) kbBuffer.remove(kbBuffer.length() - 1);
          drawWifiKeyboard();
        }
        else if (hit(278, 176, 32, 34, tx, ty)) {
          if (kbIsPassword) {
            drawFooter("Connecting...", C_ORANGE);
            bool ok = attemptWifiConnect(kbTargetSSID, kbBuffer, 10000);
            if (ok) saveWifiCreds(kbTargetSSID, kbBuffer);
            kbBuffer = "";
            currentScreen = SCR_WIFI;
            drawWifi();
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
     
