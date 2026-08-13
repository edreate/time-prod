# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

FocusDock — ESP32-S3 firmware (PlatformIO + Arduino) for a desk
focus/availability device: OLED menu, H:M:S focus timer, WS2812B status
light, IMU-driven display auto-flip. Hardware-only project: no host test
suite, no simulator, no linter. Verification means flashing a board and
reading the serial log at 115200 baud.

Wiring, BOM, and power budget live in [`docs/HARDWARE.md`](docs/HARDWARE.md);
the concepts behind them in [`docs/ELECTRONICS.md`](docs/ELECTRONICS.md).

## Build / flash

```
make ports                          # find the board's /dev/cu.* name
make build
make flash PORT=/dev/cu.usbmodemXXXX    # upload, then monitor
make flash ENV=test-bringup PORT=...    # hardware check firmware
```

`ENV` defaults to `time-prod-app`. `make setup` (PlatformIO install +
toolchain, ~2 min) runs automatically on first build. macOS renames
`usbmodem*` across reconnects — re-run `make ports` if an upload can't find
the board.

## Environments = firmware variants

`platformio.ini` has a shared `[env]` (board, 16MB partitions, lib deps:
U8g2, DFRobot_BMI160, Adafruit NeoPixel) plus one section per firmware, each
selecting **exactly one** `.cpp` via `build_src_filter`:

| ENV | Source |
|---|---|
| `time-prod-app` (default) | `src/main.cpp` — the real firmware |
| `test-bringup` | `src/test_bringup.cpp` — I2C scan + display + IMU readout |

PlatformIO otherwise compiles every `.cpp` in `src/`, and each defines its
own `setup()`/`loop()` — so **a new file in `src/` must be added to a
`build_src_filter`, or every env fails with duplicate symbols.** New test
firmwares get a new `[env:...]` block, not an `#ifdef`.

## Firmware structure (`src/main.cpp`)

Single translation unit: header comment → **CONFIG block** → state enums →
subsystem sections (LEDs, timer, buttons, orientation, display) →
`setup()`/`loop()`.

- **CONFIG block** — every tunable (pins, colors, defaults, thresholds) is a
  `#define` in one marked block at the top. Put new tunables there; the docs
  point readers to it as the one place to re-map pins.
- **Cooperative loop, no RTOS tasks.** `loop()` runs `handleButtons() →
  tickTimer() → readOrientation() → updateLeds()`, throttles redraw to
  `DISPLAY_REDRAW_MS`, then yields with a single `delay(LOOP_DELAY_MS)`. All
  timing is `millis()` deltas. Anything blocking stalls button response and
  the timer tick alike.
- **Plain enum state machines** (`Program`, `TimerState`, `Field`,
  `Availability`, `Orientation`) written from `handleButtons()`; rendering and
  LED output derive from that state. Keep input and rendering separate.
- **`Button`** debounces and exposes `clicked` / `longPressed` as one-shot
  flags valid for exactly one loop pass — read them once per iteration.
- **Long-press the center click returns to the menu from any screen**, so no
  direction button is spent on back.

### Button mapping: physical ≠ logical

The 5-way module sits rotated relative to the display. `docs/HARDWARE.md`
names pins by **silkscreen**; the firmware remaps them once at the top of
`handleButtons()`:

```
GPIO4  physical F -> logical LEFT     GPIO6  physical L -> logical DOWN
GPIO5  physical B -> logical RIGHT    GPIO7  physical R -> logical UP
GPIO15 center click
```

Write UI logic in logical terms. The remap is fixed at the wires — it does
**not** follow the IMU flip.

### Orientation auto-flip

`readOrientation()` smooths one accel axis with an EMA (`EMA_ALPHA`), applies
a `±FLIP_THRESHOLD` hysteresis dead zone (inside it: keep last state), and
requires the candidate to hold `FLIP_DEBOUNCE_MS` before committing
`setDisplayRotation()`. All three guards exist to stop flicker when the
device is handled — don't simplify them away.

IMU init failure is non-fatal: `imuOk` gates the read and the firmware runs
with auto-flip disabled. Keep new hardware optional the same way.

## Not built yet

Pomodoro, the Wi-Fi settings page, and settings persistence were removed on
this branch and are back on the README roadmap. An earlier commit has a
working implementation of all three (`git show HEAD:src/main.cpp`) if they
get rebuilt.
