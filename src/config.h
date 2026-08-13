// Every tunable in the firmware - pins, colors, defaults, thresholds.
// This is the one place to re-map hardware; nothing below it hardcodes a pin.
#pragma once

// ---- pins / I2C ----
#define PIN_I2C_SDA 8
#define PIN_I2C_SCL 9
#define PIN_LED_DATA 16
#define PIN_BTN_F 4 // physical "F" -> logical LEFT
#define PIN_BTN_B 5 // physical "B" -> logical RIGHT
#define PIN_BTN_L 6 // physical "L" -> logical DOWN
#define PIN_BTN_R 7 // physical "R" -> logical UP
#define PIN_BTN_PRESS 15
#define IMU_I2C_ADDR 0x68 // not BMI160_I2C_ADDR - the driver library owns that name

// ---- LEDs / colors ----
#define NUM_LEDS 10              // 10 LEDs = one per 10% of the timer
#define LED_BRIGHTNESS 60        // 0-255; capped low on purpose (power budget)
#define COLOR_AVAILABLE 0x00FF00 // green = free
#define COLOR_BUSY 0xFF0000      // red   = busy
#define COLOR_PROGRESS 0xFFA500  // amber = timer running
#define COLOR_DONE 0x00FF00      // green = timer done, blinking
#define COLOR_OFF 0x000000
#define DONE_BLINK_MS 500

// ---- pomodoro ----
#define COLOR_POMO_WORK 0xFF0000        // red   = work phase, do not disturb
#define COLOR_POMO_SHORT_BREAK 0x00FF00 // green = short break
#define COLOR_POMO_LONG_BREAK 0x0080FF  // blue  = long break
#define DEFAULT_POMO_WORK_MIN 25
#define DEFAULT_POMO_SHORT_MIN 5
#define DEFAULT_POMO_LONG_MIN 15
#define DEFAULT_POMO_SESSIONS 4         // work sessions before a long break
#define POMO_PHASE_TRANSITION_MS 3000   // blink window before auto-continuing

// ---- buttons ----
#define BTN_DEBOUNCE_MS 30
#define LONG_PRESS_MS 800 // hold this long anywhere -> back to menu

// ---- timer defaults ----
#define DEFAULT_HOURS 0
#define DEFAULT_MINUTES 25 // classic pomodoro length
#define DEFAULT_SECONDS 0

// ---- orientation ----
#define FLIP_THRESHOLD 0.5f  // g; accel-x past +/- this commits a state
#define FLIP_DEBOUNCE_MS 350 // must hold past threshold this long
#define EMA_ALPHA 0.2f       // accel smoothing, 0..1 (higher = jumpier)

// ---- loop / display ----
#define LOOP_DELAY_MS 10
#define DISPLAY_REDRAW_MS 100
#define PROGRESS_CHARS 10 // width of the on-screen timer bar, in characters
