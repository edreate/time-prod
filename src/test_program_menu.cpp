// Test 5: a small menu of programs, selected and driven by the 5-way
// button, with IMU auto-flip running underneath exactly like main.cpp.
//
// Button mapping - IMPORTANT:
//   The 5-way module's silkscreen reads F/B/L/R, but that does not match
//   the on-screen Up/Down/Left/Right the UI needs (the module sits rotated
//   in the enclosure relative to the display). The physical-to-logical
//   mapping is fixed at the wires, not runtime-dependent on the IMU flip:
//     physical F -> logical LEFT
//     physical B -> logical RIGHT
//     physical L -> logical DOWN
//     physical R -> logical UP
//   All button logic below is written in terms of logical
//   left/right/up/down/press, computed once per loop from the physical
//   pins - see the top of handleButtons().
//
// Navigation:
//   Menu          Up/Down move the cursor, click opens the program.
//   Any program   A LONG press (hold >LONG_PRESS_MS) always returns to the
//                 menu - one consistent "go home" gesture so no direction
//                 button has to be sacrificed for it.
//   Timer         Left/Right pick the H/M/S field, Up/Down change it,
//                 short click starts/pauses/resumes (same as
//                 test_button_timer.cpp). LEDs: 10-LED countdown bar,
//                 emptying out as time runs out, blinking green when done.
//   Availability  Short click toggles Available/Busy. LEDs: solid green
//                 (available) or solid red (busy) - the whole point of the
//                 program is to be readable at a glance.
//
// Orientation: unchanged from test_orientation_led.cpp - a single filtered
// accel axis with hysteresis + debounce flips the display 180 degrees
// depending on which edge of the monitor the device is clipped to. Runs
// every loop regardless of which program is active.
//
// Wiring: see docs/HARDWARE.md.

#include <Wire.h>
#include <U8g2lib.h>
#include <DFRobot_BMI160.h>
#include <Adafruit_NeoPixel.h>

// ============================== CONFIG ==============================
#define PIN_I2C_SDA      8
#define PIN_I2C_SCL      9
#define PIN_LED_DATA     16
#define PIN_BTN_F        4    // physical "F" -> logical LEFT
#define PIN_BTN_B        5    // physical "B" -> logical RIGHT
#define PIN_BTN_L        6    // physical "L" -> logical DOWN
#define PIN_BTN_R        7    // physical "R" -> logical UP
#define PIN_BTN_PRESS    15
#define BMI160_I2C_ADDR  0x68

#define NUM_LEDS                10     // 10 LEDs = one per 10% of the timer
#define LED_BRIGHTNESS           60    // 0-255; capped low on purpose (power budget)
#define COLOR_AVAILABLE          0x00FF00  // green = free
#define COLOR_BUSY               0xFF0000  // red   = busy
#define COLOR_PROGRESS           0xFFA500  // amber = timer running
#define COLOR_DONE               0x00FF00  // green = timer done, blinking
#define COLOR_OFF                0x000000
#define DONE_BLINK_MS             500

#define BTN_DEBOUNCE_MS           30
#define LONG_PRESS_MS             800   // hold this long anywhere -> back to menu

#define DEFAULT_HOURS             0
#define DEFAULT_MINUTES           25    // classic pomodoro length
#define DEFAULT_SECONDS           0

#define FLIP_THRESHOLD           0.5f   // g; accel-x past +/- this commits a state
#define FLIP_DEBOUNCE_MS         350    // must hold past threshold this long
#define EMA_ALPHA                0.2f   // accel smoothing, 0..1 (higher = jumpier)

#define LOOP_DELAY_MS             10
#define DISPLAY_REDRAW_MS         100
// ============================ END CONFIG =============================

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
DFRobot_BMI160 bmi160;
Adafruit_NeoPixel strip(NUM_LEDS, PIN_LED_DATA, NEO_GRB + NEO_KHZ800);

// ---- programs ----
enum Program { PROGRAM_MENU, PROGRAM_TIMER, PROGRAM_AVAILABILITY };
Program currentProgram = PROGRAM_MENU;

const char* MENU_ITEMS[] = { "Timer", "Available / Busy" };
const int MENU_COUNT = 2;
int menuIndex = 0;

// ---- availability program ----
enum Availability { AVAIL_AVAILABLE, AVAIL_BUSY };
Availability availability = AVAIL_AVAILABLE;

// ---- timer program (same state machine as test_button_timer.cpp) ----
enum TimerState { STATE_SET, STATE_RUNNING, STATE_PAUSED, STATE_DONE };
TimerState timerState = STATE_SET;

enum Field { FIELD_HOUR, FIELD_MINUTE, FIELD_SECOND, FIELD_COUNT };
Field selectedField = FIELD_MINUTE;

uint8_t setHours = DEFAULT_HOURS;
uint8_t setMinutes = DEFAULT_MINUTES;
uint8_t setSeconds = DEFAULT_SECONDS;

long totalMs = 0;
long remainingMs = 0;
unsigned long lastTickMs = 0;
unsigned long doneSinceMs = 0;

// ---- orientation ----
enum Orientation { ORIENT_NORMAL, ORIENT_FLIPPED };
Orientation currentOrientation = ORIENT_NORMAL;
Orientation candidateOrientation = ORIENT_NORMAL;
unsigned long candidateSinceMs = 0;
float axFiltered = 0.0f;
bool imuOk = false;

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

Button btnF{PIN_BTN_F}, btnB{PIN_BTN_B}, btnL{PIN_BTN_L},
       btnR{PIN_BTN_R}, btnPress{PIN_BTN_PRESS};

// ============================= LEDs =================================

void setAllLeds(uint32_t color) {
  for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, color);
  strip.show();
}

// Lights the first `lit` LEDs (out of NUM_LEDS) in COLOR_PROGRESS, rest off.
void renderProgress(long remaining, long total) {
  int lit = 0;
  if (total > 0 && remaining > 0) {
    lit = (int)((remaining * (long)NUM_LEDS + total - 1) / total);  // ceil
    if (lit > NUM_LEDS) lit = NUM_LEDS;
  }
  for (int i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, i < lit ? COLOR_PROGRESS : COLOR_OFF);
  }
  strip.show();
}

void updateLeds() {
  switch (currentProgram) {
    case PROGRAM_MENU:
      setAllLeds(COLOR_OFF);
      break;

    case PROGRAM_AVAILABILITY:
      setAllLeds(availability == AVAIL_AVAILABLE ? COLOR_AVAILABLE : COLOR_BUSY);
      break;

    case PROGRAM_TIMER:
      switch (timerState) {
        case STATE_SET:
          setAllLeds(COLOR_OFF);
          break;
        case STATE_RUNNING:
        case STATE_PAUSED:
          renderProgress(remainingMs, totalMs);
          break;
        case STATE_DONE: {
          bool on = ((millis() - doneSinceMs) / DONE_BLINK_MS) % 2 == 0;
          setAllLeds(on ? COLOR_DONE : COLOR_OFF);
          break;
        }
      }
      break;
  }
}

// =========================== timer ==================================

void startTimer() {
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

// ========================== buttons =================================

void handleButtons() {
  btnF.update(); btnB.update(); btnL.update();
  btnR.update(); btnPress.update();

  // physical -> logical, per the wiring note at the top of this file
  bool leftClicked  = btnF.clicked;
  bool rightClicked = btnB.clicked;
  bool downClicked  = btnL.clicked;
  bool upClicked    = btnR.clicked;
  bool pressClicked = btnPress.clicked;
  bool pressLong    = btnPress.longPressed;

  if (pressLong) {
    if (currentProgram == PROGRAM_TIMER) timerState = STATE_SET;  // abandon any running session
    currentProgram = PROGRAM_MENU;
    return;
  }

  switch (currentProgram) {
    case PROGRAM_MENU:
      if (upClicked)   menuIndex = (menuIndex - 1 + MENU_COUNT) % MENU_COUNT;
      if (downClicked) menuIndex = (menuIndex + 1) % MENU_COUNT;
      if (pressClicked) currentProgram = (menuIndex == 0) ? PROGRAM_TIMER : PROGRAM_AVAILABILITY;
      break;

    case PROGRAM_AVAILABILITY:
      if (pressClicked) {
        availability = (availability == AVAIL_AVAILABLE) ? AVAIL_BUSY : AVAIL_AVAILABLE;
      }
      break;

    case PROGRAM_TIMER:
      switch (timerState) {
        case STATE_SET:
          if (leftClicked)  cycleField(-1);
          if (rightClicked) cycleField(+1);
          if (upClicked)    adjustField(+1);
          if (downClicked)  adjustField(-1);
          if (pressClicked) startTimer();
          break;

        case STATE_RUNNING:
          if (pressClicked) timerState = STATE_PAUSED;
          break;

        case STATE_PAUSED:
          if (pressClicked) {              // resume
            lastTickMs = millis();
            timerState = STATE_RUNNING;
          }
          if (downClicked) timerState = STATE_SET;  // cancel back to editing
          break;

        case STATE_DONE:
          if (pressClicked || upClicked || downClicked || leftClicked || rightClicked) {
            timerState = STATE_SET;
          }
          break;
      }
      break;
  }
}

// ======================== orientation ===============================

void readOrientation() {
  if (!imuOk) return;

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

void drawCentered(const uint8_t* font, int y, const char* text) {
  u8g2.setFont(font);
  u8g2.drawStr((128 - u8g2.getStrWidth(text)) / 2, y, text);
}

void drawMenu() {
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 10, "Select program");
  for (int i = 0; i < MENU_COUNT; i++) {
    char line[24];
    snprintf(line, sizeof(line), "%s%s", i == menuIndex ? "> " : "  ", MENU_ITEMS[i]);
    u8g2.drawStr(4, 28 + i * 14, line);
  }
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

  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 10, title);
  drawCentered(u8g2_font_logisoso16_tr, 34, big);
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 48, hint);

  if (timerState == STATE_RUNNING || timerState == STATE_PAUSED) {
    int lit = 0;
    if (totalMs > 0 && remainingMs > 0) {
      lit = (int)((remainingMs * (long)NUM_LEDS + totalMs - 1) / totalMs);
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

void drawAvailability() {
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 10, "Status");
  drawCentered(u8g2_font_9x15B_tr, 36,
               availability == AVAIL_AVAILABLE ? "AVAILABLE" : "BUSY");
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 50, "click=toggle");
  u8g2.drawStr(0, 62, "hold=menu");
}

void drawScreen() {
  u8g2.clearBuffer();
  switch (currentProgram) {
    case PROGRAM_MENU:         drawMenu();         break;
    case PROGRAM_TIMER:        drawTimer();         break;
    case PROGRAM_AVAILABILITY: drawAvailability();  break;
  }
  u8g2.sendBuffer();
}

// ============================ setup/loop ============================

void setup() {
  Serial.begin(115200);
  delay(500);
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

  u8g2.begin();

  strip.begin();
  strip.setBrightness(LED_BRIGHTNESS);
  setAllLeds(COLOR_OFF);

  btnF.begin(); btnB.begin(); btnL.begin();
  btnR.begin(); btnPress.begin();

  if (bmi160.softReset() == BMI160_OK &&
      bmi160.I2cInit(BMI160_I2C_ADDR) == BMI160_OK) {
    imuOk = true;
    Serial.println("IMU: init OK");
  } else {
    Serial.println("IMU: init failed - auto-flip disabled this session");
  }

  Serial.println("program menu test ready");
}

void loop() {
  handleButtons();
  tickTimer();
  readOrientation();
  updateLeds();

  static unsigned long lastDrawMs = 0;
  if (millis() - lastDrawMs >= DISPLAY_REDRAW_MS) {
    lastDrawMs = millis();
    drawScreen();
  }

  delay(LOOP_DELAY_MS);
}
