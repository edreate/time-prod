// Focus timer: a settable H:M:S countdown with a draining LED bar.
//
// Header-only, like every module here: `static` state + `inline` functions, so
// include it from exactly one .cpp (main.cpp). See hardware/display.h.
#pragma once

#include <Arduino.h>

#include "../config.h"
#include "../hardware/buttons.h"
#include "../hardware/display.h"
#include "../hardware/leds.h"

enum TimerState
{
  STATE_SET,
  STATE_RUNNING,
  STATE_PAUSED,
  STATE_DONE
};

enum Field
{
  FIELD_HOUR,
  FIELD_MINUTE,
  FIELD_SECOND,
  FIELD_COUNT
};

static TimerState timerState = STATE_SET;
static Field selectedField = FIELD_MINUTE;

static uint8_t setHours = DEFAULT_HOURS;
static uint8_t setMinutes = DEFAULT_MINUTES;
static uint8_t setSeconds = DEFAULT_SECONDS;

static long totalMs = 0;
static long remainingMs = 0;
static unsigned long lastTickMs = 0;
static unsigned long doneSinceMs = 0;

inline void timerStart()
{
  totalMs = ((long)setHours * 3600L + (long)setMinutes * 60L + setSeconds) * 1000L;
  if (totalMs <= 0)
    return; // nothing set - ignore start
  remainingMs = totalMs;
  lastTickMs = millis();
  timerState = STATE_RUNNING;
}

// Abandon any running session and go back to editing.
inline void timerReset()
{
  timerState = STATE_SET;
}

inline void timerCycleField(int dir)
{
  selectedField = (Field)(((int)selectedField + dir + FIELD_COUNT) % FIELD_COUNT);
}

inline void timerAdjustField(int dir)
{
  switch (selectedField)
  {
  case FIELD_HOUR:
    setHours = (uint8_t)((setHours + dir + 24) % 24);
    break;
  case FIELD_MINUTE:
    setMinutes = (uint8_t)((setMinutes + dir + 60) % 60);
    break;
  case FIELD_SECOND:
    setSeconds = (uint8_t)((setSeconds + dir + 60) % 60);
    break;
  case FIELD_COUNT:
    break;
  }
}

inline void timerTick()
{
  if (timerState != STATE_RUNNING)
    return;
  unsigned long now = millis();
  remainingMs -= (long)(now - lastTickMs);
  lastTickMs = now;
  if (remainingMs <= 0)
  {
    remainingMs = 0;
    timerState = STATE_DONE;
    doneSinceMs = now;
  }
}

inline void timerInput(const ButtonEvents &b)
{
  switch (timerState)
  {
  case STATE_SET:
    if (b.left)
      timerCycleField(-1);
    if (b.right)
      timerCycleField(+1);
    if (b.up)
      timerAdjustField(+1);
    if (b.down)
      timerAdjustField(-1);
    if (b.click)
      timerStart();
    break;

  case STATE_RUNNING:
    if (b.click)
      timerState = STATE_PAUSED;
    break;

  case STATE_PAUSED:
    if (b.click)
    { // resume
      lastTickMs = millis();
      timerState = STATE_RUNNING;
    }
    if (b.down)
      timerState = STATE_SET; // cancel back to editing
    break;

  case STATE_DONE:
    if (b.click || b.up || b.down || b.left || b.right)
      timerState = STATE_SET;
    break;
  }
}

inline void timerLeds()
{
  switch (timerState)
  {
  case STATE_SET:
    ledsSetAll(COLOR_OFF);
    break;
  case STATE_RUNNING:
  case STATE_PAUSED:
    ledsSetProgress(remainingMs, totalMs);
    break;
  case STATE_DONE:
  {
    bool on = ((millis() - doneSinceMs) / DONE_BLINK_MS) % 2 == 0;
    ledsSetAll(on ? COLOR_DONE : COLOR_OFF);
    break;
  }
  }
}

inline void timerDraw()
{
  char big[16];
  const char *title = "";
  const char *hint = "";
  long s;

  switch (timerState)
  {
  case STATE_SET:
  {
    title = "Set Timer";
    char h[6], m[6], sec[6];
    snprintf(h, sizeof(h), selectedField == FIELD_HOUR ? "[%02u]" : "%02u", setHours);
    snprintf(m, sizeof(m), selectedField == FIELD_MINUTE ? "[%02u]" : "%02u", setMinutes);
    snprintf(sec, sizeof(sec), selectedField == FIELD_SECOND ? "[%02u]" : "%02u", setSeconds);
    snprintf(big, sizeof(big), "%s:%s:%s", h, m, sec);
    hint = "</> field ^v +/- clk=go";
    break;
  }
  case STATE_RUNNING:
    title = "Running";
    s = remainingMs / 1000;
    snprintf(big, sizeof(big), "%02ld:%02ld:%02ld", s / 3600, (s / 60) % 60, s % 60);
    hint = "click=pause";
    break;
  case STATE_PAUSED:
    title = "Paused";
    s = remainingMs / 1000;
    snprintf(big, sizeof(big), "%02ld:%02ld:%02ld", s / 3600, (s / 60) % 60, s % 60);
    hint = "click=resume v=cancel";
    break;
  case STATE_DONE:
    title = "Done!";
    snprintf(big, sizeof(big), "00:00:00");
    hint = "press any button";
    break;
  }

  displayText(0, 10, title);
  displayBigCentered(34, big);
  displayText(0, 48, hint);

  if (timerState == STATE_RUNNING || timerState == STATE_PAUSED)
  {
    int lit = 0;
    if (totalMs > 0 && remainingMs > 0)
    {
      lit = (int)((remainingMs * (long)PROGRESS_CHARS + totalMs - 1) / totalMs);
      if (lit > PROGRESS_CHARS)
        lit = PROGRESS_CHARS;
    }
    char bar[PROGRESS_CHARS + 1];
    for (int i = 0; i < PROGRESS_CHARS; i++)
      bar[i] = (i < lit) ? '#' : '-';
    bar[PROGRESS_CHARS] = '\0';
    displayText(0, 62, bar);
  }
  else
  {
    displayText(0, 62, "hold=menu");
  }
}
