# Prototype hardware — decisions & open points

Status of the move off the breadboard. Decisions below are locked (reflected
in `platformio.ini`, `README.md`, and `docs/HARDWARE.md`); open points are
not — don't order/wire against those until they're resolved. Source listings:
[links.txt](links.txt).

## Decided

| Decision | What | Why |
|---|---|---|
| Board | **Tenstar/Waveshare ESP32-S3 Zero** (`ESP32-S3FH4R2`, 4MB flash / 2MB quad PSRAM) | Same MCU family as the current N16R8 devkit — `src/config.h`'s pin map (GPIO4/5/6/7/8/9/15/16) carries over unmodified, none of it falls on this board's strapping/reserved pins. `platformio.ini` envs (`*-s3zero`) already added and build-verified; **not yet flash-verified on real hardware** — run `test-peripherals-s3zero` first. |
| Power topology | **USB-only, permanently** — no battery, no dedicated charger. Design point: USB 3.0's guaranteed 900mA (not USB 2.0's 500mA, not a PD charger's 2-3A) | Device lives on whatever USB 3.0 port is on hand (computer/laptop/monitor). No diode needed for dual-power (there's only one power source). |
| LED budget | 10 LEDs, full white supported — ~700-750mA total against USB 3.0's 900mA (~17-22% headroom) | See [HARDWARE.md Power notes](../HARDWARE.md#power-notes) for the full breakdown. Margin is real but thin. |
| LED power fix | Strip VDD moves to 5V; **74AHCT125 level shifter + 1000µF cap required now**, not deferred | Thin power margin means an unbuffered LED switching spike is exactly what browns out the board. Exact specs/pinout: [HARDWARE.md](../HARDWARE.md#level-shifter--capacitor---exact-specs). |
| Display | Stays OLED (SH1106, I2C) — a 1.3" ST7789 SPI TFT (240x240) was considered and rejected | TFT backlight draws constant current regardless of content; OLED only lights drawn pixels. Directly opposed to the low-power goal. |
| Wi-Fi (once rebuilt) | Defaults OFF on boot, menu toggle to enable | Its active/TX current isn't in the power budget above by default — see [README roadmap](../../README.md#roadmap). |
| Brightness | Menu-toggle max-brightness setting, off by default | Keeps the device power-safe on any USB port out of the box; full brightness becomes an opt-in for a port confirmed to handle it. |

## Rejected

| Option | Why not |
|---|---|
| ESP32-C3 SuperMini | Different MCU family (RISC-V, no PSRAM, 4MB flash, different partition table) — not a drop-in. Tight GPIO budget (~11 pins) with 3 of them (GPIO2/8/9) sitting on the chip's own boot-strapping pins, one of which is usually already the onboard BOOT button. Contradicted "prototype into production with minimal changes." Not used anywhere in this project. |
| 1.3" ST7789 SPI TFT | See Display row above — backlight power. Also would've needed a `display.h` rewrite (U8g2 doesn't support ST7789) and every screen's hardcoded layout coordinates re-tuned for a 240x240 square panel vs. today's 128x64. |

## Open points

Not decided — resolve before ordering/wiring further:

- **Rotary encoder (KY-040) vs. keeping the 5-way switch.** Still genuinely
  open. If pursued: wire the module's `+` to **3.3V, not 5V** (its own
  Arduino-style wiring example uses 5V, which would put 5V on the ESP32's
  GPIO — its Raspberry Pi example correctly uses 3.3V instead, same board).
  No level shifter needed either way. Bigger blocker: `src/software/timer.h`
  uses genuine 2-axis input (LEFT/RIGHT selects H/M/S field, UP/DOWN adjusts
  it) — an encoder only gives one rotation axis + click, so adopting it means
  redesigning that interaction, not just rewiring. Also needs new
  quadrature-decode code in `buttons.h` (the existing `Button` struct only
  debounces a level, doesn't decode CLK/DT phase).
- **Physical verification of the S3 Zero.** Nothing above has touched real
  hardware yet. First step on arrival: `make flash ENV=test-peripherals-s3zero
  PORT=...` — confirms the pin map, I2C, and PSRAM mode actually work before
  any wiring commitment.
- **Cabling.** Not yet specified:
  - USB cable from the host port to the device — needs to be a real
    power-rated cable (not a thin charge-only one), since ~700-750mA at the
    edge of USB 3.0's budget is exactly where cable resistance/voltage sag
    starts to matter.
  - Not every port labeled "USB 3.0" delivers its full 900mA spec (some
    monitor hubs under-deliver) — spot-check the actual port(s) this will
    live on with a USB power meter before finalizing the 10-LED-at-full-white
    target.
  - Wiring from the S3 Zero's 5V pin to the level shifter + LED strip, and
    from the level shifter's output through the existing 330-470Ω resistor
    to the strip's DIN — breadboard for now; connector choice (JST, screw
    terminal, direct solder) not yet decided for a PCB revision.
- **PSRAM mode in `platformio.ini`.** The S3 Zero's FH4R2 is quad PSRAM vs.
  the N16R8's octal — `board_build.arduino.memory_type = qio_qspi` is already
  set in the `*-s3zero` envs and the build compiles clean, but that's only
  datasheet reasoning until it's confirmed against real boot behavior on
  actual hardware (wrong PSRAM mode is a classic silent-failure bug, not a
  build-time one).
