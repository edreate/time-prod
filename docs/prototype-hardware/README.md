# Prototype-stage hardware evaluation

Notes on parts being considered to move the build off the breadboard. Written
to be critical, not a sales pitch — see [links.txt](links.txt) for the source
listings.

## Current baseline (what these parts are being compared against)

- Board: **ESP32-S3 WROOM-1 N16R8** (`board = esp32-s3-devkitc-1` in
  `platformio.ini`), dual-core Xtensa LX7, 16 MB flash, 8 MB PSRAM (unused by
  the firmware today), 3.3V logic throughout, no level shifters anywhere yet.
- Input: 5-way switch on GPIO4/5/6/7/15, internal pull-ups, no external
  resistors — see `src/hardware/buttons.h` and
  [HARDWARE.md](../HARDWARE.md).
- The README roadmap already lists *"Evaluate rotary encoder vs. 5-way button
  for the final feel"* under Hardware — so an encoder is an anticipated
  direction, not a departure.

## Part 1 — ESP32-C3 SuperMini

**Verdict: not a drop-in. This is an MCU-family swap, not a smaller cable.**
Framing it as "smaller components to get off the breadboard" undersells what
changes if you actually adopt it.

### Voltage — fine, matches what you already do

- Board accepts 3.3–6V on the `5V` pin through its own regulator, same
  pattern as the S3 devkit's USB-5V-in → 3.3V-out. GPIO logic is 3.3V, same
  as your current design.
- **No level shifter needed** for I2C, the LED data line, or buttons — you're
  already running everything at 3.3V by choice (see the "Decisions locked for
  the prototype" note in HARDWARE.md), and this board doesn't change that.

### What actually changes — and it's a lot

| | ESP32-S3 (current) | ESP32-C3 SuperMini |
|---|---|---|
| CPU | Dual-core Xtensa LX7 @ 240 MHz | Single-core RISC-V @ 160 MHz |
| Flash | 16 MB | 4 MB (`ESP32C3FN4`) |
| PSRAM | 8 MB (unused today, but there) | None |
| Toolchain identity | `board = esp32-s3-devkitc-1` | Different `board=` id entirely |

`platformio.ini` currently hardcodes `board_upload.flash_size = 16MB` and
`board_build.partitions = default_16MB.csv`. Neither applies to a 4MB part —
this isn't a wiring change, it's a second `[env:]` block (or a full board
swap) with its own partition table, and every existing pin-map assumption in
`config.h`/`HARDWARE.md` gets re-derived from scratch.

**GPIO budget is tight.** The C3 SuperMini breaks out roughly 11 usable
GPIOs (some boards vary — verify against your exact unit's silkscreen before
wiring). Your current design alone needs 8: 2 for I2C, 1 for LED data, 5 for
the button. That leaves little headroom for the roadmap's presence sensor,
and none for the S3's PSRAM-backed headroom you aren't using but could.

**Strapping pins are a real trap here.** GPIO2, GPIO8, and GPIO9 are
ESP32-C3 boot-mode strapping pins. GPIO9 in particular is usually already
wired to the onboard BOOT button on these modules — pulling it low at reset
selects download mode. Wiring a button or encoder line onto any of these
three (which your buttons already do by design — "pressed" pulls a pin to
GND) risks a device that won't boot normally if that line happens to be held
at power-on. The S3 devkit doesn't have this landmine on the pins you're
using today; a C3 layout would need to route around GPIO2/8/9 deliberately.
This needs verifying against the specific board's schematic before any
wiring — don't take pin numbers from memory (mine or otherwise) as final.

**Library risk is probably low but unverified.** U8g2 and the I2C-based
DFRobot_BMI160 driver are architecture-agnostic and should build fine for
RISC-V. Adafruit_NeoPixel leans on the RMT peripheral, which exists on C3 but
with fewer channels than S3 — a single strip should be unaffected, but this
hasn't been tested against this codebase.

### Tension with your own stated goal

You said the prototype should carry into production with minimal changes.
Swapping MCU families works directly against that — it's the single biggest
possible hardware change short of a new sensor suite. If the goal is
literally *"smaller board, same guts"*, there is an **ESP32-S3 SuperMini** —
same chip family, comparable footprint to the C3 board, no partition/pin
rework required. That's the part that actually satisfies "smaller, but
stable," if that's what's being optimized for. If C3's price/size is worth
the RISC-V rewrite on its own merits, that's a legitimate call — just make it
knowingly, not as a side effect of a "smaller cables" pass.

**Recommendation:** don't wire this in blind. Either (a) confirm you actually
want a chip-family change and spend a short spike getting a bare
`blink + I2C scan` build running on the C3 before committing any wiring, or
(b) default to an S3 SuperMini / stay on the current WROOM-1 and look
elsewhere for "smaller" (e.g. a smaller display or connector, not a smaller
MCU).

## Part 2 — KY-040 rotary encoder module

**Verdict: usable, but wire it correctly and expect firmware work — this is
not a drop-in replacement for the 5-way switch as-is.**

### Voltage — one real hazard, easy to avoid

The listing says "Working Voltage: 5V" and its own Arduino wiring example
ties `+` to `5V`. But its own Raspberry Pi wiring example ties the same pin
to **3.3V** — because CLK/DT/SW are just mechanical switch contacts pulled up
to whatever `+` is fed (the board's onboard resistors, R1/R2/R3, are exactly
that pull-up network — visible in the product photo). There's no logic IC on
this board translating levels; `+` sets the HIGH voltage directly.

- **Wire `+` to your 3.3V rail, not `5V`.** If you follow the module's own
  Arduino-style default and feed it 5V, CLK/DT/SW idle at 5V and that goes
  straight into an ESP32 GPIO, whose absolute max input is ~3.6V. That's
  over-voltage on every read while the encoder is idle, not just a
  bad-immediately fault — the kind of thing that degrades a GPIO over time
  rather than failing loud.
- **No level shifter needed either way** — once `+` is on 3.3V, the output is
  already 3.3V logic. A level shifter would be the wrong fix for the wrong
  problem here; the fix is just which rail you pick.

### Firmware — this is the part that isn't just wiring

`src/software/timer.h` currently uses a genuine 2-axis input: LEFT/RIGHT
selects the H/M/S field, UP/DOWN adjusts the selected field's value, click
confirms. A rotary encoder gives you exactly **one axis (CW/CCW) + one
click**. Those don't map 1:1 — you can't wire this in and keep
`timerInput()` as-is. Adopting it means redesigning that interaction (e.g.
click cycles the field, rotation adjusts it) before the encoder is actually
usable in `PROGRAM_TIMER`, not just before it compiles.

Also, `buttons.h`'s `Button` struct debounces a level (pressed/released) —
that reused directly for `SW`. CLK/DT need quadrature decoding (compare
CLK/DT phase per edge, ideally on a `CHANGE` interrupt so fast spins aren't
missed by the polling loop's `LOOP_DELAY_MS`), which is new code, not a
second instance of the existing `Button`.

### GPIO — the one place this helps the C3 discussion above

3 pins (CLK, DT, SW) vs. 5 today (F/B/L/R + click). If the C3 SuperMini is
pursued despite the concerns above, replacing the 5-way with this encoder is
what actually makes the GPIO budget work — worth treating as a package
decision (both changes together), not two independent purchases.

### Minor

- Working voltage note aside, this is a cheap mechanical part (no datasheet
  claims about lifespan/detents beyond the generic listing) — fine for
  prototyping, but if "stable component, ships basically unchanged to
  production" is the bar, get click/rotation-count endurance before locking
  it in for a device meant to be operated multiple times a day.
- The "5x push button caps" in the kit are cosmetic spares, no functional
  relevance.

## Board selected: Tenstar ESP32-S3 Zero

Verdict: **good pick — confirmed compatible with the current pin map,
3.3V logic throughout, no level shifter.** Real specs (not the generic
"S3 SuperMini" guess above), per [espboards.dev](https://www.espboards.dev/esp32/esp32-s3-zero/)
and [Waveshare's docs](https://docs.waveshare.com/ESP32-S3-Zero) for the same
reference design (Tenstar's is a clone of it):

- Chip: **ESP32-S3FH4R2** — dual-core Xtensa LX7 @ 240 MHz, **4 MB flash, 2 MB
  quad PSRAM**, Wi-Fi + BLE 5.0. Same silicon family as the WROOM-1 N16R8 you
  run today, just a smaller flash/PSRAM SKU.
  ([source](https://manuals.plus/ae/1005006862681936))
- **24 GPIO broken out** across 27 pins (2.54mm pitch) + 5V/GND/3V3. Native
  USB-C (no USB-UART bridge chip to go flaky). Onboard WS2812 RGB status LED
  (separate from your external strip — doesn't consume a project pin).
  Castellated (half-hole) module edges, so the same module can be reflow-
  soldered straight onto a custom PCB later — a real plus for "prototype
  becomes production."
- Regulator: ME6217C33M5G, 800 mA — comfortable headroom for OLED + IMU +
  the LED strip's capped brightness draw.

### Pin-map check against `src/config.h`

| Signal | Pin | On this board? |
|---|---|---|
| I2C SDA/SCL | GPIO8/9 | Yes — plain GPIO, not strapping/reserved |
| LED data | GPIO16 | Yes |
| Buttons F/B/L/R/press | GPIO4/5/6/7/15 | Yes |

None of your 8 pins fall in this board's problem ranges: **GPIO0/3/45/46**
are S3 strapping pins (boot mode) — avoid for new signals, but you don't use
them; **GPIO19/20** are USB-JTAG; **GPIO26–32** are flash/PSRAM; **GPIO33–37**
aren't broken out on this board at all (reserved for octal PSRAM the 2MB
quad chip doesn't use). Your current wiring should map straight across —
this is the "unmodified config.h" case the earlier recommendation was hoping
for, now confirmed against the real part number instead of a generic
"SuperMini" guess.

### What still needs changing (config, not code)

`platformio.ini`'s `[env]` block currently assumes the N16R8 module — none of
this applies to the Zero's FH4R2:

- `board_upload.flash_size = 16MB` → **4MB**
- `board_build.partitions = default_16MB.csv` → a 4MB-sized partition table
  (e.g. PlatformIO's default 4MB scheme — pick one with room for OTA later
  if that roadmap item still matters, or the smallest no-OTA table if not)
- PSRAM mode: the N16R8 is **octal** PSRAM; the FH4R2 on this board is
  **quad**. If `board_build.arduino.memory_type` (or equivalent PSRAM build
  flag) is set anywhere for octal, it needs to move to quad/`qio_qspi` —
  wrong mode here is a classic silent-PSRAM-not-detected bug, worth checking
  even though nothing in `platformio.ini` sets this explicitly today (so it's
  currently using whatever `esp32-s3-devkitc-1`'s default assumes).
- `board = esp32-s3-devkitc-1` will very likely still work as the PlatformIO
  board id (it's a generic S3 devkit definition, not N16R8-specific) — worth
  a first-boot smoke test (`test-peripherals` env) before assuming it, same
  as any new board.

Nothing in `src/` should need to change — this is the scenario the earlier
"same MCU family" recommendation was aiming for, and it now checks out
against the real chip.

## Power budget (locked decisions)

Deployment target confirmed: **USB-only, permanently, off whatever USB 3.0
port is on hand** (computer, laptop, or monitor) — no battery, no dedicated
wall charger assumed. That fixes the design point at USB 3.0's guaranteed
900 mA, not the 500 mA USB 2.0 floor or a PD charger's 2-3A.

**Budget at 10 LEDs, full white:**

| | |
|---|---|
| 10× WS2812B @ full white (60mA/LED) | 600 mA |
| ESP32-S3 active, Wi-Fi off | ~80-120 mA |
| 1.3" OLED (SH1106, I2C) | ~20-30 mA |
| BMI160 IMU + buttons | ~1-2 mA |
| **Total** | **~700-750 mA** |

Against USB 3.0's 900 mA, that's ~150-200 mA headroom (~17-22%) — workable
but not generous, so the mitigations below aren't optional:

- **Wi-Fi defaults OFF, menu-toggle to enable.** Wi-Fi active/TX current
  (spikes into the hundreds of mA) isn't in the budget above on purpose —
  it's opt-in per session, not a background cost paid by default. Applies to
  both the Wi-Fi settings-hotspot and Wi-Fi-client roadmap items.
- **Max-brightness LED mode is a menu setting, not the default.** The
  existing `LED_BRIGHTNESS` cap in `config.h` is what keeps the device
  power-safe on *any* USB port including older/cheap ones; true full-white
  becomes something the user opts into on a port they've confirmed can take
  it, not an assumption baked into firmware.
- **The 1000 µF bulk cap and 74AHCT125 level shifter are both required, not
  optional**, once LED VDD moves to 5V — exact part specs, pinout, and
  wiring now in [HARDWARE.md](../HARDWARE.md#level-shifter--capacitor---exact-specs).
  With only ~17-22% margin against USB 3.0's limit, an unbuffered LED
  current spike is exactly what erases that margin and browns out the
  board — this is load-bearing, not a nice-to-have.
- Not all ports labeled "USB 3.0" actually deliver the full 900 mA — some
  monitor USB hubs under-deliver their spec. Worth spot-checking the actual
  port(s) this will live on with a USB power meter before treating 10 LEDs
  at full white as a settled number.

## Bottom line

| Part / decision | Voltage / level-shifter question | Bigger issue |
|---|---|---|
| ESP32-C3 SuperMini | Fine — 3.3V logic, no shifter needed | Different MCU family: new partition table, tight/strapping-pin-constrained GPIO map, contradicts "minimal changes into production" unless that trade is made deliberately |
| **Tenstar ESP32-S3 Zero (selected)** | Fine — 3.3V logic, no shifter needed | None blocking — same MCU family, current pin map fits without collisions; `platformio.ini` envs already added and build-verified |
| KY-040 encoder | Fine **only if `+` → 3.3V**, not the listing's 5V default | Firmware redesign needed for 2-axis timer field editing; new quadrature-decode code, not a wiring-only swap |
| 1.3" ST7789 SPI TFT (240x240) | Fine — 3.3V logic | Not pursued: backlight is constant-current regardless of content, works against low-power goal; OLED stays |
| WS2812 LED strip on 5V | Needs 74AHCT125 (exact specs above) | 1000 µF cap now required, not deferred — see Power budget above |
| Wi-Fi (when rebuilt) | n/a | Defaults OFF, menu-toggle on — not budgeted into baseline power |

The S3 Zero is a solid choice — confirm with a `test-peripherals` flash
before committing wiring, since "should work" per datasheet isn't the same
as verified on your actual unit. The encoder still needs its own decision
(see above) independent of which board it's wired to.
