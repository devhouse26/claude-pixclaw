// ESP32-C3 + TFT_eSPI: IDF's soc.h defines REG_SPI_BASE(i) only for (i)==2, but
// SPI2_HOST is 1. TFT_eSPI passes SPI2_HOST into SPI_*_REG(SPI_PORT), so register
// pointers resolve to 0x0 / 0x10 and the first SPI register write faults (MTVAL 0x10).
// TFT_eSPI_ESP32_C3.h tries #ifndef REG_SPI_BASE, which never triggers once soc.h ran.
#pragma once

// Use BOARD_* so this applies when the preprocessor runs (-include), before IDF
// target macros are visible in some translation units (e.g. vendor libs).
#if defined(BOARD_XIAO_ST7735)
#include "soc/soc.h"
#undef REG_SPI_BASE
#define REG_SPI_BASE(i) (DR_REG_SPI2_BASE)
#endif
