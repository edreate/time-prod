// FocusDock - the shell: menu, program dispatch, and the main loop.
//
// Everything else lives in a module:
//   src/hardware/   one file per peripheral (display, LEDs, IMU, buttons)
//   src/software/   one file per program (timer, pomodoro, availability)
//   src/config.h    every tunable
//
// This file owns no hardware and no program state. It routes button events to
// whichever program is open, ticks it, and asks it to render.
//
// Navigation:
//   Menu          Up/Down move the cursor, click opens the program.
//   Any program   A LONG press (hold >LONG_PRESS_MS) always returns to the
//                 menu - one consistent "go home" gesture so no direction
//                 button has to be sacrificed for it.
//
// Wiring: docs/HARDWARE.md.

#include <Arduino.h>
#include <Wire.h>

#include "config.h"
#include "hardware/buttons.h"
#include "hardware/display.h"
#include "hardware/imu.h"
#include "hardware/leds.h"
#include "software/availability.h"
#include "software/pomodoro.h"
#include "software/timer.h"

enum Program
{
  PROGRAM_MENU,
  PROGRAM_TIMER,
  PROGRAM_POMODORO,
  PROGRAM_AVAILABILITY
};
Program currentProgram = PROGRAM_MENU;

// Menu rows are laid out for up to 3 entries - a 4th would collide with the
// footer hint, and would need a scrolling window like the pre-simplify build.
const char *MENU_ITEMS[] = {"Timer", "Pomodoro", "Available / Busy"};
const int MENU_COUNT = 3;
int menuIndex = 0;

// Adding a program: write src/software/<name>.h with the same
// input/tick/leds/draw shape, then add a row here and a case to each switch.
const Program MENU_PROGRAMS[] = {PROGRAM_TIMER, PROGRAM_POMODORO,
                                 PROGRAM_AVAILABILITY};

// ============================ input =================================

void handleInput(const ButtonEvents &b)
{
  // Long press is the one gesture the shell owns: leave the program, and let
  // it drop whatever it was doing.
  if (b.longClick)
  {
    if (currentProgram == PROGRAM_TIMER)
      timerReset();
    if (currentProgram == PROGRAM_POMODORO)
      pomodoroReset();
    currentProgram = PROGRAM_MENU;
    return;
  }

  switch (currentProgram)
  {
  case PROGRAM_MENU:
    if (b.up)
      menuIndex = (menuIndex - 1 + MENU_COUNT) % MENU_COUNT;
    if (b.down)
      menuIndex = (menuIndex + 1) % MENU_COUNT;
    if (b.click)
      currentProgram = MENU_PROGRAMS[menuIndex];
    break;

  case PROGRAM_TIMER:
    timerInput(b);
    break;
  case PROGRAM_POMODORO:
    pomodoroInput(b);
    break;
  case PROGRAM_AVAILABILITY:
    availabilityInput(b);
    break;
  }
}

// ======================== per-loop updates ==========================

void tickProgram()
{
  switch (currentProgram)
  {
  case PROGRAM_TIMER:
    timerTick();
    break;
  case PROGRAM_POMODORO:
    pomodoroTick();
    break;
  default:
    break; // menu and availability have nothing to advance
  }
}

void updateLeds()
{
  switch (currentProgram)
  {
  case PROGRAM_MENU:
    ledsSetAll(COLOR_OFF);
    break;
  case PROGRAM_TIMER:
    timerLeds();
    break;
  case PROGRAM_POMODORO:
    pomodoroLeds();
    break;
  case PROGRAM_AVAILABILITY:
    availabilityLeds();
    break;
  }
}

// The shell only cares that the orientation changed, not how it was measured.
void applyOrientation()
{
  static Orientation shown = ORIENT_NORMAL;
  Orientation now = imuOrientation();
  if (now != shown)
  {
    shown = now;
    displaySetFlipped(now == ORIENT_FLIPPED);
  }
}

// ============================ rendering =============================

void drawMenu()
{
  displayText(0, 10, "Select program");
  for (int i = 0; i < MENU_COUNT; i++)
  {
    char line[24];
    snprintf(line, sizeof(line), "%s%s", i == menuIndex ? "> " : "  ", MENU_ITEMS[i]);
    displayText(4, 24 + i * 13, line);
  }
  displayText(0, 62, "up/dn=move click=open");
}

void drawScreen()
{
  displayBeginFrame();
  switch (currentProgram)
  {
  case PROGRAM_MENU:
    drawMenu();
    break;
  case PROGRAM_TIMER:
    timerDraw();
    break;
  case PROGRAM_POMODORO:
    pomodoroDraw();
    break;
  case PROGRAM_AVAILABILITY:
    availabilityDraw();
    break;
  }
  displayEndFrame();
}

// ============================ setup/loop ============================

void setup()
{
  Serial.begin(115200);
  delay(500);
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL); // shared bus: display + IMU

  displayBegin();
  ledsBegin();
  buttonsBegin();

  // A missing IMU is not fatal - the firmware just runs without auto-flip.
  Serial.println(imuBegin() ? "IMU: init OK"
                            : "IMU: init failed - auto-flip disabled this session");

  Serial.println("FocusDock ready");
}

void loop()
{
  handleInput(buttonsPoll());
  tickProgram();
  applyOrientation();
  updateLeds();

  static unsigned long lastDrawMs = 0;
  if (millis() - lastDrawMs >= DISPLAY_REDRAW_MS)
  {
    lastDrawMs = millis();
    drawScreen();
  }

  delay(LOOP_DELAY_MS);
}
