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
phone), USB-C power breakout (connector only — no dual-power diode needed,
the device is USB-only), 74AHCT125 level shifter, 1000 µF capacitor — exact
specs for the last two are in [Level shifter & capacitor](#level-shifter--capacitor---exact-specs)
below (see [Roadmap in the README](../README.md#roadmap) for the rest).

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
  do, that's the supply, not the code — move strip VDD to the `5Vin` pin.
- One nice side effect of 3.3V: the data line matches the ESP32's 3.3V logic,
  so no level shifter is needed *yet*. At 5V VDD the 3.3V data signal is
  marginal — add the 74AHCT125 level shifter before scaling up.
- Budget: a WS2812B draws up to **60 mA at full white**, but a status color at
  our capped brightness is more like **8-10 mA**. The ≤10-LED prototype is fine
  on USB power. The full 20-30 LED ring needs a **5V/3A supply**, the level
  shifter, and a **1000 µF capacitor** across the strip's power pins.
- The device is **USB-only, permanently** — no battery, so no diode is needed
  to combine power sources safely. That also means the LED strip and the
  ESP32 share one supply rail: a sudden LED current spike (several LEDs
  switching at once) can sag that shared rail enough to brown out the board.
  The 1000 µF cap above isn't just for the full ring — it's what prevents
  mid-session resets on the 5V strip at any LED count, so treat it as needed
  as soon as VDD moves off 3.3V, not deferred until scaling up.
- Firmware caps brightness in software (`LED_BRIGHTNESS` in `src/config.h`)
  so the strip can never draw enough to brown out the board.

## Level shifter & capacitor — exact specs

Both parts sit on the LED strip's power/data lines and become necessary the
moment strip VDD moves off 3.3V onto 5V (see Power notes above) — not
deferred until the full 20-30 LED ring.

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
