// Orientation sensor: BMI160 accel, smoothed and debounced into a stable state.
// The caller gets an orientation and never sees a raw acceleration.
//
// Header-only: `static` state + `inline` functions, so include it from exactly
// one .cpp (main.cpp). A second includer would get its own copy of the sensor.
#pragma once

#include <Arduino.h>
#include <DFRobot_BMI160.h>

#include "../config.h"

enum Orientation
{
  ORIENT_NORMAL,
  ORIENT_FLIPPED
};

static DFRobot_BMI160 bmi160;
static bool imuOk = false;

static Orientation currentOrientation = ORIENT_NORMAL;
static Orientation candidateOrientation = ORIENT_NORMAL;
static unsigned long candidateSinceMs = 0;
static float axFiltered = 0.0f;

// Returns false if the sensor is absent - the caller keeps running without it.
inline bool imuBegin()
{
  imuOk = bmi160.softReset() == BMI160_OK &&
          bmi160.I2cInit(IMU_I2C_ADDR) == BMI160_OK;
  return imuOk;
}

// Poll each loop. Three guards against flicker while the device is handled: an
// EMA on the raw axis, a dead zone around zero, and a hold before committing.
inline Orientation imuOrientation()
{
  if (!imuOk)
    return currentOrientation;

  int16_t accelGyro[6] = {0};
  if (bmi160.getAccelGyroData(accelGyro) != 0)
    return currentOrientation;

  float axRaw = accelGyro[3] / 16384.0f; // raw counts -> g (at +/-2g range)
  axFiltered = axFiltered * (1.0f - EMA_ALPHA) + axRaw * EMA_ALPHA;

  Orientation instant;
  if (axFiltered > FLIP_THRESHOLD)
    instant = ORIENT_NORMAL;
  else if (axFiltered < -FLIP_THRESHOLD)
    instant = ORIENT_FLIPPED;
  else
    return currentOrientation; // dead zone: keep last known state

  if (instant != candidateOrientation)
  {
    candidateOrientation = instant;
    candidateSinceMs = millis();
  }
  if (candidateOrientation != currentOrientation &&
      (millis() - candidateSinceMs) > FLIP_DEBOUNCE_MS)
  {
    currentOrientation = candidateOrientation;
  }
  return currentOrientation;
}
