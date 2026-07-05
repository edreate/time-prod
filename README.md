# Focus & Availability Device

A small device that clips onto the top or bottom edge of your monitor. It helps you protect your focus time - set a timer, show your availability to the team, and park your phone. One device that keeps you on task and tells others not to interrupt.

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
