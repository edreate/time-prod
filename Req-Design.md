microcontrooler board: esp32 s3 wroom 1 n16r8

button: 5DirKey_V1-2 3035DIRKEY1 20251016

imu: bmi-160 board with 12 pins out

Display: 1.3 inch i2c 4 pins

LED Strip: SMD-WS2812B_RGB-02-ML

---

## Wiring (prototype, breadboard)

Shared rails: ESP32 `3V3` -> breadboard + rail (logic power). ESP32 `5Vin` -> separate + rail (LED power, few LEDs only). All GND tied together, including LED strip GND.

| Component | Pin | Connects to |
|---|---|---|
| Display (OLED) | VDD | 3.3V rail |
| | GND | GND rail |
| | SCK | ESP32 GPIO9 (I2C SCL) |
| | SDA | ESP32 GPIO8 (I2C SDA) |
| IMU (BMI160) | VIN | 3.3V rail |
| | 3V3 | **unconnected** — this is the onboard LDO's output, not an input; don't also feed it from the ESP32 |
| | GND | GND rail |
| | SCL | GPIO9 (shared I2C bus) |
| | SDA | GPIO8 (shared I2C bus) |
| | CS | 3.3V rail (**must be tied high, or module falls back to SPI mode and I2C won't respond**) |
| | SA0 | GND (addr 0x68) or 3.3V (0x69) — pick one, avoid clash if anything else uses 0x68 |
| | OCS | unconnected (aux-interface chip select, unused — if I2C is flaky, try tying this high or low, floating aux pins are a known culprit) |
| | INT1 / INT2 | unconnected (only needed for interrupt-driven orientation detection instead of polling) |
| | SCX / SDX | unconnected (aux I2C interface, for chaining a magnetometer — not used) |
| LED strip | 5V | 5V rail (from `5Vin`) |
| | GND | GND rail |
| | DIN | ESP32 data GPIO -> 330-470Ω resistor -> strip DIN (no level shifter for prototype, see note) |
| Button (5DirKey) | Up/Down/Left/Right/Press | 5x ESP32 GPIO, each `INPUT_PULLUP` (unverified) |
| | common | GND rail |

## Power note

WS2812B is spec'd DC5V — do not run it at 3.3V, drivers will be starved and colors will be wrong, not just dim. Prototype is ≤10 LEDs, so the ESP32-S3 board's `5Vin` pin (USB VBUS passthrough) is enough — no separate supply needed yet. Move to a dedicated 5V/3A supply only when scaling to the full 20-30 LED ring.

## Decisions locked for prototype

- Level shifter (74AHCT125) skipped for now — 3.3V data direct into 5V strip, short wires. Add back in before scaling up the LED count or finalizing the board.
- I2C bus: GPIO8 (SDA) / GPIO9 (SCL), shared by display + IMU.
- 5DirKey assumed to be 5 independent GPIO signals + common GND (standard for this module style) — **unverified**, confirm with a multimeter before trusting the button pin assignment above.
- IMU CS must be tied to 3.3V (forces I2C mode, not SPI) — verify this against the actual breakout's silkscreen since some BMI160 boards wire CS differently.
