// FocusDock - main firmware: the app state machine and the screens.
//
// All hardware lives behind the small modules in src/hw/ - this file asks for
// button events and an orientation, and tells the display and LEDs what to
// show. It never touches a driver library.
//
// Navigation:
//   Menu          Up/Down move the cursor, click opens the program.
//   Any program   A LONG press (hold >LONG_PRESS_MS) always returns to the
//                 menu - one consistent "go home" gesture so no direction
//                 button has to be sacrificed for it.
//   Timer         Left/Right pick the H/M/S field, Up/Down change it,
//                 short click starts/pauses/resumes. LEDs: 10-LED countdown
//                 bar, emptying out as time runs out, blinking green when
//                 done.
//   Pomodoro      Work / short break / work / ... / long break, auto-cycling
//                 with a POMO_PHASE_TRANSITION_MS blink between phases (any
//                 button skips the wait). LEDs: solid phase color.
//   Availability  Short click toggles Available/Busy. LEDs: solid green
//                 (available) or solid red (busy) - the whole point of the
//                 program is to be readable at a glance.
//
// Tunables: src/config.h.   Wiring: docs/HARDWARE.md.

#include <Arduino.h>
#include <Wire.h>

#include "config.h"
#include "hw/buttons.h"
#include "hw/display.h"
#include "hw/imu.h"
#include "hw/leds.h"

// ---- programs ----
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

// ---- availability program ----
enum Availability
{
  AVAIL_AVAILABLE,
  AVAIL_BUSY
};
Availability availability = AVAIL_AVAILABLE;

// ---- timer program ----
enum TimerState
{
  STATE_SET,
  STATE_RUNNING,
  STATE_PAUSED,
  STATE_DONE
};
TimerState timerState = STATE_SET;

enum Field
{
  FIELD_HOUR,
  FIELD_MINUTE,
  FIELD_SECOND,
  FIELD_COUNT
};
Field selectedField = FIELD_MINUTE;

uint8_t setHours = DEFAULT_HOURS;
uint8_t setMinutes = DEFAULT_MINUTES;
uint8_t setSeconds = DEFAULT_SECONDS;

long totalMs = 0;
long remainingMs = 0;
unsigned long lastTickMs = 0;
unsigned long doneSinceMs = 0;

// ---- pomodoro program ----
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
PomoRunState pomoRunState = POMO_IDLE;
PomoPhase pomoPhase = PHASE_WORK;
uint8_t pomoSessionCount = 0; // completed work sessions since the last long break
long pomoTotalMs = 0;
long pomoRemainingMs = 0;
unsigned long pomoLastTickMs = 0;
unsigned long pomoPhaseDoneSinceMs = 0;

// =========================== timer ==================================

void startTimer()
{
  totalMs = ((long)setHours * 3600L + (long)setMinutes * 60L + setSeconds) * 1000L;
  if (totalMs <= 0)
    return; // nothing set - ignore start
  remainingMs = totalMs;
  lastTickMs = millis();
  timerState = STATE_RUNNING;
}

void tickTimer()
{
  if (currentProgram != PROGRAM_TIMER || timerState != STATE_RUNNING)
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

void cycleField(int dir)
{
  selectedField = (Field)(((int)selectedField + dir + FIELD_COUNT) % FIELD_COUNT);
}

void adjustField(int dir)
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

// ========================== pomodoro ================================

const char *phaseLabel(PomoPhase phase)
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

uint32_t phaseColor(PomoPhase phase)
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

uint16_t phaseMinutes(PomoPhase phase)
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

void pomoStart(PomoPhase phase)
{
  pomoPhase = phase;
  pomoTotalMs = (long)phaseMinutes(phase) * 60L * 1000L;
  pomoRemainingMs = pomoTotalMs;
  pomoLastTickMs = millis();
  pomoRunState = POMO_RUNNING;
}

// Work -> short break -> work -> ... -> long break every DEFAULT_POMO_SESSIONS.
// Lands in POMO_PHASE_DONE so the next phase is announced before it starts.
void pomoAdvancePhase()
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

void tickPomodoro()
{
  if (currentProgram != PROGRAM_POMODORO)
    return;

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

// ============================ input =================================

// State transitions only - the polling and the pin mapping live in hw/buttons.
void handleInput(const ButtonEvents &b)
{
  if (b.longClick)
  {
    if (currentProgram == PROGRAM_TIMER)
      timerState = STATE_SET; // abandon any running session
    if (currentProgram == PROGRAM_POMODORO)
    {
      pomoRunState = POMO_IDLE; // abandon the whole cycle, not just the phase
      pomoSessionCount = 0;
    }
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
    {
      switch (menuIndex)
      {
      case 0:
        currentProgram = PROGRAM_TIMER;
        break;
      case 1:
        currentProgram = PROGRAM_POMODORO;
        break;
      case 2:
        currentProgram = PROGRAM_AVAILABILITY;
        break;
      }
    }
    break;

  case PROGRAM_POMODORO:
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
      { // cancel the whole cycle
        pomoRunState = POMO_IDLE;
        pomoSessionCount = 0;
      }
      break;

    case POMO_PHASE_DONE:
      // any button skips the wait and starts the next phase now
      if (b.click || b.up || b.down || b.left || b.right)
        pomoStart(pomoPhase);
      break;
    }
    break;

  case PROGRAM_AVAILABILITY:
    if (b.click)
    {
      availability = (availability == AVAIL_AVAILABLE) ? AVAIL_BUSY : AVAIL_AVAILABLE;
    }
    break;

  case PROGRAM_TIMER:
    switch (timerState)
    {
    case STATE_SET:
      if (b.left)
        cycleField(-1);
      if (b.right)
        cycleField(+1);
      if (b.up)
        adjustField(+1);
      if (b.down)
        adjustField(-1);
      if (b.click)
        startTimer();
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
      {
        timerState = STATE_SET;
      }
      break;
    }
    break;
  }
}

// ============================= LEDs =================================

void updateLeds()
{
  switch (currentProgram)
  {
  case PROGRAM_MENU:
    ledsSetAll(COLOR_OFF);
    break;

  case PROGRAM_AVAILABILITY:
    ledsSetAll(availability == AVAIL_AVAILABLE ? COLOR_AVAILABLE : COLOR_BUSY);
    break;

  // Solid phase color, not a countdown bar - the point is to be readable at a
  // glance as "he's in a work block" vs "he's on a break".
  case PROGRAM_POMODORO:
    switch (pomoRunState)
    {
    case POMO_IDLE:
      ledsSetAll(COLOR_AVAILABLE);
      break;
    case POMO_RUNNING:
      ledsSetAll(phaseColor(pomoPhase));
      break;
    case POMO_PAUSED:
      ledsSetAll(COLOR_BUSY);
      break;
    case POMO_PHASE_DONE:
    {
      bool on = ((millis() - pomoPhaseDoneSinceMs) / DONE_BLINK_MS) % 2 == 0;
      ledsSetAll(on ? phaseColor(pomoPhase) : COLOR_OFF);
      break;
    }
    }
    break;

  case PROGRAM_TIMER:
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
    break;
  }
}

// ======================== orientation ===============================

// Main only cares that the orientation changed, not how it was measured.
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

// =========================== screens ================================

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

void drawTimer()
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

void drawPomodoro()
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
    snprintf(big, sizeof(big), "Next: %s", phaseLabel(pomoPhase));
    hint = "press to skip";
    break;
  }

  displayText(0, 10, phaseLabel(pomoPhase));
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

void drawAvailability()
{
  displayText(0, 10, "Status");
  displayBoldCentered(36, availability == AVAIL_AVAILABLE ? "AVAILABLE" : "BUSY");
  displayText(0, 50, "click=toggle");
  displayText(0, 62, "hold=menu");
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
    drawTimer();
    break;
  case PROGRAM_POMODORO:
    drawPomodoro();
    break;
  case PROGRAM_AVAILABILITY:
    drawAvailability();
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
  tickTimer();
  tickPomodoro();
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
