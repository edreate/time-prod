# Usage:
#   make setup                          # one-time: installs PlatformIO + pulls toolchain/libs
#   make build   [ENV=...]
#   make upload  [ENV=...] PORT=/dev/cu.usbmodemXXXX
#   make monitor        PORT=/dev/cu.usbmodemXXXX
#   make flash   [ENV=...] PORT=/dev/cu.usbmodemXXXX   (upload, then monitor)
#
# ENV selects which firmware+board to build/upload (see platformio.ini):
#   time-prod-app-n16r8   (default) the firmware, on the original N16R8 devkit
#   test-peripherals-n16r8       I2C scan + display + IMU readout, N16R8
#   time-prod-app-s3zero         the firmware, on the ESP32-S3 Zero
#   test-peripherals-s3zero      I2C scan + display + IMU readout, S3 Zero
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
PORT ?= /dev/cu.usbmodem2101
ENV  ?= time-prod-app-n16r8

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
