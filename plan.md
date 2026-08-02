# FocusDock: Wi-Fi client + NTP clock + Pomodoro menu

## Context

Right now the device has no concept of wall-clock time or a real menu — `src/main.cpp` broadcasts its own Wi-Fi hotspot only (no internet), and jumps straight into a single free-form focus timer. Every power loss wipes any notion of "what time is it," and there's no Pomodoro work/break cycle, just one manually-set countdown.

The user wants: the device to know the real time (via NTP, once it can reach the internet), a way to actually get it onto their home Wi-Fi (the device already hosts its own AP for settings — extend that same portal to also collect home Wi-Fi credentials), a configurable timezone, a real Pomodoro mode (work → short break → ... → long break, auto-cycling) reachable from a menu, and all of this — timezone, Wi-Fi credentials, Pomodoro durations — persisted across power loss like the existing settings already are.

No hardware RTC is being added (Wi-Fi + NTP only, per user's choice) — so wall-clock time is only as good as the last successful sync; countdown timers (Pomodoro, focus timer) never depended on wall-clock time and are unaffected by sync status.

## Existing code to build on

- `src/main.cpp` — production firmware. Already has: `Settings` struct persisted via `Preferences`/NVS (namespace `"focusdock"`, see `loadSettings()`/`saveSettings()`), a hand-rolled AP-only Wi-Fi settings page (`startWifiPortal()`, `handleRoot()`, `handleSave()`, `settingsPage()`), a debounced `Button` struct, a single free-form H:M:S-less minutes-only timer (`STATE_IDLE/RUNNING/PAUSED/DONE`), manual LED status override (`OVERRIDE_AUTO/FREE/BUSY/FOCUS`, cycled by left/right "anytime").
- `src/test_program_menu.cpp` — unmerged prototype with the menu pattern to reuse: `Program` enum (`PROGRAM_MENU/PROGRAM_TIMER/PROGRAM_AVAILABILITY`), menu-index list screen, H:M:S field-based timer editing (`cycleField`/`adjustField`), a long-press-anywhere-to-go-home gesture. Its physical F/B/L/R → logical direction remap is **specific to that test file's wiring note** and must NOT be ported — `main.cpp`'s buttons are already named/wired by logical direction.
- `platformio.ini` — no changes needed. `WiFi.h`, `WebServer.h`, `Preferences.h` already used; `ESPmDNS.h` and `<time.h>` (for `configTzTime`/`getLocalTime`) are also part of the arduino-esp32 core already pulled in by `framework = arduino` — no new `lib_deps`.

## Design decisions made along the way

- **Availability vs. status-override merge**: the test file's separate `Availability` enum and `main.cpp`'s existing `statusOverride` are two competing sources of LED truth. Resolution: drop the standalone `Availability` enum; the "Available/Busy" menu screen becomes a UI view over `statusOverride` (toggles between `OVERRIDE_FREE`/`OVERRIDE_BUSY`), keeping one variable owning LED color everywhere.
- **Left/Right ownership**: `statusOverride` keeps left/right "anytime" *except* while editing the free-form timer's H/M/S fields (the only screen that genuinely needs both directions itself). Pomodoro has no on-device duration editing (web-page only), so left/right stays free there.
- **LED precedence**: manual `statusOverride` (if not AUTO) always wins, then whichever program is active (Timer countdown bar / Pomodoro phase color), else idle green.
- **Timezone**: IANA-style human labels mapped to POSIX TZ strings server-side (handles DST automatically via newlib), not a raw UTC offset — a small curated dropdown (UTC, London, Berlin/Paris, US Eastern, US Pacific), default UTC.
- **Wi-Fi**: AP stays always-on (so the settings page is never unreachable), STA (home Wi-Fi) runs concurrently via `WIFI_AP_STA`, connects/reconnects through a non-blocking backoff state machine driven from `loop()` — never a blocking `while(...) delay()`.
- **mDNS**: add `http://focusdock.local` reachability once STA joins, alongside the existing `192.168.4.1` AP address.

## Implementation

### 1. Settings additions (`Settings` struct + NVS, `src/main.cpp`)

```cpp
char staSsid[33] = "";
char staPassword[65] = "";
uint8_t tzIndex = 0;                 // index into TZ_OPTIONS[]
uint16_t pomoWorkMin = 25, pomoShortBreakMin = 5, pomoLongBreakMin = 15;
uint8_t pomoSessionsBeforeLong = 4;
```
NVS keys (≤15 chars): `staSsid`, `staPass`, `tzIndex`, `pomoWork`, `pomoShort`, `pomoLong`, `pomoSessN`. Load with sane clamped defaults (e.g. `tzIndex` clamped `< TZ_OPTION_COUNT`).

New CONFIG constants: `LONG_PRESS_MS` (800), Pomodoro defaults/bounds (`POMO_MIN_MINUTES` 1, `POMO_MAX_MINUTES` 180, `POMO_SESSIONS_MIN/MAX` 1/10), `COLOR_POMO_LONG_BREAK` (new, e.g. blue), `NTP_SERVER1/2` (`pool.ntp.org`, `time.nist.gov`), `MDNS_HOSTNAME` (`"focusdock"`), STA backoff/timeout constants, `POMO_PHASE_TRANSITION_MS` (3000).

### 2. Wi-Fi STA state machine (non-blocking)

- `WiFi.mode(WIFI_AP_STA)` in `startWifiPortal()` (AP still starts unconditionally, unchanged behavior).
- `enum StaState { STA_DISABLED, STA_CONNECTING, STA_CONNECTED, STA_BACKOFF }` + globals for attempt count / timestamps.
- `staBegin()`: no-op → `STA_DISABLED` if `staSsid` empty; else `WiFi.begin(...)`, `STA_CONNECTING`.
- `wifiStaTick()`, called every `loop()`: pure `millis()`/`WiFi.status()` state transitions, exponential backoff on failure/drop, calls `onStaConnected()` on first connect (and reconnects).
- `staReset()`: called from `handleSave()` when SSID/password actually changed — disconnects and retries immediately (skip backoff) so a settings save gives instant feedback.

### 3. mDNS

- `#include <ESPmDNS.h>`; in `setup()`, `MDNS.begin(MDNS_HOSTNAME)` + `MDNS.addService("http","tcp",80)`, called once unconditionally — harmless before STA connects, live once it does.

### 4. NTP + timezone

- `const TzOption TZ_OPTIONS[] = {label, posix}` table (UTC, London GMT/BST, Berlin/Paris CET/CEST, US Eastern, US Pacific).
- `applyTimezone()`: `configTzTime(TZ_OPTIONS[settings.tzIndex].posix, NTP_SERVER1, NTP_SERVER2)`, idempotent, called from `onStaConnected()` and from `handleSave()` when `tzIndex` changes.
- `isTimeSynced()`: `getLocalTime(&t, 5)` + sanity check `tm_year+1900 >= 2024` (distinguishes real sync from 1970 epoch default).
- `updateClockCache()`: gated to once/second, writes a cached `"HH:MM"` string or `"--:--"` when not synced — called unconditionally each `loop()` pass, cheap.
- No RTC hardware means: no Wi-Fi/internet at boot → clock shows `"--:--"` and never blocks; countdown timers are untouched since they never read `struct tm`.

### 5. Menu merge (adapt `test_program_menu.cpp` pattern into `main.cpp`)

- `enum Program { PROGRAM_MENU, PROGRAM_TIMER, PROGRAM_POMODORO, PROGRAM_AVAILABILITY }`, `MENU_ITEMS = {"Focus Timer", "Pomodoro", "Available / Busy"}`.
- Extend `main.cpp`'s existing `Button` struct with long-press fields (`pressedAtMs`, `longPressed`, `longFired`) from the test file — applied to all 5 buttons, but only `btnPress.longPressed` is used, as the universal "hold center to go home" gesture (checked first in `handleButtons()`, before program-specific dispatch).
- Adopt the H:M:S field-based timer editing (`setHours/setMinutes/setSeconds`, `selectedField`, `cycleField`/`adjustField`) from the test file in place of `main.cpp`'s minutes-only `STATE_IDLE`; fresh entry pre-fills from `settings.focusMinutes`. Keep the 8-LED countdown bar (adjust the test file's ceil-division math for `NUM_LEDS=8` instead of 10).
- Rewrite `statusColor()`/`updateLeds()` precedence: override (if not AUTO) → active program's color → idle green.
- Availability screen: toggles `statusOverride` between FREE/BUSY (see design decision above), no separate enum.

### 6. Pomodoro state machine (new)

```cpp
enum PomoRunState { POMO_IDLE, POMO_RUNNING, POMO_PAUSED, POMO_PHASE_DONE };
enum PomoPhase    { PHASE_WORK, PHASE_SHORT_BREAK, PHASE_LONG_BREAK };
```
- `pomoStart(phase)` / `pomoTick()` (mirrors existing `tickTimer()`) / `pomoAdvancePhase()`: work session increments `pomoSessionCount`; reaching `pomoSessionsBeforeLong` triggers a long break and resets the counter; any break always returns to `PHASE_WORK`.
- `POMO_PHASE_DONE` blinks briefly (`POMO_PHASE_TRANSITION_MS`, reusing `DONE_BLINK_MS` cadence) then auto-advances into the next phase running — delivers the auto-cycling requirement without manual restarts. Any button press skips the wait.
- Long-press-home abandons the whole cycle (`POMO_IDLE`, session count reset).
- LED colors: work = `COLOR_FOCUS` (red), short break = `COLOR_FREE` (green), long break = `COLOR_POMO_LONG_BREAK` (blue), paused = `COLOR_BUSY` (amber).
- `drawPomodoro()`: phase name, MM:SS countdown, "Session N/M" counter, contextual hint line — following `drawTimer()`'s existing layout conventions.

### 7. Settings web page (`settingsPage()` / `handleSave()`)

- New fields: Wi-Fi SSID (text) + password (password input, **never echoed back** into the rendered form — only overwritten in `handleSave()` if non-empty, so re-saving other settings doesn't blank a working password); timezone `<select>` from `TZ_OPTIONS`; 4 Pomodoro number inputs (work/short/long minutes, sessions-before-long-break).
- `handleSave()`: validate/`constrain()` all new fields, detect if SSID/password actually changed → `staReset()`; detect if `tzIndex` changed → `applyTimezone()` immediately; `saveSettings()` persists everything through the existing pattern. Response notice can note "(reconnecting Wi-Fi…)" when relevant — non-blocking, page still returns immediately.

### 8. Sequencing (each step flashable/testable on its own)

1. STA join + AP/STA concurrency + backoff state machine (no menu/Pomodoro/NTP yet) — verify AP still reachable at `192.168.4.1`, device joins home Wi-Fi, empty SSID stays harmlessly idle.
2. mDNS — verify `http://focusdock.local` resolves once STA is joined.
3. NTP + timezone + clock rendering on the existing (pre-menu) screen — verify sync, `"--:--"` fallback with no Wi-Fi, correct DST behavior for a couple of timezones.
4. Menu merge (Program enum, long-press-home, H:M:S timer, Availability, LED precedence rewrite) — verify navigation, all 3 screens, long-press-home from each, confirm steps 1–3 still work.
5. Pomodoro (state machine, settings fields, web form, `drawPomodoro()`) — verify with short test durations (e.g. 1/1/1 min, sessions=2) that a full work→short→work→long cycle runs correctly, colors/session-counter/persistence all check out.
6. Polish — update `README.md` (usage table, roadmap items now done), full regression pass.

## Verification

- Flash after each numbered step in §8 via `make flash PORT=...` and confirm that step's behavior on-device before moving to the next (serial monitor shows Wi-Fi/NTP state transitions; OLED shows menu/timer/Pomodoro screens; LEDs match documented colors).
- End-to-end: join device AP → set home Wi-Fi + timezone + Pomodoro durations via `http://192.168.4.1` (or `http://focusdock.local` once STA is up) → power-cycle the device → confirm Wi-Fi reconnects, clock re-syncs, and all settings (including Pomodoro durations/timezone) survived the power loss.
- Confirm no `delay()`-based blocking was introduced anywhere in the Wi-Fi/NTP path — buttons and display must stay responsive throughout connect/backoff/sync.
