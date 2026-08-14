// Pomodoro: work / short break / work / ... / long break, auto-cycling with a
// short announce window between phases. LEDs show a solid phase color.
//
// Header-only, like every module here: `static` state + `inline` functions, so
// include it from exactly one .cpp (main.cpp). See hardware/display.h.
#pragma once

#include <Arduino.h>

#include "../config.h"
#include "../hardware/buttons.h"
#include "../hardware/display.h"
#include "../hardware/leds.h"

enum PomoRunState
{
  POMO_IDLE,
  POMO_RUNNING,
  POMO_PAUSED,
  POMO_PHASE_DONE
};

enum PomoPhase
{
  PHASE_WORK,
  PHASE_SHORT_BREAK,
  PHASE_LONG_BREAK
};

static PomoRunState pomoRunState = POMO_IDLE;
static PomoPhase pomoPhase = PHASE_WORK;
static uint8_t pomoSessionCount = 0; // completed work sessions since the last long break
static long pomoTotalMs = 0;
static long pomoRemainingMs = 0;
static unsigned long pomoLastTickMs = 0;
static unsigned long pomoPhaseDoneSinceMs = 0;

inline const char *pomoPhaseLabel(PomoPhase phase)
{
  switch (phase)
  {
  case PHASE_WORK:
    return "Work";
  case PHASE_SHORT_BREAK:
    return "Short Break";
  case PHASE_LONG_BREAK:
    return "Long Break";
  }
  return "";
}

inline uint32_t pomoPhaseColor(PomoPhase phase)
{
  switch (phase)
  {
  case PHASE_WORK:
    return COLOR_POMO_WORK;
  case PHASE_SHORT_BREAK:
    return COLOR_POMO_SHORT_BREAK;
  case PHASE_LONG_BREAK:
    return COLOR_POMO_LONG_BREAK;
  }
  return COLOR_POMO_SHORT_BREAK;
}

inline uint16_t pomoPhaseMinutes(PomoPhase phase)
{
  switch (phase)
  {
  case PHASE_WORK:
    return DEFAULT_POMO_WORK_MIN;
  case PHASE_SHORT_BREAK:
    return DEFAULT_POMO_SHORT_MIN;
  case PHASE_LONG_BREAK:
    return DEFAULT_POMO_LONG_MIN;
  }
  return DEFAULT_POMO_WORK_MIN;
}

inline void pomoStart(PomoPhase phase)
{
  pomoPhase = phase;
  pomoTotalMs = (long)pomoPhaseMinutes(phase) * 60L * 1000L;
  pomoRemainingMs = pomoTotalMs;
  pomoLastTickMs = millis();
  pomoRunState = POMO_RUNNING;
}

// Abandon the whole cycle, not just the current phase.
inline void pomodoroReset()
{
  pomoRunState = POMO_IDLE;
  pomoSessionCount = 0;
}

// Work -> short break -> work -> ... -> long break every DEFAULT_POMO_SESSIONS.
// Lands in POMO_PHASE_DONE so the next phase is announced before it starts.
inline void pomoAdvancePhase()
{
  PomoPhase next;
  if (pomoPhase == PHASE_WORK)
  {
    pomoSessionCount++;
    if (pomoSessionCount >= DEFAULT_POMO_SESSIONS)
    {
      next = PHASE_LONG_BREAK;
      pomoSessionCount = 0;
    }
    else
    {
      next = PHASE_SHORT_BREAK;
    }
  }
  else
  {
    next = PHASE_WORK;
  }
  pomoPhase = next;
  pomoRunState = POMO_PHASE_DONE;
  pomoPhaseDoneSinceMs = millis();
}

inline void pomodoroTick()
{
  if (pomoRunState == POMO_RUNNING)
  {
    unsigned long now = millis();
    pomoRemainingMs -= (long)(now - pomoLastTickMs);
    pomoLastTickMs = now;
    if (pomoRemainingMs <= 0)
    {
      pomoRemainingMs = 0;
      pomoAdvancePhase();
    }
  }
  else if (pomoRunState == POMO_PHASE_DONE)
  {
    if ((millis() - pomoPhaseDoneSinceMs) > POMO_PHASE_TRANSITION_MS)
    {
      pomoStart(pomoPhase); // auto-continue into the phase we already advanced to
    }
  }
}

inline void pomodoroInput(const ButtonEvents &b)
{
  switch (pomoRunState)
  {
  case POMO_IDLE:
    if (b.click)
      pomoStart(PHASE_WORK);
    break;

  case POMO_RUNNING:
    if (b.click)
      pomoRunState = POMO_PAUSED;
    break;

  case POMO_PAUSED:
    if (b.click)
    { // resume
      pomoLastTickMs = millis();
      pomoRunState = POMO_RUNNING;
    }
    if (b.down)
      pomodoroReset(); // cancel the whole cycle
    break;

  case POMO_PHASE_DONE:
    // any button skips the wait and starts the next phase now
    if (b.click || b.up || b.down || b.left || b.right)
      pomoStart(pomoPhase);
    break;
  }
}

// Solid phase color, not a countdown bar - the point is to be readable at a
// glance as "he's in a work block" vs "he's on a break".
inline void pomodoroLeds()
{
  switch (pomoRunState)
  {
  case POMO_IDLE:
    ledsSetAll(COLOR_AVAILABLE);
    break;
  case POMO_RUNNING:
    ledsSetAll(pomoPhaseColor(pomoPhase));
    break;
  case POMO_PAUSED:
    ledsSetAll(COLOR_BUSY);
    break;
  case POMO_PHASE_DONE:
  {
    bool on = ((millis() - pomoPhaseDoneSinceMs) / DONE_BLINK_MS) % 2 == 0;
    ledsSetAll(on ? pomoPhaseColor(pomoPhase) : COLOR_OFF);
    break;
  }
  }
}

inline void pomodoroDraw()
{
  char big[20];
  char sessionLine[20];
  const char *hint = "";
  long s;

  switch (pomoRunState)
  {
  case POMO_IDLE:
    snprintf(big, sizeof(big), "%02u:00", DEFAULT_POMO_WORK_MIN);
    hint = "click=start";
    break;
  case POMO_RUNNING:
    s = pomoRemainingMs / 1000;
    snprintf(big, sizeof(big), "%02ld:%02ld", s / 60, s % 60);
    hint = "click=pause";
    break;
  case POMO_PAUSED:
    s = pomoRemainingMs / 1000;
    snprintf(big, sizeof(big), "%02ld:%02ld", s / 60, s % 60);
    hint = "click=resume v=stop";
    break;
  case POMO_PHASE_DONE:
    snprintf(big, sizeof(big), "Next: %s", pomoPhaseLabel(pomoPhase));
    hint = "press to skip";
    break;
  }

  displayText(0, 10, pomoPhaseLabel(pomoPhase));
  if (pomoRunState == POMO_PHASE_DONE)
    displaySmallCentered(34, big); // the phase name won't fit in the big font
  else
    displayBigCentered(34, big);

  uint8_t sessionDisplay = pomoSessionCount + 1;
  if (sessionDisplay > DEFAULT_POMO_SESSIONS)
    sessionDisplay = DEFAULT_POMO_SESSIONS;
  snprintf(sessionLine, sizeof(sessionLine), "Session %u/%u", sessionDisplay,
           DEFAULT_POMO_SESSIONS);
  displayText(0, 48, sessionLine);
  displayText(0, 62, hint);
}
