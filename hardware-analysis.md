# FocusDock Hardware Analysis — 5 V Rail, Power Budget, Programmability

Analysis of the carrier board in [`hardware/focusdock-pcb/`](hardware/focusdock-pcb/README.md)
against the wiring in [`docs/HARDWARE.md`](docs/HARDWARE.md), answering three
questions:

1. Does the WS2812B strip need a 5 V rail rather than the 3.3 V it runs on today?
2. If so, does it need a *separate* 5 V supply, given the device is USB-powered only?
3. How does the board stay programmable once it is powered that way?

Every number below is from a primary source — Espressif's own schematic, the
WS2812B datasheet, and the diode datasheet — not from a quoted figure.

---

## 1. Current state (verified)

The board as committed:

| Check | Result |
|---|---|
| ERC | **0 errors, 0 warnings** |
| DRC | **0 errors, 0 unconnected pads, 0 footprint errors** |
| Remaining DRC warnings | 4 × `MountingHole_2.7mm` "does not match copy in library" — cosmetic, locally created footprint variants |
| Netlist vs `docs/HARDWARE.md` | matches pin for pin |
| J1/J3 pinout | confirmed twice: measured from Espressif's mechanical DXF, and cross-checked against the official schematic |

Firmware inputs that set the power budget (`src/config.h`):

```
#define NUM_LEDS       10    // 10 LEDs = one per 10% of the timer
#define LED_BRIGHTNESS 60    // 0-255; capped low on purpose (power budget)
```

### What the board does today

`JP1` is a 3-pad solder jumper selecting the LED supply, shipped bridged 1–2 =
**3.3 V**, matching current firmware and docs. The 5 V position routes `J1/21`
(the devkit's `VCC_5V` pin) to the strip.

### Gap found

**The board has JP1's 5 V position but no level shifter.** Selecting 5 V today
produces a data line that is out of spec — see §4. This is the one real defect
this analysis turned up, and it is why the 5 V option is not yet usable as
built.

---

## 2. The devkit's power architecture

From [Espressif's ESP32-S3-DevKitC-1 schematic](https://dl.espressif.com/dl/schematics/SCH_ESP32-S3-DevKitC-1_V1.1_20221130.pdf):

```
  USB-UART port ──VBUS──┐
                        ├──[1N5819HW-7-F]──> VCC_5V ──┬──> J1 pin 21 (header)
  native USB port ─VBUS─┘   (diode-OR, one per port)  └──> SGM2212-3.3 LDO ──> VCC_3V3
```

| Component | Part | Rating |
|---|---|---|
| USB input diodes (×2) | `1N5819HW-7-F` | 40 V, **1 A** avg forward current, Vf ≈ 0.3 V at a few hundred mA |
| 3.3 V regulator | `SGM2212-3.3XKC3G/TR` | feeds the ESP32-S3 module |

Two consequences drive everything below:

- The **1 A diode rating is a hard ceiling** on the 5 V pin, and it is *shared*
  with the LDO powering the ESP32-S3.
- The diode drop means the "5 V" pin is really **~4.7 V** under load. That
  quietly lowers the WS2812B's logic threshold, which matters in §4.

---

## 3. Do we need a 5 V rail? — Yes

`docs/HARDWARE.md` describes 3.3 V operation as "colors may look dim or wrong."
The datasheet is stronger than that.

[WS2812B datasheet](https://cdn-shop.adafruit.com/datasheets/WS2812B.pdf):
supply voltage range **3.5 – 5.3 V**, with electrical characteristics tested at
4.5 – 5.5 V.

**At 3.3 V the part is below its datasheet minimum** — not degraded,
unspecified. So yes, a 5 V rail is the correct design, and JP1 exists for
exactly this reason.

### Do we need a *separate* 5 V supply? — Not yet

USB VBUS is already 5 V, so **no boost converter belongs in this design.** The
only question is whether VBUS reaches the LEDs *through* the devkit or *around*
it. That is a current-budget question:

| Load | Current |
|---|---|
| `1N5819HW` rating (hard ceiling) | 1000 mA |
| ESP32-S3 peak (Wi-Fi TX, drawn through the LDO) | ~350 mA |
| **Headroom at the 5 V pin** | **~500 mA peak, ~300 mA continuous** |
| **10 LEDs @ `LED_BRIGHTNESS 60`, all white** | **~150 mA** ✅ fits |
| Typical status colour at that brightness | ~50 mA ✅ |
| Roadmap 20–30 LED ring, full white | ~1800 mA ❌ **12× over** |

- **Today (10 LEDs):** no separate supply. 150 mA against ~300 mA of headroom.
  Route it through the devkit's 5 V pin — that is what JP1 already does.
- **Roadmap ring:** yes. 1.8 A cannot pass through a 1 A diode. VBUS must reach
  the LEDs without going through the devkit at all.

---

## 4. The real problem is logic level, not current

The WS2812B needs `VIH = 0.7 × VDD`. The ESP32-S3 drives 3.3 V:

| LED VDD | Source | VIH needed | Margin | Verdict |
|---|---|---|---|---|
| 3.3 V | JP1 → 3V3 (today) | 2.31 V | +0.99 V | data OK, but **VDD below spec** |
| 4.7 V | JP1 → 5 V, after devkit diode drop | 3.29 V | **+0.01 V** | **fails** |
| 5.0 V | direct from VBUS | 3.50 V | **−0.20 V** | **fails** |
| 4.3 V | 5 V minus one series diode | 3.01 V | +0.29 V | ✅ passes |
| any | through a 74AHCT125 buffer | — | full 5 V swing | ✅ passes |

The +0.01 V row explains why 5 V "sometimes works" on boards like this — the
Schottky drop lowers VIH just enough to scrape by. That is luck, not margin,
and it varies with load current and temperature.

**Conclusion: any move to 5 V requires fixing the data line.** Two ways to do
that, below.

---

## 5. What is needed

### Now — make the 5 V jumper position usable

Recommended: a single-gate buffer on the LED data line.

| Ref | Part | Package | Purpose |
|---|---|---|---|
| U1 | **`SN74AHCT1G125DBVR`** | SOT-23-5 | 3.3 V → 5 V buffer for `LED_DIN` |
| C4 | 100 nF | 0805 | U1 decoupling |

Why AHCT specifically: it runs from 5 V but has **TTL input thresholds
(VIH = 2.0 V)**, so a 3.3 V signal reads solidly high and the output swings a
full 5 V. The quad version `SN74AHCT125` (SOIC-14, or `-N` in DIP for
breadboarding) is the part Adafruit recommends universally for WS2812; the
single-gate variant is the better fit here because only one channel is needed.

**Cheaper one-part alternative:** put a series diode in the LED VDD line to drop
the rail to ~4.3 V, which pulls VIH down to 3.01 V. Another `1N5819HW-7-F` does
it — the same part the devkit already uses. Smaller and cheaper, at the cost of
slightly dimmer LEDs. Adequate for 10 LEDs; an `SS34` (3 A) would be needed at
ring scale.

Board changes either way: one part, one net split (`LED_DIN` becomes
`LED_DIN_3V3` → buffer → `LED_DIN_5V`), and a re-route of the LED data trace.

### Later — the 20–30 LED ring

Only worth doing once draw exceeds ~300 mA. Requires bringing VBUS onto the
carrier board directly:

| Part | Suggested | Notes |
|---|---|---|
| USB-C receptacle | **`HRO TYPE-C-31-M-12`** (JLCPCB `C165948`) or **`GCT USB4085-GF-A`** | 16-pin power-only; the ubiquitous cheap part |
| CC resistors | 2 × **5.1 kΩ 1%** | one on CC1 **and** one on CC2 — separate resistors, never shared |
| Bulk capacitor | **`Panasonic EEU-FR1A102`** (1000 µF, 10 V) | the cap `docs/HARDWARE.md` already calls for |
| Protection | 2 A polyfuse, e.g. `Littelfuse 1812L200/16MR` | or a soft-start load switch (`TI TPS22965DSGR`) to avoid inrush tripping the port |
| Level shifter | as above | still required — more so at a true 5.0 V rail |

Wiring: carrier VBUS feeds the LED rail directly (wide trace, bulk cap) **and**
back-feeds the devkit's 5 V pin to power the devkit.

---

## 6. Programmability — already solved by the devkit's diodes

Powering a devkit through its 5 V pin usually raises a back-feed concern. Here
it does not, and the reason is the same `1N5819HW` from §2.

Both diodes are oriented **VBUS → `VCC_5V`**. Injecting 5 V *into* the 5 V pin
reverse-biases them, so **no current can flow back out to either USB host**.

Therefore:

- Both devkit USB ports (UART and native) stay fully free for flashing.
- **Both cables may be plugged at once.** A carrier USB-C sits at 5.0 V; the
  devkit's path reaches `VCC_5V` at only ~4.7 V after its diode, so the external
  supply simply wins and the devkit's diode blocks. No conflict, no back-feed.
- Auto-reset (the DTR/RTS transistor pair on the devkit) is untouched — the
  `make flash PORT=...` workflow is unaffected.
- Single-cable operation through the devkit's own USB works today and needs no
  new connector at all.

---

## 7. Recommendation

1. **Add `SN74AHCT1G125DBVR` + 100 nF to the carrier board** and move JP1's
   default to 5 V. Small, contained revision; it makes the existing 5 V jumper
   position honest and brings the WS2812B inside its specified supply range.
2. **Defer the USB-C input.** It only earns its place above ~300 mA, i.e. when
   the LED ring actually lands. Adding it now buys nothing and costs a
   connector, CC resistors, a fuse, wide traces, and a bulk cap.
3. **Leave the 74AHCT125 note in `docs/HARDWARE.md` as-is** — it was right; this
   analysis just supplies the numbers behind it and picks the package.

## Open items

- `J4` (IMU) pin order is wired for the common GY-BMI160 12-pin order and
  remains unverified against the physical module. Printed on the back
  silkscreen so it can be checked against a bare board.
- No enclosure dimensions available; the outline is a plain rounded rectangle
  sized to the devkit, with no allowance for the monitor-clip / phone-dock
  mechanics on the README roadmap.
- Spare devkit GPIOs are unconnected rather than broken out to an expansion
  header — worth reconsidering when the roadmap's presence sensor lands.

## Sources

- [ESP32-S3-DevKitC-1 schematic v1.1](https://dl.espressif.com/dl/schematics/SCH_ESP32-S3-DevKitC-1_V1.1_20221130.pdf) — power tree, diodes, LDO, header nets
- [ESP32-S3-DevKitC-1 mechanical DXF](https://dl.espressif.com/dl/schematics/esp_idf/DXF_ESP32-S3-DevKitC-1_V1.1_20220429.dxf) — 22.86 mm row spacing, pin order
- [WS2812B datasheet](https://cdn-shop.adafruit.com/datasheets/WS2812B.pdf) — 3.5–5.3 V supply range, `VIH = 0.7 × VDD`
- [1N5819HW-7-F datasheet](https://www.mouser.com/ProductDetail/Diodes-Incorporated/1N5819HW-7-F?qs=NQ47qNm99eDyWTEd07miYA%3D%3D) — 40 V, 1 A, Vf
