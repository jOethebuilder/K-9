// ============================================================
//  TFT_eSPI User_Setup.h for ESP32-2432S028R (CYD)
//  K.9 RFID Spool Management
//
//  INSTRUCTIONS:
//  After installing TFT_eSPI via Arduino Library Manager,
//  copy THIS file to replace the User_Setup.h found in:
//  Arduino/libraries/TFT_eSPI/User_Setup.h
// ============================================================

#define ILI9341_DRIVER

#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// CYD display SPI pins (HSPI)
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST  -1
#define TFT_BL   21

// Touch SPI pins (VSPI) - handled by XPT2046_Touchscreen library
#define TOUCH_CS 33

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF

#define SMOOTH_FONT

#define SPI_FREQUENCY        55000000
#define SPI_READ_FREQUENCY   20000000
#define SPI_TOUCH_FREQUENCY   2500000
