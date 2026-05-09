// TFT_eSPI setup for Seeed XIAO ESP32-C3 + ST7735 (same wiring as clawpet).
// Included via build flag: -include ${PROJECT_DIR}/include/xiao_st7735_user_setup.h
//
// Blank panel checklist:
// - Serial 115200: expect "[board] XIAO ST7735 init done" after boot.
// - Backlight active-low module: add -DTFT_BL_ACTIVE_LOW to build_flags in platformio.ini.
// - Wrong ST7735 profile: try exactly one of ST7735_GREENTAB128 or ST7735_BLACKTAB
//   instead of ST7735_GREENTAB3 below (matches Adafruit INITR_144GREENTAB on some batches).

#pragma once
#define ST7735_DRIVER
#define ST7735_GREENTAB3
#define TFT_RGB_ORDER TFT_BGR
#define TFT_WIDTH  128
#define TFT_HEIGHT 128

#define TFT_MOSI 10
#define TFT_SCLK 8
#define TFT_MISO -1

#define TFT_CS   3
#define TFT_DC   5
#define TFT_RST  4
#define TFT_BL   6

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

#define SPI_FREQUENCY  4000000
#define SPI_READ_FREQUENCY 4000000
