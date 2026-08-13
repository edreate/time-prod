// OLED front-end: owns the U8g2 driver so nothing else needs to know the panel.
// Callers draw between displayBeginFrame() and displayEndFrame().
//
// Header-only: `static` state + `inline` functions, so include it from exactly
// one .cpp (main.cpp). A second includer would get its own copy of the driver.
#pragma once

#include <U8g2lib.h>

#define SCREEN_WIDTH 128

// SH1106 128x64 over hardware I2C. Wire.begin() is done by the caller - the
// bus is shared with the IMU, so no single device owns it.
static U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE);

// Centers text of any font on the panel width.
inline void displayCentered(const uint8_t *font, int y, const char *s)
{
  u8g2.setFont(font);
  u8g2.drawStr((SCREEN_WIDTH - u8g2.getStrWidth(s)) / 2, y, s);
}

inline void displayBegin()
{
  u8g2.begin();
}

// 180-degree rotation, driven by the IMU.
inline void displaySetFlipped(bool flipped)
{
  u8g2.setDisplayRotation(flipped ? U8G2_R2 : U8G2_R0);
}

inline void displayBeginFrame() // clear the off-screen buffer
{
  u8g2.clearBuffer();
}

inline void displayEndFrame() // push it to the panel
{
  u8g2.sendBuffer();
}

inline void displayText(int x, int y, const char *s) // small 6x10 line
{
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(x, y, s);
}

inline void displaySmallCentered(int y, const char *s)
{
  displayCentered(u8g2_font_6x10_tr, y, s);
}

inline void displayBigCentered(int y, const char *s)
{
  displayCentered(u8g2_font_logisoso16_tr, y, s);
}

inline void displayBoldCentered(int y, const char *s)
{
  displayCentered(u8g2_font_9x15B_tr, y, s);
}
