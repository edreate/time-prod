// Focus & Availability Device - main firmware
//
// What it does:
//   - Focus timer: up/down sets the minutes, center click starts/pauses.
//   - Status light: LEDs are green when you're free, red while a focus
//     session runs. Left/right forces a color manually (meeting indicator).
//   - Auto-flip: the screen rotates 180 degrees when the device is mounted
//     upside down (top vs bottom edge of the monitor), using the IMU.
//   - Wi-Fi settings: the device broadcasts its own Wi-Fi hotspot. Join it
//     with a phone/laptop and open http://192.168.4.1 to change settings
//     (brightness, default focus minutes, auto-flip). Saved settings survive
//     power-off.
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
#define PIN_BTN_UP       4    // 5-way key "forward"
#define PIN_BTN_DOWN     5    // 5-way key "backward"
#define PIN_BTN_LEFT     6
#define PIN_BTN_RIGHT    7
#define PIN_BTN_PRESS    15   // 5-way key center click
#define BMI160_I2C_ADDR  0x68 // IMU address (SA0 pin tied to GND)

// ---- LED strip ----
#define NUM_LEDS                8      // how many LEDs are wired up
#define DEFAULT_LED_BRIGHTNESS  60     // 0-255; capped low on purpose (power budget)
#define COLOR_FOCUS             0xFF0000  // red    = focusing, do not disturb
#define COLOR_FREE              0x00FF00  // green  = available
#define COLOR_BUSY              0xFFB400  // amber  = busy / in a meeting
#define COLOR_OFF               0x000000

// ---- Focus timer ----
#define DEFAULT_FOCUS_MINUTES   25     // classic pomodoro length
#define TIMER_STEP_MINUTES      5      // one up/down click changes this much
#define TIMER_MIN_MINUTES       5
#define TIMER_MAX_MINUTES       120
#define DONE_BLINK_MS           500    // LED blink speed when time is up
#define DONE_AUTO_DISMISS_MS    60000  // "time's up" screen clears itself after this

// ---- Buttons ----
#define BTN_DEBOUNCE_MS         30     // ignore mechanical chatter shorter than this

// ---- Orientation (auto-flip) ----
#define FLIP_THRESHOLD          0.5f   // g; accel-x past +/- this commits a state
#define FLIP_DEBOUNCE_MS        350    // must hold past threshold this long
#define EMA_ALPHA               0.2f   // accel smoothing, 0..1 (higher = jumpier)

// ---- Wi-Fi settings hotspot ----
#define WIFI_AP_SSID            "FocusDock"
#define WIFI_AP_PASSWORD        "focus1234"  // min 8 chars, or "" for an open network

// ---- Loop pacing ----
#define LOOP_DELAY_MS           10
#define DISPLAY_REDRAW_MS       100    // don't redraw the OLED faster than this
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
};
Settings settings;

// ---- timer state machine ----
enum TimerState { STATE_IDLE, STATE_RUNNING, STATE_PAUSED, STATE_DONE };
TimerState timerState = STATE_IDLE;
uint16_t setMinutes = DEFAULT_FOCUS_MINUTES;
long remainingMs = 0;
unsigned long lastTickMs = 0;
unsigned long doneSinceMs = 0;

// ---- manual status override: left/right cycles through these ----
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

// ---- debounced button ----
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

// =========================== settings ===============================

void loadSettings() {
  prefs.begin("focusdock", /*readOnly=*/false);
  settings.brightness = prefs.getUChar("bright", DEFAULT_LED_BRIGHTNESS);
  settings.focusMinutes = prefs.getUShort("minutes", DEFAULT_FOCUS_MINUTES);
  settings.flipEnabled = prefs.getBool("flip", true);
  setMinutes = settings.focusMinutes;
}

void saveSettings() {
  prefs.putUChar("bright", settings.brightness);
  prefs.putUShort("minutes", settings.focusMinutes);
  prefs.putBool("flip", settings.flipEnabled);
}

// ============================= LEDs =================================

void setAllLeds(uint32_t color) {
  for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, color);
  strip.show();
}

uint32_t statusColor() {
  switch (statusOverride) {
    case OVERRIDE_FREE:  return COLOR_FREE;
    case OVERRIDE_BUSY:  return COLOR_BUSY;
    case OVERRIDE_FOCUS: return COLOR_FOCUS;
    case OVERRIDE_AUTO:  break;
  }
  switch (timerState) {
    case STATE_RUNNING: return COLOR_FOCUS;
    case STATE_PAUSED:  return COLOR_BUSY;
    case STATE_DONE:    // blink green: handled in updateLeds()
    case STATE_IDLE:    return COLOR_FREE;
  }
  return COLOR_FREE;
}

void updateLeds() {
  if (statusOverride == OVERRIDE_AUTO && timerState == STATE_DONE) {
    bool on = ((millis() - doneSinceMs) / DONE_BLINK_MS) % 2 == 0;
    setAllLeds(on ? COLOR_FREE : COLOR_OFF);
    return;
  }
  setAllLeds(statusColor());
}

// =========================== timer ==================================

void startTimer() {
  remainingMs = (long)setMinutes * 60L * 1000L;
  lastTickMs = millis();
  timerState = STATE_RUNNING;
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
  }
}

// ========================== buttons =================================

void cycleOverride(int direction) {
  statusOverride = (StatusOverride)(((int)statusOverride + direction + OVERRIDE_COUNT) % OVERRIDE_COUNT);
}

void handleButtons() {
  btnUp.update(); btnDown.update(); btnLeft.update();
  btnRight.update(); btnPress.update();

  if (btnRight.clicked) cycleOverride(+1);
  if (btnLeft.clicked)  cycleOverride(-1);

  switch (timerState) {
    case STATE_IDLE:
      if (btnUp.clicked && setMinutes + TIMER_STEP_MINUTES <= TIMER_MAX_MINUTES)
        setMinutes += TIMER_STEP_MINUTES;
      if (btnDown.clicked && setMinutes > TIMER_MIN_MINUTES)
        setMinutes -= TIMER_STEP_MINUTES;
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
      if (btnDown.clicked) timerState = STATE_IDLE;  // cancel session
      break;

    case STATE_DONE:
      if (btnPress.clicked || btnUp.clicked || btnDown.clicked)
        timerState = STATE_IDLE;
      break;
  }

  if (timerState == STATE_DONE &&
      (millis() - doneSinceMs) > DONE_AUTO_DISMISS_MS) {
    timerState = STATE_IDLE;
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

void drawScreen() {
  char big[16];
  const char* title = "";
  const char* hint = "";

  switch (timerState) {
    case STATE_IDLE:
      title = "Ready";
      snprintf(big, sizeof(big), "%u min", setMinutes);
      hint = "click=start  ^v=time";
      break;
    case STATE_RUNNING: {
      title = "Focus";
      long s = remainingMs / 1000;
      snprintf(big, sizeof(big), "%02ld:%02ld", s / 60, s % 60);
      hint = "click=pause";
      break;
    }
    case STATE_PAUSED: {
      title = "Paused";
      long s = remainingMs / 1000;
      snprintf(big, sizeof(big), "%02ld:%02ld", s / 60, s % 60);
      hint = "click=resume  v=stop";
      break;
    }
    case STATE_DONE:
      title = "Done!";
      snprintf(big, sizeof(big), "00:00");
      hint = "click=dismiss";
      break;
  }

  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 10, title);
  const char* ovr = overrideLabel();
  u8g2.drawStr(128 - u8g2.getStrWidth(ovr), 10, ovr);

  u8g2.setFont(u8g2_font_logisoso24_tr);
  u8g2.drawStr((128 - u8g2.getStrWidth(big)) / 2, 44, big);

  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 62, hint);

  u8g2.sendBuffer();
}

// ======================= Wi-Fi settings page ========================

String settingsPage(const String& notice) {
  String html =
    "<!DOCTYPE html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>FocusDock</title>"
    "<style>body{font-family:sans-serif;max-width:22em;margin:2em auto;padding:0 1em}"
    "label{display:block;margin:1em 0 .2em}input[type=number]{width:6em}"
    "button{margin-top:1.2em;padding:.5em 1.5em}.ok{color:green}</style></head><body>"
    "<h2>FocusDock settings</h2>";
  if (notice.length()) html += "<p class=ok>" + notice + "</p>";
  html += "<form method=POST action=/save>";
  html += "<label>LED brightness (1-255)</label><input type=number name=bright min=1 max=255 value=" + String(settings.brightness) + ">";
  html += "<label>Default focus minutes (" + String(TIMER_MIN_MINUTES) + "-" + String(TIMER_MAX_MINUTES) + ")</label>"
          "<input type=number name=minutes min=" + String(TIMER_MIN_MINUTES) + " max=" + String(TIMER_MAX_MINUTES) + " value=" + String(settings.focusMinutes) + ">";
  html += "<label><input type=checkbox name=flip value=1 " + String(settings.flipEnabled ? "checked" : "") + "> Auto-flip display</label>";
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

  saveSettings();
  strip.setBrightness(settings.brightness);
  if (timerState == STATE_IDLE) setMinutes = settings.focusMinutes;
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
  readOrientation();
  updateLeds();
  server.handleClient();

  static unsigned long lastDrawMs = 0;
  if (millis() - lastDrawMs >= DISPLAY_REDRAW_MS) {
    lastDrawMs = millis();
    drawScreen();
  }

  delay(LOOP_DELAY_MS);
}
