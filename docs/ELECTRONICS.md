# Electronics Concepts, Briefly

Every electronics idea this project uses, explained in a few lines each, with
the example of where it shows up in *our* circuit. No prior knowledge assumed.

## Voltage, current, ground

**Voltage** (volts, V) is electrical pressure; **current** (amps, A) is how
much electricity actually flows. A part needs the right voltage to work and
draws however much current it needs. **Ground (GND)** is the shared zero-point
that all voltages are measured against.

*In this project:* USB gives us 5V. The ESP32 board converts that to 3.3V for
its own chip and the sensors. Every part's GND pin ties to one shared GND rail
— without that common reference, parts can't understand each other's signals.

## GPIO pins

A **GPIO** (general-purpose input/output) is a pin the firmware controls: it
can output 3.3V ("high") or 0V ("low"), or read which of the two a connected
wire is at.

*In this project:* GPIO16 outputs the LED data signal; GPIO4/5/6/7/15 read the
five button directions.

## Pull-up resistors

An input pin connected to nothing "floats" — it reads random noise. A
**pull-up** weakly ties the pin to 3.3V so it reads a steady "high" until
something actively pulls it to GND. The ESP32 has pull-ups built into every
pin that firmware can switch on (`INPUT_PULLUP`).

*In this project:* each button pin idles high via its internal pull-up.
Pressing the button connects the pin to GND, so the firmware sees it go low.
That's why "pressed" is `LOW` in the code, not `HIGH`.

## I2C (the two-wire bus)

**I2C** lets many chips share just two wires: SDA (data) and SCL (clock). Each
chip has an **address** (a number like 0x68) so the ESP32 can talk to one chip
at a time, like calling a name in a room.

*In this project:* the OLED display (address 0x3C) and the IMU (0x68) share
the same two wires, GPIO8 (SDA) and GPIO9 (SCL). The `test-bringup` firmware
scans the bus and prints every address that answers — the first thing to check
when something doesn't respond.

## Addressable LEDs (WS2812B)

A normal LED is one color and one wire per LED. A **WS2812B** strip has a tiny
chip inside every LED: you send one data signal into the strip's DIN pin, and
each LED reads its own color from the stream and passes the rest along. Any
LED, any color, three wires total.

*In this project:* GPIO16 sends the color data. A **330-470 Ω resistor** sits
in the data line to absorb signal spikes that can damage the first LED's input.

## Power budgeting

Add up the worst-case current of everything, and make sure the supply can
deliver it. Exceeding it causes a **brownout**: voltage sags and the
microcontroller resets randomly — a maddening bug if you don't know to look
for it.

*In this project:* each WS2812B can draw 60 mA at full white; 30 LEDs would be
1.8 A — way beyond what USB through the devkit can give. That's why the
firmware caps brightness in software, and why the full LED ring needs its own
5V/3A supply.

## Level shifting

Chips only reliably read a "high" signal above ~70% of their own supply
voltage. A 3.3V signal into a chip powered at 5V is right at the edge — it
might work, or might glitch. A **level shifter** (like the 74AHCT125)
translates 3.3V signals up to 5V.

*In this project:* not needed *yet* because we run the strip at 3.3V. The
moment the strip moves to proper 5V power, the ESP32's 3.3V data signal
becomes marginal and the level shifter goes in.

## Decoupling / bulk capacitors

A **capacitor** is a tiny rechargeable reservoir of charge. Placed across a
part's power pins, it smooths out sudden demand spikes the supply can't react
to fast enough.

*In this project:* when all LEDs switch on at once, the current demand jumps
instantly. A **1000 µF capacitor** across the strip's power pins (planned)
absorbs that spike — without it, the jolt can reset the ESP32 or even kill the
first LED.

## Switch bounce and debouncing

A pressed button doesn't close cleanly — the metal contacts physically bounce
for a few milliseconds, producing dozens of fake press/release events.
**Debouncing** means ignoring changes until the signal has been stable for a
while.

*In this project:* the `Button` struct in `src/main.cpp` only accepts a state
change after it has held steady for `BTN_DEBOUNCE_MS` (30 ms). Without it, one
click could jump the timer by several steps.

## Accelerometers and orientation

An **accelerometer** measures acceleration on three axes — including gravity,
which always points down at a constant 1g. Read which axis gravity appears on
and you know which way up the device is.

*In this project:* the BMI160's X axis reads about +1g mounted normally and
-1g upside down. The firmware smooths the reading (**EMA filter**), requires
it to clearly cross ±0.5g (**hysteresis** — the dead zone in between never
triggers), and to stay there for 350 ms (**debounce**) before flipping the
display. Each of those three guards kills a different kind of false trigger:
noise, wobble near the threshold, and brief bumps.

## Regulator outputs vs. inputs

Many breakout boards include their own voltage regulator. Its *output* pin
looks like a power pin but must not be fed power — two supplies fighting each
other can damage the board.

*In this project:* the BMI160 breakout's `3V3` pin is exactly that: the
onboard regulator's output. We power the board through `VIN` and leave `3V3`
unconnected.

## Chip-select and mode pins

Some chips speak multiple protocols (I2C *and* SPI) and decide which based on
the voltage on a mode pin at power-up. A floating mode pin means random
behavior.

*In this project:* the BMI160's `CS` pin must be tied to 3.3V or the chip
silently switches to SPI mode and stops answering I2C — one of the first
gotchas we hit. Its `SA0` pin picks the I2C address: GND → 0x68, 3.3V → 0x69.
