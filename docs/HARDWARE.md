# Hardware Guide

Everything about the physical build: what parts we use, how they connect, and
the gotchas we already hit so you don't have to. New to electronics terms like
I2C, pull-up, or level shifter? Read [ELECTRONICS.md](ELECTRONICS.md) first —
it explains each concept in a few lines.

## The prototype today

![Breadboard prototype](breadboard.jpeg)

## Parts (Bill of Materials)

| Part | Exact module | What it does |
|---|---|---|
| ESP32-S3 board | ESP32-S3 WROOM-1 N16R8 devkit | The brain: runs the firmware, provides Wi-Fi |
| Display | 1.3" OLED, 128x64, I2C (4 pins, SH1106 driver) | Shows the timer and menus |
| IMU (motion sensor) | BMI160 breakout (12-pin) | Detects which way up the device is mounted |
| LED strip | WS2812B addressable RGB (SMD-WS2812B_RGB-02-ML) | The red/amber/green status light |
| 5-way button | 5DirKey V1-2 | Up/down/left/right/click input |
| Resistor | 330-470 Ω | Protects the first LED's data input |

Planned but not on the breadboard yet: the **74AHCT125 level shifter** and
**1000 µF capacitor** for the LED strip's 5V power — exact specs and wiring
in [Level shifter & capacitor](#level-shifter--capacitor---exact-specs) below.

## Circuit diagram (breadboard prototype)

```
                          ┌─────────────────────────┐
              USB-C 5V ──►│ ESP32-S3 DevKit         │
                          │                         │
      3.3V rail ◄─────────┤ 3V3            GPIO8 ├──┼──► SDA ──┬───────────┐
      GND rail  ◄─────────┤ GND            GPIO9 ├──┼──► SCL ──┼──┬────────┼──┐
                          │                         │          │  │        │  │
                          │                GPIO16├──┼─[330Ω]─┐ │  │        │  │
                          │                         │        │ │  │        │  │
                          │  GPIO4  ◄── button UP   │        │ │  │        │  │
                          │  GPIO5  ◄── button DOWN │   ┌────▼─┴──┴───┐ ┌──▼──▼─────┐
                          │  GPIO6  ◄── button LEFT │   │ OLED display│ │ IMU BMI160│
                          │  GPIO7  ◄── button RIGHT│   │ VDD ── 3.3V │ │ VIN ─ 3.3V│
                          │  GPIO15 ◄── button CLICK│   │ GND ── GND  │ │ GND ─ GND │
                          └─────────────────────────┘   └─────────────┘ │ CS  ─ 3.3V│
                                     ▲                                  │ SA0 ─ GND │
                                     │ common pin                       └───────────┘
                               5-way button ── GND
                                                        ┌─────────────────────┐
                          LED strip:  DIN ◄─[330Ω]─ GPIO16   WS2812B strip    │
                                      VDD ── 3.3V rail (testing; 5V later)    │
                                      GND ── GND rail   └─────────────────────┘
```

Two power rails on the breadboard:

- **3.3V rail** — fed from the ESP32's `3V3` pin. Powers the display, the IMU,
  and (for now, out of spec) the LED strip.
- **GND rail** — every single part connects here. All grounds must be tied
  together or signals have no reference and nothing works reliably.

## Wiring table

| Component | Pin | Connects to | Why |
|---|---|---|---|
| Display (OLED) | VDD | 3.3V rail | Power |
| | GND | GND rail | |
| | SCK | GPIO9 (I2C SCL) | Shared I2C clock |
| | SDA | GPIO8 (I2C SDA) | Shared I2C data |
| IMU (BMI160) | VIN | 3.3V rail | Power in |
| | 3V3 | **unconnected** | This is the onboard regulator's *output*, not an input — don't feed it |
| | GND | GND rail | |
| | SCL / SDA | GPIO9 / GPIO8 | Same I2C bus as the display |
| | CS | 3.3V rail | **Must be tied high** or the chip switches to SPI mode and stops answering I2C |
| | SA0 | GND | Sets I2C address 0x68 (3.3V would make it 0x69) |
| | OCS, INT1/2, SCX/SDX | unconnected | Aux/interrupt pins we don't use (if I2C is flaky, try tying OCS high or low — floating aux pins are a known culprit) |
| LED strip | VDD | 3.3V rail (**testing**) | Out of spec — WS2812B wants 5V; see power note |
| | GND | GND rail | |
| | DIN | GPIO16 → 330-470 Ω → DIN | Resistor protects the first LED's data input |
| 5-way button | forward (up) | GPIO4 | All button pins use the ESP32's internal pull-ups, |
| | backward (down) | GPIO5 | so pressing a direction pulls the pin to GND — |
| | left | GPIO6 | no external resistors needed |
| | right | GPIO7 | |
| | center click | GPIO15 | |
| | common | GND rail | |

Pin numbers are defined once in [`src/config.h`](../src/config.h) — if you wire
something differently, change it there.

## Power notes

- WS2812B is spec'd for **5V**. We currently run it at 3.3V for bring-up
  convenience: colors may look dim or wrong (blue/green fade first). If they
  do, that's the supply, not the code — move strip VDD to the `5Vin` pin. One
  side effect of the 3.3V bring-up wiring: the data line happens to match the
  ESP32's own 3.3V logic, so no level shifter is needed *yet* — but that ends
  the moment VDD moves to 5V (see below; it's not a "later, once we scale up"
  thing — it's the very next wiring change).
- **Design target: 10 LEDs, powered from a USB 3.0 port** (computer, laptop,
  or monitor — no dedicated charger, no battery; the device is **USB-only,
  permanently**). A WS2812B draws up to **60 mA at full white**; budget at
  10 LEDs full white plus the rest of the board:

  | | |
  |---|---|
  | 10× LED @ full white | 600 mA |
  | ESP32-S3 active, Wi-Fi off | ~80-120 mA |
  | OLED + IMU + buttons | ~25-30 mA |
  | **Total** | **~700-750 mA** |

  Against USB 3.0's guaranteed 900 mA, that's only **~150-200 mA headroom
  (~17-22%)** — workable, but not generous. Not every port labeled "USB 3.0"
  actually delivers the full 900 mA (some monitor hubs under-deliver spec) —
  worth confirming with a USB power meter on the actual port(s) this will
  live on before treating full-white-at-10-LEDs as a settled number.
- An unbuffered LED current spike is exactly what erases that ~150-200mA
  margin and browns out the board mid-session — the LED strip and the ESP32
  share this one supply rail, so a spike on one side sags the other. That's
  what the cap and level shifter below are for.
- Firmware also caps brightness in software (`LED_BRIGHTNESS` in
  `src/config.h`) as a second line of defense — see the
  [README roadmap](../README.md#roadmap) for the planned menu-toggle
  brightness setting (default stays at the safe cap; full brightness becomes
  opt-in, not assumed) and the same default-off pattern for Wi-Fi once it's
  rebuilt.

## Level shifter & capacitor — exact specs

Both parts sit on the LED strip's power/data lines and become necessary the
moment strip VDD moves off 3.3V onto 5V (see Power notes above) — needed now,
at the current 10-LED design, not deferred to a later scale-up.

**Level shifter — 74AHCT125** (quad 3-state buffer; e.g. TI `SN74AHCT125N`,
PDIP-14 for the breadboard, or `74AHCT125D` SOIC-14 for a future PCB). AHCT
specifically, not HC/AHC/HCT's other siblings — its inputs use TTL-level
thresholds, so it reliably reads the ESP32's 3.3V HIGH even though the chip
itself runs on 5V. Only one of its four gates is needed:

| Pin | Signal | Wire to |
|---|---|---|
| 14 | VCC | **5V** rail (same supply as the LED strip — not 3.3V, or the output swing is wrong) |
| 7 | GND | GND rail |
| 1 | 1OE̅ (output enable, active low) | GND — ties it permanently enabled |
| 2 | 1A (input) | GPIO16 (`PIN_LED_DATA`), the existing 3.3V data signal |
| 3 | 1Y (output) | the existing 330-470 Ω resistor → LED strip DIN |
| 4, 10, 13 | 2OE̅/3OE̅/4OE̅ (unused gates) | VCC — disables their outputs |
| 5, 9, 12 | 2A/3A/4A (unused gates) | GND — avoids floating CMOS inputs |

Add a 0.1 µF ceramic capacitor across pins 14 (VCC) and 7 (GND), as close to
the chip as possible — standard IC decoupling, per the TI datasheet.

**Bulk capacitor — 1000 µF, ≥16V, radial electrolytic** (any brand; e.g.
Nichicon UVZ-series, Panasonic EEU-FR-series, or an equivalent generic part —
this is a commodity component, no need to match a specific SKU). Adafruit's
own NeoPixel guidance sets 6.3V as the floor for a 5V rail; 16V gives real
margin on a component that's cheap either way and protects against
transients above nominal 5V.

- **Polarity matters** — it's electrolytic. `+` to the strip's `VDD`, `−`
  (marked with a stripe) to `GND`. Reversed, it can fail and vent.
- **Placement matters** — across the strip's own V+/GND pins, physically at
  the first LED, not back at the ESP32 or the level shifter. That's what
  actually damps the current spike at its source.

## Decisions locked for the prototype

- I2C bus: GPIO8 (SDA) / GPIO9 (SCL), shared by display + IMU.
- IMU CS tied to 3.3V (forces I2C mode). Verify against your breakout's
  silkscreen — some BMI160 boards wire CS differently.
- Display driver assumed SH1106 (typical for 1.3" I2C OLEDs). If the screen is
  blank or garbled, swap the constructor in the code for
  `U8G2_SSD1306_128X64_NONAME_F_HW_I2C`.
