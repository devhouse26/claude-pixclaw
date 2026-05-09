#pragma once

#include <Arduino.h>
#include "board_config.h"

#ifdef BOARD_XIAO_ST7735
#include <TFT_eSPI.h>
extern TFT_eSPI tft;
extern TFT_eSprite spr;

typedef struct {
  uint8_t Hours;
  uint8_t Minutes;
  uint8_t Seconds;
} RTC_TimeTypeDef;

typedef struct {
  uint8_t WeekDay;
  uint8_t Month;
  uint8_t Date;
  uint16_t Year;
} RTC_DateTypeDef;
#else
#include <M5StickCPlus.h>
#endif

void boardBegin();
void boardLoop();

void boardApplyBrightness(uint8_t level_0_4);
void boardDisplayPower(bool on);

bool boardUsbPowered();

void boardRtcSyncFromBridge(uint32_t localPseudoEpochSec);
void boardRtcRead(RTC_TimeTypeDef* tm, RTC_DateTypeDef* dt);

void boardBeep(uint16_t freq, uint16_t dur);
void boardBeepUpdate();

void boardImuInit();

void boardImuRead(float* ax, float* ay, float* az);

uint8_t boardPowerButtonFlag();

int boardBatteryMilliVolts();
int boardBatteryMilliAmps();
int boardUsbMilliVolts();
int boardPowerIcTempC();

TFT_eSPI* boardRawLcd();
