// Status light: owns the WS2812B strip; callers pass colors, not pixels.
//
// Header-only: `static` state + `inline` functions, so include it from exactly
// one .cpp (main.cpp). A second includer would get its own copy of the strip.
#pragma once

#include <Adafruit_NeoPixel.h>

#include "../config.h"

static Adafruit_NeoPixel strip(NUM_LEDS, PIN_LED_DATA, NEO_GRB + NEO_KHZ800);

inline void ledsSetAll(uint32_t color) // 0xRRGGBB
{
  for (int i = 0; i < NUM_LEDS; i++)
    strip.setPixelColor(i, color);
  strip.show();
}

// Lights the first N LEDs in proportion to remaining/total, rest off.
inline void ledsSetProgress(long remaining, long total)
{
  int lit = 0;
  if (total > 0 && remaining > 0)
  {
    lit = (int)((remaining * (long)NUM_LEDS + total - 1) / total); // ceil
    if (lit > NUM_LEDS)
      lit = NUM_LEDS;
  }
  for (int i = 0; i < NUM_LEDS; i++)
  {
    strip.setPixelColor(i, i < lit ? COLOR_PROGRESS : COLOR_OFF);
  }
  strip.show();
}

inline void ledsBegin()
{
  strip.begin();
  strip.setBrightness(LED_BRIGHTNESS);
  ledsSetAll(COLOR_OFF);
}
