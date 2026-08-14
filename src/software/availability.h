// Available / Busy: a manual status light, readable at a glance from across
// the room. No timing, no phases - click toggles.
//
// Header-only, like every module here: `static` state + `inline` functions, so
// include it from exactly one .cpp (main.cpp). See hardware/display.h.
#pragma once

#include "../config.h"
#include "../hardware/buttons.h"
#include "../hardware/display.h"
#include "../hardware/leds.h"

enum Availability
{
  AVAIL_AVAILABLE,
  AVAIL_BUSY
};

static Availability availability = AVAIL_AVAILABLE;

inline void availabilityInput(const ButtonEvents &b)
{
  if (b.click)
    availability = (availability == AVAIL_AVAILABLE) ? AVAIL_BUSY : AVAIL_AVAILABLE;
}

inline void availabilityLeds()
{
  ledsSetAll(availability == AVAIL_AVAILABLE ? COLOR_AVAILABLE : COLOR_BUSY);
}

inline void availabilityDraw()
{
  displayText(0, 10, "Status");
  displayBoldCentered(36, availability == AVAIL_AVAILABLE ? "AVAILABLE" : "BUSY");
  displayText(0, 50, "click=toggle");
  displayText(0, 62, "hold=menu");
}
