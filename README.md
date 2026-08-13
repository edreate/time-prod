# FocusDock — Focus & Availability Device

A small desk gadget that protects your focus time. It sits on your desk as a
phone dock, or clips onto the top or bottom edge of your monitor. One glance
tells you (and everyone walking by) what's going on:

- **Menu** — pick a program: Focus Timer or Available/Busy. Long-press the
  center click, from anywhere, to jump back to the menu.
- **Focus timer** — set an H:M:S duration, click to start. The screen counts
  down and the LEDs empty out like a progress bar.
- **Status light** — LEDs glow **green** (free), **red** (busy), or **amber**
  (timer running). One click toggles available/busy: an instant meeting
  indicator.
- **Phone dock** — park your phone on it and out of your hands. (Auto-detect
  of a docked phone is on the [roadmap](#roadmap).)
- **Mounts either way up** — clip it to the top or bottom monitor edge; the
  screen flips itself the right way up automatically.

## The prototype today

Everything below is running on a breadboard right now:

![Breadboard prototype](docs/breadboard.jpeg)

**Working:** display, motion sensor, auto-flip, LED strip, the menu, focus
timer, pomodoro, and the available/busy status light.
**Not built yet:** Wi-Fi settings, phone-presence sensing, enclosure,
proper 5V LED power — see the [roadmap](#roadmap).

## How to use it

The 5-way button does everything:

| Button | Menu | Setting the timer | Timer running/paused | Availability |
|---|---|---|---|---|
| **Up / Down** | Move cursor | Change selected field | Down = cancel (paused) | — |
| **Left / Right** | — | Select H / M / S field | — | — |
| **Click** (center) | Open program | Start | Pause / resume | Toggle available/busy |
| **Hold click** | — | Return to menu, from any screen | | |

When a focus session hits zero the LEDs blink green and the screen shows
*Done!* — press any button to dismiss.

## What's inside

| Part | Job | ~Price |
|---|---|---|
| ESP32-S3 board (WROOM-1 N16R8) | The brain — runs everything, provides Wi-Fi | €14.49 |
| 1.3" OLED display (128×64, I2C) | Timer and menus | €7.19 |
| BMI160 motion sensor | Detects which way up it's mounted | ~€5 |
| WS2812B LED strip | The status light | €3.89 |
| 5-way button (5DirKey) | All input | ~€1 |

Full wiring instructions, circuit diagram, and part gotchas:
**[docs/HARDWARE.md](docs/HARDWARE.md)**.
New to electronics? Every concept this project uses, explained briefly:
**[docs/ELECTRONICS.md](docs/ELECTRONICS.md)**.

## Build & flash the firmware

You need [Python 3](https://www.python.org/downloads/) and a USB-C cable.
Everything else installs itself.

1. **Plug in the board** and find its port:
   ```
   make ports
   ```
   Yours is one of the `usbmodem*` entries (not `Bluetooth-Incoming-Port` or
   `debug-console`). The board has two USB-C ports — if one doesn't respond,
   try the other. macOS can rename the port across reconnects, so re-check if
   an upload suddenly can't find it.

2. **Flash:**
   ```
   make flash PORT=/dev/cu.usbmodemXXXX
   ```
   The first run installs PlatformIO and downloads the toolchain (~2 min,
   one-time). It then uploads the firmware and opens the serial monitor so you
   can watch the device's log output. `Ctrl+C` exits the monitor.

Other targets: `make build`, `make upload`, `make monitor`, `make clean`.

**If upload hangs at "Connecting..."**: hold **BOOT**, tap **RESET**, release
**BOOT**, retry. If it fails partway with a checksum error, that's a flaky USB
connection — reseat the cable and retry.

### Test firmwares

Besides the main firmware there's a small test build for checking the
hardware — useful after wiring changes:

| `ENV` | What it checks |
|---|---|
| `time-prod-app` *(default)* | The real firmware — everything |
| `test-peripherals` | I2C scan + display + IMU readout: is everything wired and answering? |

Flash it with `make flash ENV=test-peripherals PORT=...`.

## Tweaking the firmware

Every tunable number lives in [`src/config.h`](src/config.h): pin assignments,
colors, timer defaults, thresholds. Change a value, `make flash`, done.

Each piece of hardware sits behind a small header-only module in `src/hw/`, so
`main.cpp` holds only the app: it asks for button events and an orientation and
says what to display. Adding a sensor means adding one header there and two
lines in `main.cpp` — no build config to touch.

Project layout:

```
src/config.h        every tunable (pins, colors, thresholds)
src/hw/display.h    OLED panel
src/hw/leds.h       WS2812B status light
src/hw/imu.h        accelerometer -> screen orientation
src/hw/buttons.h    5-way switch, debounced
src/main.cpp        the app: state machine, screens, loop
src/test_peripherals.cpp  peripheral check test build
platformio.ini      build configuration (one env per firmware)
Makefile            build/upload/monitor shortcuts
docs/HARDWARE.md    parts, wiring, circuit diagram
docs/ELECTRONICS.md electronics concepts, briefly explained
```

## Roadmap

What it takes to go from breadboard to a product with a long life:

**Hardware**
- [ ] Move LED strip to proper 5V power + 74AHCT125 level shifter + 1000 µF cap
- [ ] Presence sensor for the phone dock (IR / light / pressure — pick one)
- [ ] USB-C power breakout with Schottky diode (safe dual-power)
- [ ] Scale to the full 20-30 LED ring on a 5V/3A supply
- [ ] Enclosure + monitor clip design (3D-printed first)
- [ ] Evaluate rotary encoder vs. 5-way button for the final feel
- [ ] Custom PCB once the design settles

**Firmware**
- [ ] Dock/undock actions (auto-start focus session when phone is docked)
- [x] Pomodoro program (work / short break / long break, auto-cycling)
- [ ] Settings page over the device's own Wi-Fi hotspot, saved across power-off
      (would also bring back the Info screen: SSID / IP / MAC)
- [ ] Wi-Fi client mode + NTP so the menu screen can show a clock
- [ ] Calendar integration (busy light follows your meetings automatically)
- [ ] Optional buzzer/chime when the session ends
- [ ] Over-the-air firmware updates (no cable needed)
- [ ] MQTT / Home Assistant integration
- [ ] Factory-reset gesture (e.g. hold click 10 s)

**Product**
- [ ] Session stats (focus minutes per day/week)
- [ ] Battery option + deep sleep for cable-free desks
- [ ] User-test with non-technical people; simplify anything they stumble on

## License

See [LICENSE](LICENSE).
