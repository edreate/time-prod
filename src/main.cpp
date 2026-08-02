// First bring-up test: OLED display + BMI160 IMU only (button and LED strip
// not wired yet). See Req-Design.md for the pin/wiring reference.
//
// I2C bus: SDA=GPIO8, SCL=GPIO9 (ESP32-S3 Arduino core defaults Wire to
// these pins, so no explicit Wire.begin(sda, scl) is needed).
// IMU address: 0x68 (SA0 tied to GND).
// Display: assumed SH1106 driver (typical for 1.3" 128x64 I2C OLEDs). If the
// screen stays blank or shows garbage, swap the constructor below for
// U8G2_SSD1306_128X64_NONAME_F_HW_I2C.

#include <Wire.h>
#include <U8g2lib.h>
#include <DFRobot_BMI160.h>

#define BMI160_ADDR 0x68

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
DFRobot_BMI160 bmi160;

bool imuOk = false;

void scanI2C() {
  Serial.println("Scanning I2C bus...");
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  found device at 0x%02X\n", addr);
      found++;
    }
  }
  if (found == 0) {
    Serial.println("  no I2C devices found - check wiring/power");
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Wire.begin();

  scanI2C();

  u8g2.begin();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.clearBuffer();
  u8g2.drawStr(0, 12, "Display: OK");
  u8g2.sendBuffer();

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
  u8g2.clearBuffer();
  u8g2.drawStr(0, 12, "Display: OK");

  if (imuOk) {
    int16_t accelGyro[6] = {0};
    if (bmi160.getAccelGyroData(accelGyro) == 0) {
      float ax = accelGyro[3] / 16384.0;
      float ay = accelGyro[4] / 16384.0;
      float az = accelGyro[5] / 16384.0;

      Serial.printf("accel g: x=%.2f y=%.2f z=%.2f\n", ax, ay, az);

      char line[24];
      snprintf(line, sizeof(line), "ax %.2f", ax);
      u8g2.drawStr(0, 30, line);
      snprintf(line, sizeof(line), "ay %.2f", ay);
      u8g2.drawStr(0, 44, line);
      snprintf(line, sizeof(line), "az %.2f", az);
      u8g2.drawStr(0, 58, line);
    } else {
      u8g2.drawStr(0, 30, "IMU read failed");
    }
  } else {
    u8g2.drawStr(0, 30, "IMU: not found");
  }

  u8g2.sendBuffer();
  delay(200);
}
