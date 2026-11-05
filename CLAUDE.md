# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

PlatformIO/Arduino firmware for ESP32 (`wemos_d1_mini32`) wall switches, controlled over MQTT. "ha" in the project name and the `ha-switch` prefix means home automation; the project has no link to Home Assistant. One binary serves every device. A JSON config stored on the device selects its role at runtime: `dimmer`, `switch` (on/off relays), or `blinds`.

## Commands

If `pio` isn't on `PATH`, it's at `~/.platformio/penv/bin/pio`.

```sh
pio run                                                # build (the only check CI runs)
VERSION=1.2.3 pio run                                  # build with a version baked in (default 0.0.1)
pio run -t upload --upload-port <device>.local         # flash over the network (OTA)
pio device monitor                                     # serial console, 115200 baud
node util/apply_config.mjs                             # push util/config/*.json to devices (run from repo root)
```

There are no tests and no linter, so a successful `pio run` is the only automated verification. Behaviour can only be confirmed on hardware.

`pio run -t upload` and `apply_config.mjs` act on real installed devices (mains loads, motors), and `apply_config.mjs` touches every device whose config differs and reboots it. Only run them when asked.

Flashing defaults to `espota` at `ha-switch.local`, the fallback hostname, so configured devices need `--upload-port`. Serial flashing requires changing `upload_protocol` in `platformio.ini`.

Publishing a GitHub release builds the firmware and attaches `ha_switch_esp32.bin` to it (`.github/workflows/release.yml`).

## Architecture

### Wiring

`src/main.cpp` creates one global `Io`, `SwitchCommon`, and one of each switch type, and connects them with callbacks. All three switch types always exist, but only the one matching `type` gets `configure()` called. The others stay `_initialized = false`, and their `appendState`/`updateState` are no-ops, so `main.cpp` calls all three unconditionally. A new type extends `SwitchBase` and is added at each of those call sites.

Code in `lib/<name>/` is PlatformIO private libraries, included by bare header name (`"io.h"`).

| Library | Role |
| --- | --- |
| `switch-common` | Wi-Fi reconnect, mDNS, OTA, MQTT client and state sync; calls `Io` setup |
| `switch-base` | Base class with state-change throttling (500 ms) and suspend/resume batching |
| `switch-dimmer`, `switch-onoff`, `switch-blinds` | Role behaviour: touch handling, LED levels, state JSON |
| `io` | 3 channels: touch (`qt1070` over I2C) or GPIO inputs, debounce, LED PWM |
| `dimmer` | Triac phase control via the ULP coprocessor |
| `web` | HTTP API: `GET /api/status`, `GET`/`POST /api/config`, `POST /api/reboot` |
| `configuration` | Config JSON string in NVS (`Preferences`) |

### Execution model

`loop()` only runs `ArduinoOTA.handle()` and then sleeps for 50 ms so the CPU can idle. Everything else runs in `Ticker` callbacks and async web/MQTT callbacks. Tickers share the `esp_timer` task, so a blocking call in one stalls the others, including the 5 ms `Io` poll. MQTT callbacks run on the esp-mqtt task while it holds its API lock, and `publish` waits for that lock, so a role that shares state between the two must not publish while holding its own lock (see `SwitchBlinds::_lock`).

### Configuration lifecycle

- `POST /api/config` replaces the whole stored JSON (no patching) and calls `applyConfiguration(false)` without rebooting. A body that isn't a JSON object is rejected with 400 and nothing is stored.
- `configure()` therefore runs more than once. One-time setup is guarded by `_initialized`, while tunables such as LED levels are re-read on each call.
- `configure()` returns `true` if a change only takes effect after restart (pins, hostname, `type`, MQTT settings), which schedules a reboot after 1.5 s.
- Keep this contract when adding options: `Io::begin` and the role setup run once, so pin changes must request a reboot.

`util/config/<device>.json` holds each device's config. The filename is the device's hostname. `apply_config.mjs` connects to `<device>.lan`, sets `mdns.host` from the filename, and copies `mqtt` from the device's current config, so these files deliberately contain neither `mdns` nor MQTT credentials. It then skips devices whose config already matches.

There is no Wi-Fi provisioning code: `WiFi.begin()` uses credentials already stored in flash.

### MQTT state protocol

Topics are `<prefix>/<host>/...`, where `prefix` defaults to `ha-switch`. MQTT is only enabled if `host`, `user`, and `password` are all set.

| Topic | Direction | Purpose |
| --- | --- | --- |
| `state` | publish, retained | Current state; also republished every 300 s |
| `state/set` | subscribe | Commands; the device clears the retained message after acting on it |
| `online` | publish, retained | `true`, or `false` via last will |
| `version`, `reset-reason`, `debug` | publish | Diagnostics |

Empty payloads are ignored, because clearing `state/set` echoes one back.

**State restore on boot:** the device subscribes to its own retained `state` first. It applies that message with `isFromStoredState = true`, then unsubscribes and moves to `state/set`. If nothing arrives within 5 s of connecting, it does the same switch and publishes its current state. `SwitchBlinds` uses this to restore its position without re-homing and to resume a move that a reset interrupted, which is deliberate because blinds devices can reset when the motor starts. `SwitchOnOff` leaves outputs off if they were on a timer (`resetAfter`, or marked in the state's `autoOff` bitmask), because the timer doesn't survive a reboot.

Command fields by role (`suspendInputs` works on all of them; it stops touch events but keeps LED feedback):

- **dimmer:** `on`, `brightness`, and `for` (with `brightness`: hold it as a minimum for N seconds)
- **switch:** `on` (bool, or an array with one entry per configured pin), `for` (auto-off seconds)
- **blinds:** `openPercent`

### Hardware details

- **Dimmer:** a ULP program in `dimmer.cpp` does zero-cross detection and triac firing. The main CPU only writes the delay to `RTC_SLOW_MEM[Mem_Delay]` every 10 ms while ramping brightness. Both pins must be RTC-capable GPIOs, and `usePins` silently ignores others.
- **On/off dimmers:** `onOffOnly` is for boards without the dimming changes. The ULP isn't loaded and the zero pin is ignored; a 10 ms ticker holds the triac pin high or low. `begin` calls `rtc_gpio_deinit` first, because a software reset keeps the RTC routing from the previous boot.
- **Mains and hardware assumptions:** timing assumes a 10 ms half-cycle (50 Hz) and a modified dimmer board (see the comment at the top of `dimmer.cpp`).
- **Brightness curve:** 100 delay values in µs, generated by `lib/dimmer/curve.js`. It can be overridden with a config `curve` array of exactly 100 entries.
- **Blinds:** there are no position sensors. Position is estimated from motor run time in ms (`travel`, `openCloseDelta`), and full open/close commands overrun by 1 s to re-home.
- **LEDs:** blue channels are active-low (`255 - level`). Red is active-high unless `redLedInvert` is set.
- **Touch timing:** `switch` allows simultaneous keys on the QT1070 (GPIO inputs always register one at a time) with no pause after release. Other types use one key at a time with a 150 ms ignore period after release.
