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

Planned but not on the breadboard yet: presence sensor (detect a docked
phone), USB-C power breakout, 74AHCT125 level shifter, 1000 µF capacitor,
Schottky diode (see [Roadmap in the README](../README.md#roadmap)).

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

Pin numbers are defined once at the top of [`src/main.cpp`](../src/main.cpp)
(the CONFIG block) — if you wire something differently, change it there.

## Power notes

- WS2812B is spec'd for **5V**. We currently run it at 3.3V for bring-up
  convenience: colors may look dim or wrong (blue/green fade first). If they
  do, that's the supply, not the code — move strip VDD to the `5Vin` pin.
- One nice side effect of 3.3V: the data line matches the ESP32's 3.3V logic,
  so no level shifter is needed *yet*. At 5V VDD the 3.3V data signal is
  marginal — add the 74AHCT125 level shifter before scaling up.
- Budget: a WS2812B draws up to **60 mA at full white**, but a status color at
  our capped brightness is more like **8-10 mA**. The ≤10-LED prototype is fine
  on USB power. The full 20-30 LED ring needs a **5V/3A supply**, the level
  shifter, and a **1000 µF capacitor** across the strip's power pins.
- Firmware caps brightness in software (`DEFAULT_LED_BRIGHTNESS` in the CONFIG
  block) so the strip can never draw enough to brown out the board.

## Decisions locked for the prototype

- Level shifter (74AHCT125) skipped for now — 3.3V data direct, short wires.
  Add it back before scaling LED count or finalizing a board.
- I2C bus: GPIO8 (SDA) / GPIO9 (SCL), shared by display + IMU.
- IMU CS tied to 3.3V (forces I2C mode). Verify against your breakout's
  silkscreen — some BMI160 boards wire CS differently.
- Display driver assumed SH1106 (typical for 1.3" I2C OLEDs). If the screen is
  blank or garbled, swap the constructor in the code for
  `U8G2_SSD1306_128X64_NONAME_F_HW_I2C`.
