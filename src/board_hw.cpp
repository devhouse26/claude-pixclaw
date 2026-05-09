#include "board_hw.h"
#include <time.h>
#include <math.h>
#include <string.h>

#ifdef BOARD_XIAO_ST7735

#ifdef TFT_BL_ACTIVE_LOW
static constexpr uint8_t kBlOn = LOW;
static constexpr uint8_t kBlOff = HIGH;
#else
static constexpr uint8_t kBlOn = HIGH;
static constexpr uint8_t kBlOff = LOW;
#endif

TFT_eSPI tft;
TFT_eSprite spr(&tft);

// TFT_BL is active-high on typical 1.44" ST7735 modules (matches clawpet TFT_LED).
// Avoid analogWrite() on ESP32-C3 for backlight: it can leave the pin in a bad state
// or conflict with LEDC, resulting in a permanently dark panel while the TFT still works.

static uint32_t s_epochAnchor = 0;
static uint32_t s_msAnchor   = 0;
static bool     s_rtcValid   = false;

static void backlightPin(bool on) {
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, on ? kBlOn : kBlOff);
}

void boardBegin() {
  backlightPin(true);
  delay(10);
  tft.init();
  tft.setRotation(2);
  tft.fillScreen(TFT_BLACK);
  backlightPin(true);
  Serial.println("[board] XIAO ST7735 init done");
}

void boardLoop() {}

void boardApplyBrightness(uint8_t level_0_4) {
  // Display-only levels: off vs on (PWM can be added later with ledcAttach).
  backlightPin(level_0_4 > 0);
}

void boardDisplayPower(bool on) {
  backlightPin(on);
}

bool boardUsbPowered() {
#ifdef ARDUINO_USB_CDC_ON_BOOT
  return true;
#else
  return false;
#endif
}

void boardRtcSyncFromBridge(uint32_t localPseudoEpochSec) {
  s_epochAnchor = localPseudoEpochSec;
  s_msAnchor    = millis();
  s_rtcValid    = true;
}

void boardRtcRead(RTC_TimeTypeDef* tm, RTC_DateTypeDef* dt) {
  if (!tm || !dt) return;
  memset(tm, 0, sizeof(*tm));
  memset(dt, 0, sizeof(*dt));
  if (!s_rtcValid) return;

  time_t now =
      (time_t)s_epochAnchor + (time_t)((millis() - s_msAnchor) / 1000);
  struct tm lt;
  gmtime_r(&now, &lt);
  tm->Hours   = (uint8_t)lt.tm_hour;
  tm->Minutes = (uint8_t)lt.tm_min;
  tm->Seconds = (uint8_t)lt.tm_sec;
  dt->WeekDay = (uint8_t)lt.tm_wday;
  dt->Month   = (uint8_t)(lt.tm_mon + 1);
  dt->Date    = (uint8_t)lt.tm_mday;
  dt->Year    = (uint16_t)(lt.tm_year + 1900);
}

void boardBeep(uint16_t, uint16_t) {}

void boardBeepUpdate() {}

void boardImuInit() {}

void boardImuRead(float* ax, float* ay, float* az) {
  if (ax) *ax = 0;
  if (ay) *ay = 0;
  if (az) *az = 1.0f;
}

uint8_t boardPowerButtonFlag() { return 0; }

int boardBatteryMilliVolts() { return 0; }
int boardBatteryMilliAmps() { return 0; }
int boardUsbMilliVolts() { return boardUsbPowered() ? 5000 : 0; }
int boardPowerIcTempC() { return 0; }

TFT_eSPI* boardRawLcd() { return &tft; }

#else

void boardBegin() {
  M5.begin();
  M5.Lcd.setRotation(0);
  M5.Imu.Init();
  M5.Beep.begin();
}

void boardLoop() {
  M5.update();
}

void boardApplyBrightness(uint8_t level_0_4) {
  M5.Axp.ScreenBreath(20 + level_0_4 * 20);
}

void boardDisplayPower(bool on) { M5.Axp.SetLDO2(on); }

bool boardUsbPowered() { return M5.Axp.GetVBusVoltage() > 4.0f; }

void boardRtcSyncFromBridge(uint32_t localPseudoEpochSec) {
  time_t local = (time_t)localPseudoEpochSec;
  struct tm lt;
  gmtime_r(&local, &lt);
  RTC_TimeTypeDef tm = {(uint8_t)lt.tm_hour, (uint8_t)lt.tm_min,
                         (uint8_t)lt.tm_sec};
  RTC_DateTypeDef dt = {(uint8_t)lt.tm_wday, (uint8_t)(lt.tm_mon + 1),
                         (uint8_t)lt.tm_mday, (uint16_t)(lt.tm_year + 1900)};
  M5.Rtc.SetTime(&tm);
  M5.Rtc.SetDate(&dt);
}

void boardRtcRead(RTC_TimeTypeDef* tm, RTC_DateTypeDef* dt) {
  M5.Rtc.GetTime(tm);
  M5.Rtc.GetDate(dt);
}

void boardBeep(uint16_t freq, uint16_t dur) { M5.Beep.tone(freq, dur); }

void boardBeepUpdate() { M5.Beep.update(); }

void boardImuInit() { M5.Imu.Init(); }

void boardImuRead(float* ax, float* ay, float* az) {
  M5.Imu.getAccelData(ax, ay, az);
}

uint8_t boardPowerButtonFlag() { return M5.Axp.GetBtnPress(); }

int boardBatteryMilliVolts() {
  return (int)(M5.Axp.GetBatVoltage() * 1000);
}
int boardBatteryMilliAmps() { return (int)M5.Axp.GetBatCurrent(); }
int boardUsbMilliVolts() {
  return (int)(M5.Axp.GetVBusVoltage() * 1000);
}
int boardPowerIcTempC() { return (int)M5.Axp.GetTempInAXP192(); }

TFT_eSPI* boardRawLcd() { return &M5.Lcd; }

#endif
