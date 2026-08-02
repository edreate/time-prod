// Test 4: button-driven H:M:S timer with a 10-LED countdown progress bar.
//
// Purpose: exercise the 5-way button + LED strip together, independent of
// the orientation/IMU code, before folding this behavior into main.cpp.
//
// State machine:
//   STATE_SET     -> editing the target time. Left/Right pick which field
//                    (hour/minute/second) is active; Up/Down change it.
//                    Each field wraps like a clock digit (23->0, 59->0, ...)
//                    rather than clamping, since there's no reason a "set
//                    the time" control should dead-end at a limit.
//   STATE_RUNNING -> counting down. Click pauses.
//   STATE_PAUSED  -> click resumes; Down cancels back to STATE_SET (mirrors
//                    main.cpp's "Down = cancel session" while paused).
//   STATE_DONE    -> countdown hit zero. Any button press dismisses back to
//                    STATE_SET so the configured H:M:S is ready to re-run.
//
// LED progress bar (NUM_LEDS = 10, one LED per 10% of the configured time):
//   Empties out as time runs out - all 10 lit at start, they go dark one by
//   one, none lit at zero. Lit count uses integer ceiling division so a LED
//   only goes dark once its 10% slice has fully elapsed (not the instant
//   the countdown starts ticking through it). LEDs stay off while setting
//   the time (nothing is running yet) and freeze at the current count while
//   paused. At STATE_DONE the whole strip blinks until dismissed.
//
// This assumes a 10-LED strip wired to PIN_LED_DATA, unlike main.cpp's
// 8-LED status strip - update NUM_LEDS below to match your actual wiring.
//
// Wiring: see docs/HARDWARE.md.

#include <Wire.h>
#include <U8g2lib.h>
#include <Adafruit_NeoPixel.h>

// ============================== CONFIG ==============================
#define PIN_I2C_SDA      8
#define PIN_I2C_SCL      9
#define PIN_LED_DATA     16
#define PIN_BTN_UP       4
#define PIN_BTN_DOWN     5
#define PIN_BTN_LEFT     6
#define PIN_BTN_RIGHT    7
#define PIN_BTN_PRESS    15

#define NUM_LEDS                10     // 10 LEDs = one per 10% of the timer
#define LED_BRIGHTNESS           60    // 0-255; capped low on purpose (power budget)
#define COLOR_PROGRESS           0xFFA500  // amber = time remaining
#define COLOR_DONE                0x00FF00 // green = done, blinking
#define COLOR_OFF                 0x000000
#define DONE_BLINK_MS             500

#define BTN_DEBOUNCE_MS           30

#define DEFAULT_HOURS             0
#define DEFAULT_MINUTES           25    // classic pomodoro length
#define DEFAULT_SECONDS           0

#define LOOP_DELAY_MS             10
#define DISPLAY_REDRAW_MS         100
// ============================ END CONFIG =============================

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
Adafruit_NeoPixel strip(NUM_LEDS, PIN_LED_DATA, NEO_GRB + NEO_KHZ800);

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

// ---- debounced button (same pattern as main.cpp) ----
struct Button {
  explicit Button(uint8_t p) : pin(p) {}

  uint8_t pin;
  bool stable = true;        // true = released (pin idles high via pull-up)
  bool lastReading = true;
  unsigned long lastChangeMs = 0;
  bool clicked = false;      // true for exactly one loop pass after a press

  void begin() { pinMode(pin, INPUT_PULLUP); }

  void update() {
    clicked = false;
    bool reading = digitalRead(pin);
    if (reading != lastReading) {
      lastChangeMs = millis();
      lastReading = reading;
    }
    if ((millis() - lastChangeMs) > BTN_DEBOUNCE_MS && reading != stable) {
      stable = reading;
      if (stable == LOW) clicked = true;
    }
  }
};

Button btnUp{PIN_BTN_UP}, btnDown{PIN_BTN_DOWN}, btnLeft{PIN_BTN_LEFT},
       btnRight{PIN_BTN_RIGHT}, btnPress{PIN_BTN_PRESS};

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
}

// =========================== timer ==================================

void startTimer() {
  totalMs = ((long)setHours * 3600L + (long)setMinutes * 60L + setSeconds) * 1000L;
  if (totalMs <= 0) return;  // nothing set - ignore start
  remainingMs = totalMs;
  lastTickMs = millis();
  timerState = STATE_RUNNING;
  Serial.printf("timer start: %02u:%02u:%02u\n", setHours, setMinutes, setSeconds);
}

void tickTimer() {
  if (timerState != STATE_RUNNING) return;
  unsigned long now = millis();
  remainingMs -= (long)(now - lastTickMs);
  lastTickMs = now;
  if (remainingMs <= 0) {
    remainingMs = 0;
    timerState = STATE_DONE;
    doneSinceMs = now;
    Serial.println("timer done");
  }
}

// ========================== buttons =================================

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

void handleButtons() {
  btnUp.update(); btnDown.update(); btnLeft.update();
  btnRight.update(); btnPress.update();

  switch (timerState) {
    case STATE_SET:
      if (btnLeft.clicked)  cycleField(-1);
      if (btnRight.clicked) cycleField(+1);
      if (btnUp.clicked)    adjustField(+1);
      if (btnDown.clicked)  adjustField(-1);
      if (btnPress.clicked) startTimer();
      break;

    case STATE_RUNNING:
      if (btnPress.clicked) timerState = STATE_PAUSED;
      break;

    case STATE_PAUSED:
      if (btnPress.clicked) {           // resume
        lastTickMs = millis();
        timerState = STATE_RUNNING;
      }
      if (btnDown.clicked) timerState = STATE_SET;  // cancel back to editing
      break;

    case STATE_DONE:
      if (btnPress.clicked || btnUp.clicked || btnDown.clicked ||
          btnLeft.clicked || btnRight.clicked) {
        timerState = STATE_SET;
      }
      break;
  }
}

// ========================== display =================================

void drawScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);

  char big[16];
  const char* title = "";
  const char* hint = "";
  long s;

  switch (timerState) {
    case STATE_SET: {
      title = "Set Timer";
      // brackets mark the field currently being edited
      char h[6], m[6], sec[6];
      snprintf(h,   sizeof(h),   selectedField == FIELD_HOUR   ? "[%02u]" : "%02u",   setHours);
      snprintf(m,   sizeof(m),   selectedField == FIELD_MINUTE ? "[%02u]" : "%02u",   setMinutes);
      snprintf(sec, sizeof(sec), selectedField == FIELD_SECOND ? "[%02u]" : "%02u",   setSeconds);
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

  u8g2.drawStr(0, 10, title);
  u8g2.setFont(u8g2_font_logisoso16_tr);
  u8g2.drawStr((128 - u8g2.getStrWidth(big)) / 2, 34, big);
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 48, hint);

  // ASCII mirror of the LED progress bar, for bench-testing without eyeballing the strip
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

  btnUp.begin(); btnDown.begin(); btnLeft.begin();
  btnRight.begin(); btnPress.begin();

  Serial.println("button timer test ready");
}

void loop() {
  handleButtons();
  tickTimer();
  updateLeds();

  static unsigned long lastDrawMs = 0;
  if (millis() - lastDrawMs >= DISPLAY_REDRAW_MS) {
    lastDrawMs = millis();
    drawScreen();
  }

  delay(LOOP_DELAY_MS);
}
