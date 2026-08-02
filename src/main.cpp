// Focus & Availability Device - main firmware
//
// What it does:
//   - Menu: click cycles between Focus Timer, Pomodoro, and Available/Busy.
//   - Focus timer: pick an H:M:S duration, center click starts/pauses.
//   - Pomodoro: work / short break / work / ... / long break, auto-cycling,
//     durations and session count configurable from the Wi-Fi settings page.
//   - Status light: LEDs are green when you're free, red while focusing,
//     amber when paused/busy. Left/right forces a color manually (meeting
//     indicator) from anywhere except while editing the timer's H:M:S.
//   - Auto-flip: the screen rotates 180 degrees when the device is mounted
//     upside down (top vs bottom edge of the monitor), using the IMU.
//   - Wi-Fi: the device broadcasts its own hotspot at all times, so you can
//     always reach the settings page at http://192.168.4.1 - no internet or
//     home network required. (Home Wi-Fi client mode / NTP clock is on the
//     roadmap, not wired up yet.)
//   - Long-press the center click, from any screen, to return to the menu.
//
// Wiring: see docs/HARDWARE.md. New to electronics? See docs/ELECTRONICS.md.
//
// Everything you might want to tweak lives in the CONFIG block below.

#include <Wire.h>
#include <U8g2lib.h>
#include <DFRobot_BMI160.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

// ============================== CONFIG ==============================
// ---- Pins (change these if you wire things differently) ----
#define PIN_I2C_SDA      8    // OLED + IMU data line (shared I2C bus)
#define PIN_I2C_SCL      9    // OLED + IMU clock line
#define PIN_LED_DATA     16   // WS2812B strip data-in (through 330-470 ohm resistor)
#define PIN_BTN_UP       7    // 5-way key "forward"
#define PIN_BTN_DOWN     6    // 5-way key "backward"
#define PIN_BTN_LEFT     4
#define PIN_BTN_RIGHT    5
#define PIN_BTN_PRESS    15   // 5-way key center click
#define BMI160_I2C_ADDR  0x68 // IMU address (SA0 pin tied to GND)

// ---- LED strip ----
#define NUM_LEDS                8      // how many LEDs are wired up
#define DEFAULT_LED_BRIGHTNESS  60     // 0-255; capped low on purpose (power budget)
#define COLOR_FOCUS              0xFF0000  // red    = focusing, do not disturb
#define COLOR_FREE                0x00FF00  // green  = available
#define COLOR_BUSY                0xFF0000  // amber  = busy / in a meeting / paused
#define COLOR_POMO_LONG_BREAK     0x0080FF  // blue   = pomodoro long break
#define COLOR_OFF                 0x000000

// ---- Focus timer ----
#define DEFAULT_FOCUS_MINUTES   25     // classic pomodoro length
#define TIMER_MIN_MINUTES       5
#define TIMER_MAX_MINUTES       120
#define DONE_BLINK_MS            500    // LED blink speed when time is up
#define DONE_AUTO_DISMISS_MS     60000  // "time's up" screen clears itself after this

// ---- Pomodoro ----
#define DEFAULT_POMO_WORK_MIN    25
#define DEFAULT_POMO_SHORT_MIN   5
#define DEFAULT_POMO_LONG_MIN    15
#define DEFAULT_POMO_SESSIONS    4
#define POMO_MIN_MINUTES         1
#define POMO_MAX_MINUTES         180
#define POMO_SESSIONS_MIN        1
#define POMO_SESSIONS_MAX        10
#define POMO_PHASE_TRANSITION_MS 3000   // blink window between phases before auto-continuing

// ---- Buttons / menu ----
#define BTN_DEBOUNCE_MS          30     // ignore mechanical chatter shorter than this
#define LONG_PRESS_MS            800    // hold center click this long, anywhere, to return to the menu

// ---- Orientation (auto-flip) ----
#define FLIP_THRESHOLD           0.5f   // g; accel-x past +/- this commits a state
#define FLIP_DEBOUNCE_MS         350    // must hold past threshold this long
#define EMA_ALPHA                0.2f   // accel smoothing, 0..1 (higher = jumpier)

// ---- Wi-Fi settings hotspot (always on) ----
#define WIFI_AP_SSID              "FocusDock"
#define WIFI_AP_PASSWORD          "focus1234"  // min 8 chars, or "" for an open network
#define AP_CHECK_MS               5000   // how often to confirm the hotspot is still up

// ---- Menu ----
#define MENU_VISIBLE_ROWS         3      // how many menu items fit on screen at once

// ---- Loop pacing ----
#define LOOP_DELAY_MS            10
#define DISPLAY_REDRAW_MS        100    // don't redraw the OLED faster than this
// ============================ END CONFIG ============================

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
DFRobot_BMI160 bmi160;
Adafruit_NeoPixel strip(NUM_LEDS, PIN_LED_DATA, NEO_GRB + NEO_KHZ800);
WebServer server(80);
Preferences prefs;

// ---- persisted settings (editable from the Wi-Fi page) ----
struct Settings {
  uint8_t brightness = DEFAULT_LED_BRIGHTNESS;
  uint16_t focusMinutes = DEFAULT_FOCUS_MINUTES;
  bool flipEnabled = true;

  uint16_t pomoWorkMin = DEFAULT_POMO_WORK_MIN;
  uint16_t pomoShortBreakMin = DEFAULT_POMO_SHORT_MIN;
  uint16_t pomoLongBreakMin = DEFAULT_POMO_LONG_MIN;
  uint8_t pomoSessionsBeforeLong = DEFAULT_POMO_SESSIONS;
};
Settings settings;

// ---- menu / programs ----
enum Program { PROGRAM_MENU, PROGRAM_TIMER, PROGRAM_POMODORO, PROGRAM_AVAILABILITY, PROGRAM_INFO };
Program currentProgram = PROGRAM_MENU;
const char* MENU_ITEMS[] = { "Focus Timer", "Pomodoro", "Available / Busy", "Info" };
const int MENU_COUNT = 4;
int menuIndex = 0;
int menuScroll = 0;  // index of the first menu item currently shown on screen

// ---- focus timer state machine ----
enum TimerState { STATE_SET, STATE_RUNNING, STATE_PAUSED, STATE_DONE };
TimerState timerState = STATE_SET;

enum Field { FIELD_HOUR, FIELD_MINUTE, FIELD_SECOND, FIELD_COUNT };
Field selectedField = FIELD_MINUTE;

uint8_t setHours = 0;
uint8_t setMinutes = DEFAULT_FOCUS_MINUTES;
uint8_t setSeconds = 0;

long totalMs = 0;
long remainingMs = 0;
unsigned long lastTickMs = 0;
unsigned long doneSinceMs = 0;

// ---- pomodoro state machine ----
enum PomoRunState { POMO_IDLE, POMO_RUNNING, POMO_PAUSED, POMO_PHASE_DONE };
enum PomoPhase { PHASE_WORK, PHASE_SHORT_BREAK, PHASE_LONG_BREAK };
PomoRunState pomoRunState = POMO_IDLE;
PomoPhase pomoPhase = PHASE_WORK;
uint8_t pomoSessionCount = 0;  // completed work sessions since the last long break
long pomoTotalMs = 0;
long pomoRemainingMs = 0;
unsigned long pomoLastTickMs = 0;
unsigned long pomoPhaseDoneSinceMs = 0;

// ---- manual status override: left/right cycles through these (almost anywhere) ----
enum StatusOverride { OVERRIDE_AUTO, OVERRIDE_FREE, OVERRIDE_BUSY, OVERRIDE_FOCUS };
const uint8_t OVERRIDE_COUNT = 4;
StatusOverride statusOverride = OVERRIDE_AUTO;

// ---- orientation ----
enum Orientation { ORIENT_NORMAL, ORIENT_FLIPPED };
Orientation currentOrientation = ORIENT_NORMAL;
Orientation candidateOrientation = ORIENT_NORMAL;
unsigned long candidateSinceMs = 0;
float axFiltered = 0.0f;
bool imuOk = false;

// ---- hotspot watchdog ----
unsigned long lastApCheckMs = 0;

// ---- debounced button, with long-press detection for the center click ----
struct Button {
  explicit Button(uint8_t p) : pin(p) {}

  uint8_t pin;
  bool stable = true;        // true = released (pin idles high via pull-up)
  bool lastReading = true;
  unsigned long lastChangeMs = 0;
  unsigned long pressedAtMs = 0;
  bool clicked = false;      // true for exactly one loop pass after a short press
  bool longPressed = false;  // true for exactly one loop pass once held > LONG_PRESS_MS
  bool longFired = false;    // internal: suppresses the trailing click after a long press

  void begin() { pinMode(pin, INPUT_PULLUP); }

  void update() {
    clicked = false;
    longPressed = false;
    bool reading = digitalRead(pin);
    if (reading != lastReading) {
      lastChangeMs = millis();
      lastReading = reading;
    }
    if ((millis() - lastChangeMs) > BTN_DEBOUNCE_MS && reading != stable) {
      stable = reading;
      if (stable == LOW) {
        pressedAtMs = millis();
        longFired = false;
      } else if (!longFired) {
        clicked = true;
      }
    }
    if (stable == LOW && !longFired && (millis() - pressedAtMs) > LONG_PRESS_MS) {
      longFired = true;
      longPressed = true;
    }
  }
};

Button btnUp{PIN_BTN_UP}, btnDown{PIN_BTN_DOWN}, btnLeft{PIN_BTN_LEFT},
       btnRight{PIN_BTN_RIGHT}, btnPress{PIN_BTN_PRESS};

// =========================== settings ===============================

void loadSettings() {
  prefs.begin("focusdock", /*readOnly=*/false);
  settings.brightness = prefs.getUChar("bright", DEFAULT_LED_BRIGHTNESS);
  settings.focusMinutes = prefs.getUShort("minutes", DEFAULT_FOCUS_MINUTES);
  settings.flipEnabled = prefs.getBool("flip", true);

  settings.pomoWorkMin = prefs.getUShort("pomoWork", DEFAULT_POMO_WORK_MIN);
  settings.pomoShortBreakMin = prefs.getUShort("pomoShort", DEFAULT_POMO_SHORT_MIN);
  settings.pomoLongBreakMin = prefs.getUShort("pomoLong", DEFAULT_POMO_LONG_MIN);
  settings.pomoSessionsBeforeLong = prefs.getUChar("pomoSessN", DEFAULT_POMO_SESSIONS);

  setMinutes = settings.focusMinutes;
}

void saveSettings() {
  prefs.putUChar("bright", settings.brightness);
  prefs.putUShort("minutes", settings.focusMinutes);
  prefs.putBool("flip", settings.flipEnabled);
  prefs.putUShort("pomoWork", settings.pomoWorkMin);
  prefs.putUShort("pomoShort", settings.pomoShortBreakMin);
  prefs.putUShort("pomoLong", settings.pomoLongBreakMin);
  prefs.putUChar("pomoSessN", settings.pomoSessionsBeforeLong);
}

// ============================= LEDs =================================

void setAllLeds(uint32_t color) {
  for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, color);
  strip.show();
}

uint32_t phaseColor(PomoPhase phase) {
  switch (phase) {
    case PHASE_WORK:        return COLOR_FOCUS;
    case PHASE_SHORT_BREAK: return COLOR_FREE;
    case PHASE_LONG_BREAK:  return COLOR_POMO_LONG_BREAK;
  }
  return COLOR_FREE;
}

uint32_t statusColor() {
  // A manual override always wins, regardless of what program is active.
  switch (statusOverride) {
    case OVERRIDE_FREE:  return COLOR_FREE;
    case OVERRIDE_BUSY:  return COLOR_BUSY;
    case OVERRIDE_FOCUS: return COLOR_FOCUS;
    case OVERRIDE_AUTO:  break;
  }

  if (currentProgram == PROGRAM_TIMER) {
    switch (timerState) {
      case STATE_RUNNING: return COLOR_FOCUS;
      case STATE_PAUSED:  return COLOR_BUSY;
      default:             return COLOR_FREE;  // STATE_SET; STATE_DONE blinks separately
    }
  }

  if (currentProgram == PROGRAM_POMODORO) {
    switch (pomoRunState) {
      case POMO_RUNNING: return phaseColor(pomoPhase);
      case POMO_PAUSED:  return COLOR_BUSY;
      default:            return COLOR_FREE;  // POMO_IDLE; POMO_PHASE_DONE blinks separately
    }
  }

  return COLOR_FREE;
}

void updateLeds() {
  if (statusOverride == OVERRIDE_AUTO) {
    if (currentProgram == PROGRAM_TIMER && timerState == STATE_DONE) {
      bool on = ((millis() - doneSinceMs) / DONE_BLINK_MS) % 2 == 0;
      setAllLeds(on ? COLOR_FREE : COLOR_OFF);
      return;
    }
    if (currentProgram == PROGRAM_POMODORO && pomoRunState == POMO_PHASE_DONE) {
      bool on = ((millis() - pomoPhaseDoneSinceMs) / DONE_BLINK_MS) % 2 == 0;
      setAllLeds(on ? phaseColor(pomoPhase) : COLOR_OFF);
      return;
    }
  }
  setAllLeds(statusColor());
}

// =========================== focus timer =============================

void startFocusTimer() {
  totalMs = ((long)setHours * 3600L + (long)setMinutes * 60L + setSeconds) * 1000L;
  if (totalMs <= 0) return;  // nothing set - ignore start
  remainingMs = totalMs;
  lastTickMs = millis();
  timerState = STATE_RUNNING;
}

void tickTimer() {
  if (currentProgram != PROGRAM_TIMER || timerState != STATE_RUNNING) return;
  unsigned long now = millis();
  remainingMs -= (long)(now - lastTickMs);
  lastTickMs = now;
  if (remainingMs <= 0) {
    remainingMs = 0;
    timerState = STATE_DONE;
    doneSinceMs = now;
  }
}

void cycleField(int dir) {
  selectedField = (Field)(((int)selectedField + dir + FIELD_COUNT) % FIELD_COUNT);
}

void adjustField(int dir) {
  switch (selectedField) {
    case FIELD_HOUR:   setHours   = (uint8_t)((setHours   + dir + 24) % 24); break;
    case FIELD_MINUTE: setMinutes = (uint8_t)((setMinutes + dir + 60) % 60); break;
    case FIELD_SECOND: setSeconds = (uint8_t)((setSeconds + dir + 60) % 60); break;
    case FIELD_COUNT:  break;
  }
}

// ============================ pomodoro ================================

void pomoStart(PomoPhase phase) {
  pomoPhase = phase;
  uint16_t minutes = DEFAULT_POMO_WORK_MIN;
  switch (phase) {
    case PHASE_WORK:        minutes = settings.pomoWorkMin; break;
    case PHASE_SHORT_BREAK: minutes = settings.pomoShortBreakMin; break;
    case PHASE_LONG_BREAK:  minutes = settings.pomoLongBreakMin; break;
  }
  pomoTotalMs = (long)minutes * 60L * 1000L;
  pomoRemainingMs = pomoTotalMs;
  pomoLastTickMs = millis();
  pomoRunState = POMO_RUNNING;
}

void pomoAdvancePhase() {
  PomoPhase next;
  if (pomoPhase == PHASE_WORK) {
    pomoSessionCount++;
    if (pomoSessionCount >= settings.pomoSessionsBeforeLong) {
      next = PHASE_LONG_BREAK;
      pomoSessionCount = 0;
    } else {
      next = PHASE_SHORT_BREAK;
    }
  } else {
    next = PHASE_WORK;
  }
  pomoPhase = next;
  pomoRunState = POMO_PHASE_DONE;
  pomoPhaseDoneSinceMs = millis();
}

void tickPomodoro() {
  if (currentProgram != PROGRAM_POMODORO) return;

  if (pomoRunState == POMO_RUNNING) {
    unsigned long now = millis();
    pomoRemainingMs -= (long)(now - pomoLastTickMs);
    pomoLastTickMs = now;
    if (pomoRemainingMs <= 0) {
      pomoRemainingMs = 0;
      pomoAdvancePhase();
    }
  } else if (pomoRunState == POMO_PHASE_DONE) {
    if ((millis() - pomoPhaseDoneSinceMs) > POMO_PHASE_TRANSITION_MS) {
      pomoStart(pomoPhase);  // auto-continue into the phase we already advanced to
    }
  }
}

// ========================== menu navigation ============================

void enterProgram(int index) {
  switch (index) {
    case 0:
      currentProgram = PROGRAM_TIMER;
      timerState = STATE_SET;
      setHours = 0;
      setMinutes = settings.focusMinutes;
      setSeconds = 0;
      selectedField = FIELD_MINUTE;
      break;
    case 1:
      currentProgram = PROGRAM_POMODORO;
      pomoRunState = POMO_IDLE;
      pomoPhase = PHASE_WORK;
      pomoSessionCount = 0;
      break;
    case 2:
      currentProgram = PROGRAM_AVAILABILITY;
      break;
    case 3:
      currentProgram = PROGRAM_INFO;
      break;
  }
}

// ========================== buttons =================================

void cycleOverride(int direction) {
  statusOverride = (StatusOverride)(((int)statusOverride + direction + OVERRIDE_COUNT) % OVERRIDE_COUNT);
}

void handleButtons() {
  btnUp.update(); btnDown.update(); btnLeft.update();
  btnRight.update(); btnPress.update();

  // Long-press the center click, from anywhere but the menu, to go home.
  if (btnPress.longPressed && currentProgram != PROGRAM_MENU) {
    if (currentProgram == PROGRAM_TIMER) timerState = STATE_SET;
    if (currentProgram == PROGRAM_POMODORO) {
      pomoRunState = POMO_IDLE;
      pomoSessionCount = 0;
    }
    currentProgram = PROGRAM_MENU;
    return;
  }

  // Left/right cycles the manual status override almost anywhere - except
  // while editing the focus timer's H:M:S fields, which needs both directions
  // for its own field-selection.
  bool editingTimerFields = (currentProgram == PROGRAM_TIMER && timerState == STATE_SET);
  if (!editingTimerFields) {
    if (btnRight.clicked) cycleOverride(+1);
    if (btnLeft.clicked)  cycleOverride(-1);
  }

  switch (currentProgram) {
    case PROGRAM_MENU:
      if (btnUp.clicked)   menuIndex = (menuIndex - 1 + MENU_COUNT) % MENU_COUNT;
      if (btnDown.clicked) menuIndex = (menuIndex + 1) % MENU_COUNT;
      if (btnPress.clicked) enterProgram(menuIndex);
      break;

    case PROGRAM_TIMER:
      switch (timerState) {
        case STATE_SET:
          if (btnLeft.clicked)  cycleField(-1);
          if (btnRight.clicked) cycleField(+1);
          if (btnUp.clicked)    adjustField(+1);
          if (btnDown.clicked)  adjustField(-1);
          if (btnPress.clicked) startFocusTimer();
          break;

        case STATE_RUNNING:
          if (btnPress.clicked) timerState = STATE_PAUSED;
          break;

        case STATE_PAUSED:
          if (btnPress.clicked) {           // resume
            lastTickMs = millis();
            timerState = STATE_RUNNING;
          }
          if (btnDown.clicked) timerState = STATE_SET;  // cancel session
          break;

        case STATE_DONE:
          if (btnPress.clicked || btnUp.clicked || btnDown.clicked ||
              btnLeft.clicked || btnRight.clicked) {
            timerState = STATE_SET;
          }
          break;
      }
      if (timerState == STATE_DONE &&
          (millis() - doneSinceMs) > DONE_AUTO_DISMISS_MS) {
        timerState = STATE_SET;
      }
      break;

    case PROGRAM_POMODORO:
      switch (pomoRunState) {
        case POMO_IDLE:
          if (btnPress.clicked) pomoStart(PHASE_WORK);
          break;

        case POMO_RUNNING:
          if (btnPress.clicked) pomoRunState = POMO_PAUSED;
          break;

        case POMO_PAUSED:
          if (btnPress.clicked) {           // resume
            pomoLastTickMs = millis();
            pomoRunState = POMO_RUNNING;
          }
          if (btnDown.clicked) {            // cancel the whole cycle
            pomoRunState = POMO_IDLE;
            pomoSessionCount = 0;
          }
          break;

        case POMO_PHASE_DONE:
          // any button skips the auto-continue wait and starts the next phase now
          if (btnPress.clicked || btnUp.clicked || btnDown.clicked ||
              btnLeft.clicked || btnRight.clicked) {
            pomoStart(pomoPhase);
          }
          break;
      }
      break;

    case PROGRAM_AVAILABILITY:
      if (btnPress.clicked) {
        statusOverride = (statusOverride == OVERRIDE_BUSY) ? OVERRIDE_FREE : OVERRIDE_BUSY;
      }
      break;

    case PROGRAM_INFO:
      break;  // nothing to interact with - hold=menu handles navigation
  }
}

// ======================== orientation ===============================

void readOrientation() {
  if (!imuOk || !settings.flipEnabled) return;

  int16_t accelGyro[6] = {0};
  if (bmi160.getAccelGyroData(accelGyro) != 0) return;

  float axRaw = accelGyro[3] / 16384.0f;  // raw counts -> g (at +/-2g range)
  axFiltered = axFiltered * (1.0f - EMA_ALPHA) + axRaw * EMA_ALPHA;

  Orientation instant;
  if (axFiltered > FLIP_THRESHOLD) instant = ORIENT_NORMAL;
  else if (axFiltered < -FLIP_THRESHOLD) instant = ORIENT_FLIPPED;
  else return;  // dead zone: keep last known state

  if (instant != candidateOrientation) {
    candidateOrientation = instant;
    candidateSinceMs = millis();
  }
  if (candidateOrientation != currentOrientation &&
      (millis() - candidateSinceMs) > FLIP_DEBOUNCE_MS) {
    currentOrientation = candidateOrientation;
    u8g2.setDisplayRotation(currentOrientation == ORIENT_NORMAL ? U8G2_R0 : U8G2_R2);
  }
}

// ========================== display =================================

const char* overrideLabel() {
  switch (statusOverride) {
    case OVERRIDE_FREE:  return "set:free";
    case OVERRIDE_BUSY:  return "set:busy";
    case OVERRIDE_FOCUS: return "set:focus";
    default:             return "";
  }
}

void drawCentered(const uint8_t* font, int y, const char* text) {
  u8g2.setFont(font);
  u8g2.drawStr((128 - u8g2.getStrWidth(text)) / 2, y, text);
}

void drawMenu() {
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 10, "Select program");

  // Keep the selected item inside the visible window, scrolling the minimum
  // amount needed rather than jumping - menuScroll persists across frames.
  if (menuIndex < menuScroll) menuScroll = menuIndex;
  if (menuIndex >= menuScroll + MENU_VISIBLE_ROWS) menuScroll = menuIndex - MENU_VISIBLE_ROWS + 1;

  for (int row = 0; row < MENU_VISIBLE_ROWS; row++) {
    int i = menuScroll + row;
    if (i >= MENU_COUNT) break;
    char line[24];
    snprintf(line, sizeof(line), "%s%s", i == menuIndex ? "> " : "  ", MENU_ITEMS[i]);
    u8g2.drawStr(4, 24 + row * 13, line);
  }

  // small arrows hinting there's more above/below the visible window
  if (menuScroll > 0) u8g2.drawStr(122, 24, "^");
  if (menuScroll + MENU_VISIBLE_ROWS < MENU_COUNT) u8g2.drawStr(122, 24 + (MENU_VISIBLE_ROWS - 1) * 13, "v");

  u8g2.drawStr(0, 62, "up/dn=move click=open");
}

void drawTimer() {
  char big[16];
  const char* title = "";
  const char* hint = "";
  long s;

  switch (timerState) {
    case STATE_SET: {
      title = "Set Timer";
      char h[6], m[6], sec[6];
      snprintf(h,   sizeof(h),   selectedField == FIELD_HOUR   ? "[%02u]" : "%02u", setHours);
      snprintf(m,   sizeof(m),   selectedField == FIELD_MINUTE ? "[%02u]" : "%02u", setMinutes);
      snprintf(sec, sizeof(sec), selectedField == FIELD_SECOND ? "[%02u]" : "%02u", setSeconds);
      snprintf(big, sizeof(big), "%s:%s:%s", h, m, sec);
      hint = "</> field ^v +/-";
      break;
    }
    case STATE_RUNNING:
      title = "Focus";
      s = remainingMs / 1000;
      snprintf(big, sizeof(big), "%02ld:%02ld:%02ld", s / 3600, (s / 60) % 60, s % 60);
      hint = "click=pause";
      break;
    case STATE_PAUSED:
      title = "Paused";
      s = remainingMs / 1000;
      snprintf(big, sizeof(big), "%02ld:%02ld:%02ld", s / 3600, (s / 60) % 60, s % 60);
      hint = "click=resume v=stop";
      break;
    case STATE_DONE:
      title = "Done!";
      snprintf(big, sizeof(big), "00:00:00");
      hint = "click=dismiss";
      break;
  }

  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 10, title);
  const char* ovr = overrideLabel();
  u8g2.drawStr(128 - u8g2.getStrWidth(ovr), 10, ovr);

  drawCentered(u8g2_font_logisoso16_tr, 34, big);

  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 48, hint);

  if (timerState == STATE_RUNNING || timerState == STATE_PAUSED) {
    int lit = 0;
    if (totalMs > 0 && remainingMs > 0) {
      lit = (int)((remainingMs * (long)NUM_LEDS + totalMs - 1) / totalMs);  // ceil
      if (lit > NUM_LEDS) lit = NUM_LEDS;
    }
    char bar[NUM_LEDS + 1];
    for (int i = 0; i < NUM_LEDS; i++) bar[i] = (i < lit) ? '#' : '-';
    bar[NUM_LEDS] = '\0';
    u8g2.drawStr(0, 62, bar);
  } else {
    u8g2.drawStr(0, 62, "hold=menu");
  }
}

const char* phaseLabel(PomoPhase phase) {
  switch (phase) {
    case PHASE_WORK:        return "Work";
    case PHASE_SHORT_BREAK: return "Short Break";
    case PHASE_LONG_BREAK:  return "Long Break";
  }
  return "";
}

void drawPomodoro() {
  char big[20];
  char sessionLine[20];
  const char* hint = "";
  long s;

  switch (pomoRunState) {
    case POMO_IDLE:
      snprintf(big, sizeof(big), "%02u:00", settings.pomoWorkMin);
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

  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 10, phaseLabel(pomoPhase));
  const char* ovr = overrideLabel();
  u8g2.drawStr(128 - u8g2.getStrWidth(ovr), 10, ovr);

  if (pomoRunState == POMO_PHASE_DONE) {
    drawCentered(u8g2_font_6x10_tr, 34, big);
  } else {
    drawCentered(u8g2_font_logisoso16_tr, 34, big);
  }

  uint8_t sessionDisplay = pomoSessionCount + 1;
  if (sessionDisplay > settings.pomoSessionsBeforeLong) sessionDisplay = settings.pomoSessionsBeforeLong;
  snprintf(sessionLine, sizeof(sessionLine), "Session %u/%u", sessionDisplay, settings.pomoSessionsBeforeLong);
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 48, sessionLine);
  u8g2.drawStr(0, 62, hint);
}

void drawAvailability() {
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 10, "Status");
  drawCentered(u8g2_font_9x15B_tr, 36,
               statusOverride == OVERRIDE_BUSY ? "BUSY" : "AVAILABLE");
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 50, "click=toggle");
  u8g2.drawStr(0, 62, "hold=menu");
}

void drawInfo() {
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 10, "Info");

  char line[32];

  snprintf(line, sizeof(line), "SSID: %s", WIFI_AP_SSID);
  u8g2.drawStr(0, 24, line);

  snprintf(line, sizeof(line), "IP:   %s", WiFi.softAPIP().toString().c_str());
  u8g2.drawStr(0, 36, line);

  snprintf(line, sizeof(line), "MAC:  %s", WiFi.macAddress().c_str());
  u8g2.drawStr(0, 48, line);

  u8g2.drawStr(0, 62, "hold=menu");
}

void drawScreen() {
  u8g2.clearBuffer();
  switch (currentProgram) {
    case PROGRAM_MENU:         drawMenu();         break;
    case PROGRAM_TIMER:        drawTimer();        break;
    case PROGRAM_POMODORO:     drawPomodoro();     break;
    case PROGRAM_AVAILABILITY: drawAvailability(); break;
    case PROGRAM_INFO:         drawInfo();         break;
  }
  u8g2.sendBuffer();
}

// ======================= Wi-Fi settings page ========================

String settingsPage(const String& notice) {
  String html =
    "<!DOCTYPE html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>FocusDock</title>"
    "<style>body{font-family:sans-serif;max-width:22em;margin:2em auto;padding:0 1em}"
    "label{display:block;margin:1em 0 .2em}input[type=number]{width:6em}"
    "input[type=text],input[type=password],select{width:100%;box-sizing:border-box}"
    "fieldset{margin-top:1.5em}button{margin-top:1.2em;padding:.5em 1.5em}.ok{color:green}"
    "</style></head><body>"
    "<h2>FocusDock settings</h2>";
  if (notice.length()) html += "<p class=ok>" + notice + "</p>";
  html += "<form method=POST action=/save>";

  html += "<fieldset><legend>Device</legend>";
  html += "<label>LED brightness (1-255)</label><input type=number name=bright min=1 max=255 value=" + String(settings.brightness) + ">";
  html += "<label>Default focus minutes (" + String(TIMER_MIN_MINUTES) + "-" + String(TIMER_MAX_MINUTES) + ")</label>"
          "<input type=number name=minutes min=" + String(TIMER_MIN_MINUTES) + " max=" + String(TIMER_MAX_MINUTES) + " value=" + String(settings.focusMinutes) + ">";
  html += "<label><input type=checkbox name=flip value=1 " + String(settings.flipEnabled ? "checked" : "") + "> Auto-flip display</label>";
  html += "</fieldset>";

  html += "<fieldset><legend>Pomodoro</legend>";
  html += "<label>Work minutes (" + String(POMO_MIN_MINUTES) + "-" + String(POMO_MAX_MINUTES) + ")</label>"
          "<input type=number name=pomoWork min=" + String(POMO_MIN_MINUTES) + " max=" + String(POMO_MAX_MINUTES) + " value=" + String(settings.pomoWorkMin) + ">";
  html += "<label>Short break minutes</label>"
          "<input type=number name=pomoShort min=" + String(POMO_MIN_MINUTES) + " max=" + String(POMO_MAX_MINUTES) + " value=" + String(settings.pomoShortBreakMin) + ">";
  html += "<label>Long break minutes</label>"
          "<input type=number name=pomoLong min=" + String(POMO_MIN_MINUTES) + " max=" + String(POMO_MAX_MINUTES) + " value=" + String(settings.pomoLongBreakMin) + ">";
  html += "<label>Work sessions before a long break (" + String(POMO_SESSIONS_MIN) + "-" + String(POMO_SESSIONS_MAX) + ")</label>"
          "<input type=number name=pomoSessions min=" + String(POMO_SESSIONS_MIN) + " max=" + String(POMO_SESSIONS_MAX) + " value=" + String(settings.pomoSessionsBeforeLong) + ">";
  html += "</fieldset>";

  html += "<button>Save</button></form></body></html>";
  return html;
}

void handleRoot() { server.send(200, "text/html", settingsPage("")); }

void handleSave() {
  if (server.hasArg("bright")) {
    long v = server.arg("bright").toInt();
    settings.brightness = (uint8_t)constrain(v, 1, 255);
  }
  if (server.hasArg("minutes")) {
    long v = server.arg("minutes").toInt();
    settings.focusMinutes = (uint16_t)constrain(v, TIMER_MIN_MINUTES, TIMER_MAX_MINUTES);
  }
  settings.flipEnabled = server.hasArg("flip");

  if (server.hasArg("pomoWork")) {
    settings.pomoWorkMin = (uint16_t)constrain(server.arg("pomoWork").toInt(), POMO_MIN_MINUTES, POMO_MAX_MINUTES);
  }
  if (server.hasArg("pomoShort")) {
    settings.pomoShortBreakMin = (uint16_t)constrain(server.arg("pomoShort").toInt(), POMO_MIN_MINUTES, POMO_MAX_MINUTES);
  }
  if (server.hasArg("pomoLong")) {
    settings.pomoLongBreakMin = (uint16_t)constrain(server.arg("pomoLong").toInt(), POMO_MIN_MINUTES, POMO_MAX_MINUTES);
  }
  if (server.hasArg("pomoSessions")) {
    settings.pomoSessionsBeforeLong = (uint8_t)constrain(server.arg("pomoSessions").toInt(), POMO_SESSIONS_MIN, POMO_SESSIONS_MAX);
  }

  saveSettings();
  strip.setBrightness(settings.brightness);
  if (currentProgram == PROGRAM_TIMER && timerState == STATE_SET) {
    setMinutes = settings.focusMinutes;
  }
  if (!settings.flipEnabled) {
    currentOrientation = ORIENT_NORMAL;
    u8g2.setDisplayRotation(U8G2_R0);
  }

  server.send(200, "text/html", settingsPage("Saved."));
}

void startWifiPortal() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();
  Serial.printf("Wi-Fi hotspot '%s' up, settings at http://%s\n",
                WIFI_AP_SSID, WiFi.softAPIP().toString().c_str());
}

// The settings hotspot must always be reachable. Periodically confirm it's
// still up and bring it back if anything ever knocked it down.
void ensureApOn() {
  if (millis() - lastApCheckMs < AP_CHECK_MS) return;
  lastApCheckMs = millis();

  bool apModeSet = (WiFi.getMode() & WIFI_MODE_AP) != 0;
  bool apHasIp = WiFi.softAPIP() != IPAddress(0, 0, 0, 0);
  if (!apModeSet || !apHasIp) {
    Serial.println("Hotspot was down - restarting it");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
  }
}

// ============================ setup/loop ============================

void setup() {
  Serial.begin(115200);
  delay(500);
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

  loadSettings();

  u8g2.begin();

  strip.begin();
  strip.setBrightness(settings.brightness);
  setAllLeds(COLOR_OFF);

  btnUp.begin(); btnDown.begin(); btnLeft.begin();
  btnRight.begin(); btnPress.begin();

  if (bmi160.softReset() == BMI160_OK &&
      bmi160.I2cInit(BMI160_I2C_ADDR) == BMI160_OK) {
    imuOk = true;
    Serial.println("IMU: init OK");
  } else {
    Serial.println("IMU: init failed - auto-flip disabled this session");
  }

  startWifiPortal();
}

void loop() {
  handleButtons();
  tickTimer();
  tickPomodoro();
  readOrientation();
  ensureApOn();
  updateLeds();
  server.handleClient();

  static unsigned long lastDrawMs = 0;
  if (millis() - lastDrawMs >= DISPLAY_REDRAW_MS) {
    lastDrawMs = millis();
    drawScreen();
  }

  delay(LOOP_DELAY_MS);
}
