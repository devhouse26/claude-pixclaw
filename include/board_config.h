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

#if defined(BOARD_DISPLAY_ONLY)
// scale=1 buddy (BUDDY_Y_BASE=2) fills y=0..42; stats render below that.
inline constexpr int PEEK_TOP_UI() { return 42; }
#else
inline constexpr int PEEK_TOP_UI() { return DISPLAY_H * 70 / 240; }
#endif
inline constexpr int GIF_HOME_CENTER_Y() { return DISPLAY_H * 140 / 240; }
