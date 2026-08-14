// 5-way switch, debounced. Reports logical (on-screen) directions; the
// physical F/B/L/R remap lives here, at the wires where it belongs.
//
// Header-only: `static` state + `inline` functions, so include it from exactly
// one .cpp (main.cpp). A second includer would get its own copy of the buttons.
#pragma once

#include <Arduino.h>

#include "../config.h"

struct ButtonEvents
{
  bool up, down, left, right, click, longClick;
};

// One debounced input, with long-press detection for the center click.
struct Button
{
  explicit Button(uint8_t p) : pin(p) {}

  uint8_t pin;
  bool stable = true; // true = released (pin idles high via pull-up)
  bool lastReading = true;
  unsigned long lastChangeMs = 0;
  unsigned long pressedAtMs = 0;
  bool clicked = false;     // true for exactly one poll after a short press
  bool longPressed = false; // true for exactly one poll once held > LONG_PRESS_MS
  bool longFired = false;   // internal: suppresses the trailing click after a long press

  void begin() { pinMode(pin, INPUT_PULLUP); }

  void update()
  {
    clicked = false;
    longPressed = false;
    bool reading = digitalRead(pin);
    if (reading != lastReading)
    {
      lastChangeMs = millis();
      lastReading = reading;
    }
    if ((millis() - lastChangeMs) > BTN_DEBOUNCE_MS && reading != stable)
    {
      stable = reading;
      if (stable == LOW)
      {
        pressedAtMs = millis();
        longFired = false;
      }
      else if (!longFired)
      {
        clicked = true;
      }
    }
    if (stable == LOW && !longFired && (millis() - pressedAtMs) > LONG_PRESS_MS)
    {
      longFired = true;
      longPressed = true;
    }
  }
};

static Button btnF{PIN_BTN_F}, btnB{PIN_BTN_B}, btnL{PIN_BTN_L},
    btnR{PIN_BTN_R}, btnPress{PIN_BTN_PRESS};

inline void buttonsBegin()
{
  btnF.begin();
  btnB.begin();
  btnL.begin();
  btnR.begin();
  btnPress.begin();
}

// Call once per loop; every flag is a one-shot.
//
// The module sits rotated relative to the display, so its silkscreen F/B/L/R
// does not match the on-screen directions. The mapping is fixed at the wires -
// it does not follow the IMU flip.
inline ButtonEvents buttonsPoll()
{
  btnF.update();
  btnB.update();
  btnL.update();
  btnR.update();
  btnPress.update();

  ButtonEvents e;
  e.left = btnF.clicked;  // physical F
  e.right = btnB.clicked; // physical B
  e.down = btnL.clicked;  // physical L
  e.up = btnR.clicked;    // physical R
  e.click = btnPress.clicked;
  e.longClick = btnPress.longPressed;
  return e;
}
