# Usage:
#   make setup                          # one-time: installs PlatformIO + pulls toolchain/libs
#   make build   [ENV=...]
#   make upload  [ENV=...] PORT=/dev/cu.usbmodemXXXX
#   make monitor         PORT=/dev/cu.usbmodemXXXX
#   make flash   [ENV=...] PORT=/dev/cu.usbmodemXXXX   (upload, then monitor)
#
# ENV selects which firmware to build/upload (see platformio.ini):
#   esp32-s3              (default) the real firmware: timer + LEDs + flip + Wi-Fi
#   test-bringup          I2C scan + display + IMU readout
#   test-orientation      flips the display 180 based on IMU accel
#   test-orientation-led  same, plus LED color change
#   test-button-timer     button-set H:M:S timer + 10-LED countdown bar
#   test-program-menu     menu (Timer / Available-Busy) + IMU auto-flip
#
# build/upload/monitor/flash all depend on setup having run at least once,
# so a fresh checkout just needs `make flash` - setup runs automatically.
#
# PORT: the board's USB-modem device name can change across reconnects/
# reboots (macOS reassigns it) - run `make ports` and pick the one that
# isn't Bluetooth-Incoming-Port/debug-console if uploads fail to find it.
#
# PIO defaults to `python3 -m platformio` so this works even when the `pio`
# script isn't on PATH (common after `pip3 install --user`).

PIO  ?= python3 -m platformio
PORT ?= /dev/cu.usbmodem1101
ENV  ?= esp32-s3

.PHONY: setup build upload monitor flash clean ports

.setup-stamp: platformio.ini
	pip3 install -U platformio
	$(PIO) run -e $(ENV)
	touch .setup-stamp

setup: .setup-stamp

build: .setup-stamp
	$(PIO) run -e $(ENV)

upload: .setup-stamp
	$(PIO) run -e $(ENV) -t upload --upload-port $(PORT)

monitor: .setup-stamp
	$(PIO) device monitor -p $(PORT) -b 115200

flash: upload monitor

clean:
	$(PIO) run -t clean
	rm -f .setup-stamp

ports:
	ls /dev/cu.*
