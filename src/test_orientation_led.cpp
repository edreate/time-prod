// Test 2: flips the display 180 degrees AND changes the LED strip color
// based on IMU orientation.
//
// The device only ever has two valid mounting states (clipped to the top or
// bottom edge of the monitor), not continuous tilt - so this is a two-state
// classifier, not full attitude estimation. A single accel axis with
// hysteresis + debounce is enough:
//   ax > +FLIP_THRESHOLD  -> normal orientation
//   ax < -FLIP_THRESHOLD  -> flipped 180
//   in between            -> dead zone, keep last known state
// Hysteresis stops noise near the crossover from flickering the display;
// debounce (DEBOUNCE_MS) stops a brief bump or mid-handling tilt from
// triggering a flip before the device has actually settled.
//
// Axis choice (ax) and threshold (~1g) are from measurements on the real
// hardware: ax reads ~+1 mounted vertically, so ~-1 when flipped. Re-check
// this if the IMU's orientation inside the enclosure changes.
//
// LED strip is currently powered from the 3.3V rail (out of spec for
// WS2812B, which wants 5V) - see Req-Design.md power note. Colors may read
// dim/off; that's the 3.3V supply, not this code. Cyan/magenta were picked
// instead of red/green/yellow so this test can't be confused with the
// separate availability-status LED feature.

#include <Wire.h>
#include <U8g2lib.h>
#include <DFRobot_BMI160.h>
#include <Adafruit_NeoPixel.h>

#define BMI160_ADDR 0x68
#define FLIP_THRESHOLD 0.5f   // g; dead zone is -0.5..+0.5
#define DEBOUNCE_MS 350       // must hold past threshold this long to commit
#define EMA_ALPHA 0.2f        // smoothing on the raw accel reading

#define LED_PIN 16
#define NUM_LEDS 8            // adjust to however many are actually wired
#define LED_BRIGHTNESS 60     // 0-255, capped well below max per README power note

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
DFRobot_BMI160 bmi160;
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

enum Orientation { ORIENT_NORMAL, ORIENT_FLIPPED };

bool imuOk = false;
float axFiltered = 0.0f;
Orientation currentOrientation = ORIENT_NORMAL;
Orientation candidateOrientation = ORIENT_NORMAL;
unsigned long candidateSince = 0;

void applyOrientation(Orientation o) {
  u8g2.setDisplayRotation(o == ORIENT_NORMAL ? U8G2_R0 : U8G2_R2);

  uint32_t color = (o == ORIENT_NORMAL) ? strip.Color(0, 255, 255)   // cyan
                                         : strip.Color(255, 0, 255); // magenta
  for (int i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, color);
  }
  strip.show();
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Wire.begin();

  u8g2.begin();
  u8g2.setFont(u8g2_font_6x10_tr);

  strip.begin();
  strip.setBrightness(LED_BRIGHTNESS);
  strip.show(); // all off until applyOrientation() sets the initial color

  applyOrientation(currentOrientation);

  if (bmi160.softReset() != BMI160_OK) {
    Serial.println("IMU: soft reset failed");
  } else if (bmi160.I2cInit(BMI160_ADDR) != BMI160_OK) {
    Serial.println("IMU: init failed");
  } else {
    imuOk = true;
    Serial.println("IMU: init OK");
  }
}

void loop() {
  if (imuOk) {
    int16_t accelGyro[6] = {0};
    if (bmi160.getAccelGyroData(accelGyro) == 0) {
      float axRaw = accelGyro[3] / 16384.0f;
      axFiltered = axFiltered * (1.0f - EMA_ALPHA) + axRaw * EMA_ALPHA;

      bool instantValid = true;
      Orientation instant = currentOrientation;
      if (axFiltered > FLIP_THRESHOLD) {
        instant = ORIENT_NORMAL;
      } else if (axFiltered < -FLIP_THRESHOLD) {
        instant = ORIENT_FLIPPED;
      } else {
        instantValid = false; // dead zone - don't touch the candidate
      }

      if (instantValid) {
        if (instant != candidateOrientation) {
          candidateOrientation = instant;
          candidateSince = millis();
        }
        if (candidateOrientation != currentOrientation &&
            (millis() - candidateSince) > DEBOUNCE_MS) {
          currentOrientation = candidateOrientation;
          applyOrientation(currentOrientation);
          Serial.println(currentOrientation == ORIENT_NORMAL
                              ? "-> NORMAL"
                              : "-> FLIPPED");
        }
      }

      Serial.printf("ax_raw=%.2f ax_filt=%.2f state=%s\n", axRaw, axFiltered,
                    currentOrientation == ORIENT_NORMAL ? "NORMAL" : "FLIPPED");
    }
  }

  u8g2.clearBuffer();
  u8g2.drawStr(0, 12, currentOrientation == ORIENT_NORMAL ? "NORMAL" : "FLIPPED");
  u8g2.drawStr(0, 28, currentOrientation == ORIENT_NORMAL ? "LED: cyan" : "LED: magenta");
  char line[24];
  snprintf(line, sizeof(line), "ax %.2f", axFiltered);
  u8g2.drawStr(0, 44, line);
  u8g2.sendBuffer();

  delay(100);
}
