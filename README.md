# Focus & Availability Device

A small device that can be kept on a table where phone can be docked or clips onto the top or bottom edge of your monitor (oreintation changable). It helps you protect your focus time - set a timer, show your availability to the team, and park your phone. One device that keeps you on task and tells others not to interrupt.

## Features

- **Focus timer** - Turn the knob to set a session, click to start. The screen shows the countdown.
- **Status light** - LEDs around the edge glow red, yellow, or green so colleagues can see your availability at a glance.
- **Phone dock** - Rest your phone on the ledge. Docking can auto-trigger actions like starting a focus session or changing your status.
- **Clips top or bottom** - Mounts on either edge of the monitor. The display flips automatically based on orientation.

## Tech

- **ESP32** - main controller, with Wi-Fi and Bluetooth for future integrations
- **Front-facing display** - timer and UI
- **WS2812B addressable RGB LEDs** - status lighting
- **Rotary encoder with push button** - twist to set, click to confirm
- **IMU** - detects mounting orientation, auto-flips the UI
- **Presence sensor** (IR, light, or pressure) - detects when a phone is docked


# Work in Progess:
## BOM / Actual Tech (work in progress)

The parts we're actually looking at. Prices are the current discounted price from the links below (EUR, may change).

| Part | What it's for | ~Price | Status |
|---|---|---|---|
| ESP32 board | Main controller | €14.49 | Picked - see link |
| WS2812B LED strip | Status light | €3.89 | Picked - see link |
| IMU | Detects flip / orientation | €4.79-5.39 | 3 options - pick one |
| Display | Timer + UI screen | €7.19 (?) | **To confirm** - see links |
| Rotary encoder + knob | Set and confirm timer | - | TBD |
| Presence sensor | Detect docked phone | - | TBD |
| USB-C power breakout | 5V power in | - | TBD |
| Support parts | Level shifter, resistor, cap, diode | - | TBD (see notes) |

## Links

Click to open each product in a new tab.

**ESP32 board**
<ul>
<li><a href="https://de.aliexpress.com/item/1005006935181127.html" target="_blank" rel="noopener">ESP32 board - €14.49</a></li>
</ul>

**LED strip (WS2812B)**
<ul>
<li><a href="https://de.aliexpress.com/item/2036819167.html" target="_blank" rel="noopener">WS2812B RGB LED strip - €3.89</a></li>
</ul>

**IMU (pick one)**
<ul>
<li><a href="https://de.aliexpress.com/item/1005008796700745.html" target="_blank" rel="noopener">IMU option 1 - €5.09</a></li>
<li><a href="https://de.aliexpress.com/item/1005007530430125.html" target="_blank" rel="noopener">IMU option 2 - €4.79</a></li>
<li><a href="https://de.aliexpress.com/item/1005008610645902.html" target="_blank" rel="noopener">IMU option 3 - €5.39</a></li>
</ul>

**Display**
<ul>
<li><a href="https://de.aliexpress.com/item/1005012056606318.html" target="_blank" rel="noopener">Display A 1.3 inch 128x64 - €7.19</a></li>
<li><a href="https://de.aliexpress.com/item/1005011653680962.html" target="_blank" rel="noopener">Display B 1.3 inch 128x64 - €10.99</a></li>
</ul>

**Others** - TBD

## Notes

### Power
- WS2812B pulls **60mA per LED** at full white. A status color at normal brightness is more like **8-10mA**.
- Plan for **20-30 LEDs** on a **5V/3A USB-C supply** - a full ring, plenty bright.
- Use a software brightness cap so it can never draw too much and brown out.

### Support parts still needed
These make the circuit work properly:
- **74AHCT125 level shifter** - the ESP32 runs at 3.3V but the LEDs need 5V data. This chip fixes that.
- **1000µF capacitor** across LED power - stops power spikes that can kill the first LED.
- **330-470Ω resistor** on the LED data line.
- **Schottky diode (SS34)** on VIN - protects the board when the programming USB and the 5V supply are both plugged in.

## Firmware

Two test builds so far, selected via PlatformIO's `-e <environment>` (see [platformio.ini](platformio.ini)). Both assume the wiring in [Req-Design.md](Req-Design.md). Built with [PlatformIO](https://platformio.org/).

| `ENV` | Source | What it does |
|---|---|---|
| `esp32-s3` (default) | `src/main.cpp` | I2C scan + display + IMU bring-up test |
| `esp32-s3-orientation` | `src/test_orientation.cpp` | Flips the display 180° based on IMU accel (see note below) |

### Flashing

1. Install PlatformIO Core once: `pip3 install -U platformio`
   - If `pio: command not found` afterward, it installed to a user bin not on your `PATH` (macOS pip does this). Either add it: `export PATH="$HOME/Library/Python/3.9/bin:$PATH"`, or run everything below as `python3 -m platformio ...` instead of `pio ...`.
2. Plug in the board and find its port:
   ```
   ls /dev/cu.*
   ```
   It's one of the `usbmodem*` entries, not `Bluetooth-Incoming-Port` or `debug-console`. The board exposes two USB-C ports - if one doesn't respond, try the other. Note: macOS can reassign this name across reconnects/reboots, so re-check it if uploads suddenly can't find the port.
3. Build and flash with the `Makefile` (`PORT` defaults to the last-confirmed-working port on this board - override if yours differs; `ENV` defaults to `esp32-s3`, override to flash the orientation test):
   ```
   make flash PORT=/dev/cu.usbmodemXXXX
   make flash PORT=/dev/cu.usbmodemXXXX ENV=esp32-s3-orientation
   ```
   This uploads then opens the serial monitor. First run installs PlatformIO and pulls the toolchain/libraries automatically (`setup`, ~2 min one-time cost) - every target depends on it, so you don't need to run it separately. Other targets: `make build`, `make upload`, `make monitor`, `make ports` (lists connected devices), `make clean`.

   Equivalent raw PlatformIO commands, if you'd rather skip the Makefile:
   ```
   pio run -e esp32-s3-orientation -t upload --upload-port /dev/cu.usbmodemXXXX
   pio device monitor -p /dev/cu.usbmodemXXXX -b 115200
   ```

   Command last used to flash `test_orientation.cpp` (port will likely differ for you - see step 2):
   ```
   make upload ENV=esp32-s3-orientation PORT=/dev/cu.usbmodem1101
   ```

If upload hangs at "Connecting...", hold **BOOT**, tap **RESET**, release **BOOT**, then retry. If it fails partway through with an `esptool` checksum error, that's a flaky USB connection, not a firmware bug - retry, or reseat the cable.

### Orientation detection (`esp32-s3-orientation`)

The device only clips in two valid positions (top or bottom edge of the monitor), not continuous tilt, so this is a two-state classifier, not full attitude estimation - a single accel axis is enough:

- **Axis & threshold**: `ax` (accel X) measured ~+1g mounted vertically in normal orientation, ~-1g flipped. Threshold is set at ±0.5g.
- **Hysteresis**: the dead zone between -0.5g and +0.5g means noise near the crossover doesn't flicker the display - only a reading clearly past the threshold moves the state.
- **Debounce**: a reading has to stay past the threshold for 350ms before the flip commits, so a bump or mid-handling tilt doesn't trigger a false flip.
- **Smoothing**: raw accel is exponentially smoothed (`EMA_ALPHA = 0.2`) before thresholding to cut sensor noise.

If the IMU's mounting inside the enclosure changes, re-measure `ax` in both orientations and adjust `FLIP_THRESHOLD` / the axis used in `src/test_orientation.cpp` accordingly.
