#pragma once

#if defined(BOARD_XIAO_ST7735)
enum : int {
  DISPLAY_W = 128,
  DISPLAY_H = 128,
};
#else
enum : int {
  DISPLAY_W = 135,
  DISPLAY_H = 240,
};
#endif

inline constexpr int PEEK_TOP_UI() { return DISPLAY_H * 70 / 240; }
inline constexpr int GIF_HOME_CENTER_Y() { return DISPLAY_H * 140 / 240; }
