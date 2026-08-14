Review complete. All 10 finder angles plus the gap sweep ran, and I verified the load-bearing library-internals claims directly against .pio/libdeps/time-prod-app/.

One incident to flag first: a finder agent modified src/config.h mid-review (LED_BRIGHTNESS 60 → 200). That was not part of the PR. I reverted it (git checkout -- src/config.h); the working tree is clean and back at 60. One candidate finding that was built on the contaminated value has been dropped as refuted.

[
{
"file": "src/hardware/imu.h",
"line": 19,
"summary": "`static DFRobot_BMI160 bmi160;` runs a constructor that calls bare `Wire.begin()` at static-init, so `setup()`'s `Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL)` early-returns as a no-op and the config.h I2C pins are dead.",
"failure_scenario": "Verified in .pio/libdeps/time-prod-app/DFRobot_BMI160/DFRobot_BMI160.cpp:32 — the default constructor's first statement is `Wire.begin();`. Namespace-scope statics are constructed before setup(), so the bus is initialized with the ESP32-S3 variant defaults (SDA=8/SCL=9). main.cpp:179 then calls Wire.begin(8,9), which hits the core's `if(i2cIsInit(num)){ started = true; goto end; }` and skips initPins()/i2cInit() entirely. It works today only because PIN_I2C_SDA/PIN_I2C_SCL happen to equal the variant defaults. Change config.h:6-7 to any other GPIO — the file whose own header says 'This is the one place to re-map hardware; nothing below it hardcodes a pin' — and the firmware keeps driving GPIO8/9: blank display, dead IMU, and the only diagnostic is a log_w suppressed at CORE_DEBUG_LEVEL=0."
},
{
"file": "src/hardware/imu.h",
"line": 43,
"summary": "`imuOrientation()` runs every loop pass and DFRobot's I2C read contains ~23 ms of blocking `delay()`, so this one call — not `delay(LOOP_DELAY_MS)` — sets the loop rate, and short button taps are dropped.",
"failure_scenario": "Verified in DFRobot_BMI160.cpp: I2cGetRegs does `delay(10)` before requestFrom (line 805) and `delay(1)` after each of the 12 `Wire.read()` calls (line 810), plus `delay(1)` in getRegs (line 791) = ~23 ms per call, against ~350 us of actual bus work. Arduino-ESP32's delay() is vTaskDelay with CONFIG_FREERTOS_HZ=1000, so these are real tick-granular blocks. Real loop period becomes ~33 ms (63 ms on draw passes), i.e. ~30 Hz not the 100 Hz the design assumes. Button::update() is a sampled state machine, so a press-and-release completing inside one interval never changes `reading` and is invisible — a 40 ms tap landing on a draw pass is silently lost, and a caught press needs ~66-130 ms of hold. This is why menu navigation and the hold=menu gesture will feel unreliable on hardware even though the debounce logic is correct. Gate imuOrientation() to 10-20 Hz (still 5-7x faster than FLIP_DEBOUNCE_MS=350 requires)."
},
{
"file": "src/hardware/imu.h",
"line": 30,
"summary": "`bmi160.softReset()` is called before `bmi160.I2cInit(IMU_I2C_ADDR)`, so it transmits to an uninitialized I2C address on the bus the already-initialized OLED shares — and the `== BMI160_OK` check can never fail.",
"failure_scenario": "Verified: the constructor mallocs Obmi160 but leaves `->id` and `->interface` unassigned (DFRobot_BMI160.cpp:34-35 are commented out; they are only set inside I2cInit at lines 42-43). The no-arg softReset() (line 89) calls softReset(Obmi160) -> setRegs(BMI160_COMMAND_REG_ADDR,...) -> I2cSetRegs, which does `Wire.beginTransmission(dev->id)` (line 842) with raw malloc'd garbage, writing 0x7E then 0xB6. setup() calls displayBegin() before imuBegin(), so if the garbage id lands on 0x3C the SH1106 receives 0x7E as 'set display start line' and 0xB6 as 'set page' and renders scrolled/garbled until power cycle. Separately, I2cSetRegs discards endTransmission()'s return and ends `return BMI160_OK;` (line 859), so softReset() cannot fail — the left half of the `&&` is a vacuous check and only I2cInit()'s return actually gates imuOk. Carried over from the old main.cpp, but imu.h is a new file so every line is in scope."
},
{
"file": "src/main.cpp",
"line": 112,
"summary": "`updateLeds()` forces COLOR_OFF for PROGRAM_MENU, so the available/busy status light goes dark the instant the user long-presses home — on a device whose entire purpose is a room-visible status light.",
"failure_scenario": "Open Available/Busy, click to set BUSY (strip red), then use the screen's own documented 'hold=menu' gesture. There is no availabilityReset(), so the `availability` static stays AVAIL_BUSY, but the very next updateLeds() takes the PROGRAM_MENU arm and blanks the strip. A colleague walking past sees an unlit device indistinguishable from one that is powered off. Same at boot: currentProgram starts PROGRAM_MENU, so the strip is dark where the old build showed green. The old code did this at the right depth — statusColor() (old main.cpp:246-252) was a device-level resolver consulted every frame 'regardless of what program is active'. README.md:11-13 still advertises the light as an 'instant meeting indicator'. Deeper fix: an ambient status layer the shell renders unconditionally, with per-program *Leds() as an optional claim on the strip."
},
{
"file": "src/software/pomodoro.h",
"line": 90,
"summary": "`pomodoroReset()` clears `pomoRunState` and `pomoSessionCount` but never resets `pomoPhase`, and the new menu dispatch dropped the old entry reset, so nothing in the firmware ever returns pomoPhase to PHASE_WORK.",
"failure_scenario": "Old enterProgram() (main.cpp:389-394) set pomoRunState=POMO_IDLE, pomoPhase=PHASE_WORK, pomoSessionCount=0 on every entry; the new shell replaced that with a bare `currentProgram = MENU_PROGRAMS[menuIndex]` (main.cpp:75) and relies on pomodoroReset(), which omits pomoPhase. Reachable without even leaving the screen: run a work session to completion (pomoAdvancePhase sets pomoPhase=PHASE_SHORT_BREAK), pause during the break, press down (line 165) -> pomodoroReset() -> POMO_IDLE. pomodoroDraw() then renders the header as pomoPhaseLabel(pomoPhase)='Short Break' (line 230) directly above big='25:00' (the work duration, line 211) and hint='click=start' — while clicking actually calls pomoStart(PHASE_WORK). The screen names a phase the device is neither in nor about to enter. Same stale header after a long-press exit and re-entry."
},
{
"file": "src/main.cpp",
"line": 75,
"summary": "Entering the Timer no longer restores the default H:M:S, so a user who leaves it at 00:00:00 returns to a screen whose start button silently does nothing, permanently.",
"failure_scenario": "Old enterProgram(0) set setHours=0, setMinutes=settings.focusMinutes (25), setSeconds=0, selectedField=FIELD_MINUTE on every entry. The new code assigns currentProgram only, and timerReset() touches just timerState — setHours/setMinutes/setSeconds/selectedField are file-scope statics that persist across visits. The editor wraps mod 24/60/60, so 00:00:00 is one down-press away from any value. Dial all three to zero, long-press to the menu, re-open Timer: the screen shows 00:00:00, clicking center calls timerStart(), which hits `if (totalMs <= 0) return;` (timer.h:46) and does nothing, with no message and no LED change. The state survives every subsequent menu round-trip, so the timer is dead until the user works out they must dial a duration back up."
},
{
"file": "src/software/timer.h",
"line": 124,
"summary": "In the PAUSED arm of both timerInput and pomodoroInput, click and down are independent `if`s over the same ButtonEvents, so a single poll carrying both applies both transitions — in Pomodoro's case resuming and then wiping the whole cycle.",
"failure_scenario": "Each of the five buttons is an independent debounced Button and both report `clicked` in whichever poll they settle in, so a near-simultaneous press puts both flags in one ButtonEvents. timer.h:118-126: `if (b.click)` resumes (lastTickMs=millis(), STATE_RUNNING), then control falls straight into `if (b.down)` -> STATE_SET, discarding the countdown. pomodoro.h:159-167 is worse: click resumes, then `if (b.down)` calls pomodoroReset(), dropping POMO_IDLE *and* zeroing pomoSessionCount — pause on session 3 of 4, fumble the two buttons together, and three completed work sessions vanish with the cycle restarted from zero. Making these arms mutually exclusive (else-if, or handling click last) confines each poll to one transition; note the fix has to be applied twice because the two modules are copy-paste."
},
{
"file": "src/software/timer.h",
"line": 172,
"summary": "The Set-Timer hint is 138 px wide on a 128 px panel, so the tail is clipped and the user is never told that the center click starts the timer.",
"failure_scenario": "Confirmed by two independent agents decoding the installed u8g2 font tables: for u8g2_font_6x10_tr every glyph advances 6 px, so `\"</> field ^v +/- clk=go\"` (23 chars) measures 137-138 px. displayText(0, 48, hint) draws left-aligned at x=0, so glyph 21 ('g') starts at x=126 with 2 px visible and glyph 22 ('o') is entirely outside the buffer. The user reads '</> field ^v +/- clk=' on the one screen that has no other affordance explaining how to start. This line is new in this diff — the old build had `hint = \"</> field ^v +/-\"` (95 px), which fit. Every other string in the firmware was measured and fits; the next widest is 126 px."
},
{
"file": "src/software/timer.h",
"line": 83,
"summary": "DONE_AUTO_DISMISS_MS was deleted with no replacement, so the timer's Done state blinks the LEDs and holds the screen indefinitely until a human presses a button.",
"failure_scenario": "Old handleButtons() ran `if (timerState == STATE_DONE && (millis() - doneSinceMs) > DONE_AUTO_DISMISS_MS) timerState = STATE_SET;` — the Done screen cleared itself after 60 s. The define is gone from config.h and grep for DISMISS/60000 across src/ returns nothing; timerTick() returns immediately for any non-running state, so nothing but a button press leaves STATE_DONE. A session ends while the user is in a meeting: timerLeds() blinks all 10 LEDs green at 2 Hz forever and the OLED holds a static 'Done! / 00:00:00' frame forever — OLED burn-in, wasted power on a device that caps LED brightness for its power budget, and the device never returns to reporting availability. Note pomodoroTick() *does* self-advance out of POMO_PHASE_DONE, so the two programs now disagree on whether a terminal state times out."
},
{
"file": "src/main.cpp",
"line": 43,
"summary": "MENU_COUNT is a hand-written literal decoupled from both MENU_ITEMS and MENU_PROGRAMS, and tickProgram's `default:` suppresses the one compiler check that would catch the drift.",
"failure_scenario": "Two independent breaks. (a) Bump MENU_COUNT to 4 and add a MENU_ITEMS row but forget MENU_PROGRAMS: line 75 reads one past a 3-element array, currentProgram takes a garbage value, and all four dispatch switches then match no case — blank screen (displayBeginFrame/EndFrame with nothing between), frozen LEDs, nothing responds but a long press. (b) Add to both arrays but forget MENU_COUNT: the entry is silently unreachable, since drawMenu() emits MENU_COUNT rows and the `% MENU_COUNT` wrap never lets the cursor reach index 3. PlatformIO ships -Wall so -Wswitch covers handleInput/updateLeds/drawScreen, but tickProgram's `default: break;` (line 102) suppresses it and the long-press reset if-chain (lines 59-62) is not a switch at all — so the two omissions that produce a silent runtime bug (a program that never ticks; a program that leaks state across sessions) are exactly the two with zero compile-time coverage. Fix: derive MENU_COUNT via sizeof, static_assert MENU_PROGRAMS has the same extent, and replace `default:` with explicit arms."
},
{
"file": "src/software/pomodoro.h",
"line": 190,
"summary": "COLOR_BUSY and COLOR_POMO_WORK are both 0xFF0000, so a paused pomodoro is pixel-identical to a running work block; the timer has the same blind spot from a different cause.",
"failure_scenario": "config.h:20 defines COLOR_BUSY 0xFF0000 and config.h:27 defines COLOR_POMO_WORK 0xFF0000. In pomodoroLeds(), `case POMO_RUNNING: ledsSetAll(pomoPhaseColor(PHASE_WORK))` and `case POMO_PAUSED: ledsSetAll(COLOR_BUSY)` emit the same value, so the two states are indistinguishable for 25 of every 30 cycle-minutes — the POMO_PAUSED arm is dead in effect. A user pauses a work block to take a call and everyone walking past still reads 'do not disturb'. timer.h:142-144 has the same outcome from a different cause: STATE_PAUSED shares ledsSetProgress() with STATE_RUNNING, so a paused countdown is a frozen bar that looks like a running one (the old build showed COLOR_BUSY for a paused timer). On a device whose premise is 'one glance tells everyone walking by what's going on', pause has no visible signal in either program."
},
{
"file": "Makefile",
"line": 42,
"summary": "The `monitor` target is reverted to `-p $(PORT)`, silently undoing commit 089cfbd 'monitor port', which had split the monitor port from the upload port because they are different devices.",
"failure_scenario": "Verified: 089cfbd is a dedicated one-purpose commit that changed exactly this line from `-p $(PORT)` to `-p $(MON_PORT)` and added `MON_PORT ?= /dev/cu.usbmodem59700474361` alongside `PORT ?= /dev/cu.usbmodem1101` — two distinct devices, as the ESP32-S3 devkit exposes both a native-USB CDC port and a UART-bridge port. This diff deletes MON_PORT and restores the pre-fix line verbatim. A user running the documented `make flash PORT=/dev/cu.usbmodemXXXX` uploads successfully, then gets a monitor attached to the wrong device and sees no serial output — which breaks the project's only verification path, since CLAUDE.md states 'Verification means flashing a board and reading the serial log at 115200 baud'."
},
{
"file": "src/hardware/leds.h",
"line": 26,
"summary": "The ceil expression is 32-bit signed arithmetic that overflows once the bucket count reaches ~24, blanking the bar instead of scaling it — and the same expression is duplicated in timer.h.",
"failure_scenario": "`long` is 32 bits on xtensa-esp32s3. timerAdjustField caps the timer at 23:59:59, so totalMs peaks at 86,399,000 ms; at NUM_LEDS=10 the expression peaks at 950,388,999, safely under INT32_MAX. The threshold is N <= (2147483647 - 86398999)/86399000 = 23.8. README.md:139 explicitly plans 'Scale to the full 20-30 LED ring', so setting NUM_LEDS to 24-30 in config.h — the file documented as the one place to re-map hardware — and starting a long timer gives >2.1e9: signed overflow, `lit` comes out negative, `i < lit` is false for every pixel, and the strip stays dark for the entire session. timer.h:203 has the identical expression over PROGRESS_CHARS with the same cliff, so the fix has to be found twice. Extract one `progressCells(long remaining, long total, int cells)` and widen to long long."
},
{
"file": "README.md",
"line": 7,
"summary": "The diff rewrites the README to describe a two-program menu while shipping three, and deletes the Pomodoro column from the button table — leaving a full 243-line program with no documented controls anywhere in the repo.",
"failure_scenario": "README.md:7 as changed reads '**Menu** — pick a program: Focus Timer or Available/Busy', actively replacing accurate prior text; main.cpp:42 ships three MENU_ITEMS and src/software/pomodoro.h is a complete program added by this same diff. The README then contradicts itself at line 26 ('Working: ... focus timer, pomodoro'), line 121 (the file listing) and line 146 ('[x] Pomodoro program'). The button table (line 34) lost its Pomodoro column, so bindings that appear in no doc include POMO_PAUSED + down calling pomodoroReset() — strictly more destructive than the timer's documented 'Down = cancel (paused)' because it also zeroes pomoSessionCount. Blue (COLOR_POMO_LONG_BREAK) was dropped from the status-light list at lines 11-13 though pomodoroLeds() still drives it. platformio.ini:6 and Makefile:9 repeat the same erasure. A user sees a Pomodoro row on the device that no document explains, and pressing down while paused silently restarts their 4-session cycle."
},
{
"file": "CLAUDE.md",
"line": 88,
"summary": "The CLAUDE.md added by this diff is factually wrong about the code it ships with in two ways: the 'Adding a program' recipe omits two required edit sites, and the state-machine rule describes an invariant the code does not hold.",
"failure_scenario": "CLAUDE.md:88 states the complete recipe as 'a new src/software/*.h with the shape above, a row in MENU_ITEMS/MENU_PROGRAMS, and a case in each dispatch switch' — omitting a new value in `enum Program` (main.cpp:31-37) and a bump of the `MENU_COUNT = 3` literal (main.cpp:43). An agent following it verbatim ships a menu row that is invisible and unreachable with a clean build and no warning (see the MENU_COUNT finding). main.cpp:46-47 repeats the same incomplete list, and README.md:113 understates it further as 'one header plus a menu row' when the real count is eight sites. Separately CLAUDE.md:98 states program state is 'written from that module's Input()' — but timer.h:91 sets STATE_DONE inside timerTick() and pomodoro.h:133 calls pomoAdvancePhase() from pomodoroTick(), which is how a countdown reaches Done and how pomodoro auto-cycles at all. An agent taking that clause as the invariant and refactoring the expiry write into timerInput() would make an unattended timer run past zero forever."
}
]

Did not make the 15-item cap, in rough priority order: displayCentered's negative x wrapping to ~65530 because u8g2_uint_t is uint16_t (real mechanism, no string currently wide enough to trigger it); NUM_LEDS changing 8→10 silently inside the refactor with nothing in the repo pinning the real strip length; strip.show() running unthrottled every loop pass (each call tears down and rebuilds the RMT driver on this core — ~50 malloc/free pairs per second) while drawScreen() is throttled, plus sendBuffer() pushing 1 KB over I2C every 100 ms with no dirty check; SCREEN_WIDTH defined in display.h in violation of CLAUDE.md's "every tunable is a #define here"; test_peripherals.cpp bypassing config.h with a bare Wire.begin() and its own BMI160_ADDR, so the one surviving diagnostic build drifts from the app and covers 2 of 4 peripherals; the countdown engine duplicated wholesale between timer.h and pomodoro.h (six near-identical blocks, which is why finding 7 must be fixed twice); ledsSetAll/ledsSetProgress hand-rolling loops that strip.fill() already provides; pomoTotalMs being write-only; pomodoroDraw reading DEFAULT_POMO_WORK_MIN directly instead of through pomoPhaseMinutes(), breaking the seam for the settings page the roadmap promises; and docs/HARDWARE.md:38 labelling all four direction GPIOs contrary to config.h (pre-existing, but this diff edited that page).

Verified clean, so it needn't be re-checked: both envs compile from scratch; build_src_filter matches each .cpp exactly once; no macro or type-name collisions against the pinned libs or the ESP32 core; every snprintf buffer is adequate; no millis() rollover defect; the button pin remap is byte-identical to main; and the orientation math ported faithfully with all three anti-flicker guards intact.
Now using usage credits
