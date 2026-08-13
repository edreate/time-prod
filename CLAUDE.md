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
make flash ENV=test-peripherals PORT=...  # hardware check firmware
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
| `time-prod-app` (default) | `src/main.cpp` (+ the `src/hw/` headers it includes) |
| `test-peripherals` | `src/test_peripherals.cpp` — I2C scan + display + IMU readout |

`build_src_filter` is an **allowlist of `.cpp` files**: one it doesn't match is
silently not compiled, and one matched by two envs breaks both with duplicate
`setup()`/`loop()`. So **a new `.cpp` in `src/` must be added to a
`build_src_filter`.** The `src/hw/` modules are header-only and need no entry —
they compile as part of whichever `.cpp` includes them. New test firmwares get
a new `[env:...]` block, not an `#ifdef`.

`test_peripherals.cpp` deliberately talks to the raw libraries instead of
`src/hw/` — it is the bring-up sketch you reach for when the modules
themselves are suspect.

## Firmware structure

Hardware sits behind small modules in `src/hw/`; `main.cpp` is the app alone
and never touches a driver library.

| File | Owns |
|---|---|
| `src/config.h` | every tunable — pins, colors, defaults, thresholds |
| `src/hw/display.h` | the U8g2 panel: text primitives, `displaySetFlipped()` |
| `src/hw/leds.h` | the WS2812B strip: `ledsSetAll()`, `ledsSetProgress()` |
| `src/hw/imu.h` | the BMI160: returns an `Orientation`, never a raw accel |
| `src/hw/buttons.h` | debounce + the physical→logical map: returns `ButtonEvents` |
| `src/main.cpp` | state enums, timer, input handling, screens, `setup()`/`loop()` |

- **Header-only modules.** No `.cpp` files — each `src/hw/*.h` holds its driver
  object and state as `static` and its functions as `inline`. The toolchain is
  `gnu++11`, so `inline` variables are unavailable; `static` is what makes this
  work. **Include each hardware header from exactly one `.cpp`** — a second
  includer silently gets its own copy of the driver and its state.
- **Plain prefixed free functions** (`displayBegin()`, `imuOrientation()`), no
  namespaces or classes.
- **`src/config.h`** — every tunable is a `#define` here. Put new tunables in
  it; the docs point readers to it as the one place to re-map pins. Don't reuse
  a driver library's macro name (`IMU_I2C_ADDR`, not `BMI160_I2C_ADDR`) — which
  definition wins would then depend on include order.
- **Adding a sensor** = a new `src/hw/*.h` exposing a plain value, plus a
  `begin()` in `setup()` and a line in `loop()`. Keep the math inside the
  module — main should receive a decision, not a reading.
- **Cooperative loop, no RTOS tasks.** `loop()` runs `handleInput(buttonsPoll())
  → tickTimer() → applyOrientation() → updateLeds()`, throttles redraw to
  `DISPLAY_REDRAW_MS`, then yields with a single `delay(LOOP_DELAY_MS)`. All
  timing is `millis()` deltas. Anything blocking stalls button response and
  the timer tick alike.
- **Plain enum state machines** (`Program`, `TimerState`, `Field`,
  `Availability`) written from `handleInput()`; rendering and LED output derive
  from that state. Keep input and rendering separate.
- **`ButtonEvents`** flags are one-shots valid for exactly one loop pass — poll
  once per iteration and pass the struct around.
- **Long-press the center click returns to the menu from any screen**, so no
  direction button is spent on back.

### Button mapping: physical ≠ logical

The 5-way module sits rotated relative to the display. `docs/HARDWARE.md`
names pins by **silkscreen**; `buttonsPoll()` remaps them once, so the rest of
the firmware only ever sees logical directions:

```
GPIO4  physical F -> logical LEFT     GPIO6  physical L -> logical DOWN
GPIO5  physical B -> logical RIGHT    GPIO7  physical R -> logical UP
GPIO15 center click
```

Write UI logic in logical terms. The remap is fixed at the wires — it does
**not** follow the IMU flip.

### Orientation auto-flip

`imuOrientation()` (in `src/hw/imu.h`) smooths one accel axis with an EMA
(`EMA_ALPHA`), applies a `±FLIP_THRESHOLD` hysteresis dead zone (inside it:
keep last state), and requires the candidate to hold `FLIP_DEBOUNCE_MS` before
committing. All three guards exist to stop flicker when the device is handled
— don't simplify them away.

The IMU does **not** rotate the display itself. `applyOrientation()` in
`main.cpp` compares the returned value against the last one shown and calls
`displaySetFlipped()` on a change — the sensor and the panel stay independent.

IMU init failure is non-fatal: `imuBegin()` returns false, `imuOrientation()`
keeps returning the last state, and the firmware runs with auto-flip disabled.
Keep new hardware optional the same way.

## Not built yet

The Wi-Fi settings page and settings persistence were removed on this branch
(commit `ba297e1 "simplified"`) and are back on the README roadmap. Commit
`7c3a6c0` has a working implementation of both (`git show 7c3a6c0:src/main.cpp`)
if they get rebuilt — that is also where the Info screen lives, which is pure
Wi-Fi (SSID / AP IP / MAC) and only makes sense alongside them.

Pomodoro was removed by the same commit but has since been restored onto the
module structure, with its durations as `config.h` defines rather than
persisted settings.
