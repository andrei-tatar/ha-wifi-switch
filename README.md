# ha-wifi-switch

ESP32 firmware for home automation Wi-Fi wall switches, controlled over MQTT. All devices run the same binary, and a JSON config stored on each device sets its role:

| Type | Hardware | Touch keys |
| --- | --- | --- |
| `dimmer` | Triac dimmer | Any key toggles the light |
| `switch` | Up to 3 on/off outputs | Each key toggles its own output |
| `blinds` | Open and close motor outputs | Second key opens, third key closes, any key stops the motor while it's moving |

Input comes from a QT1070 capacitive touch chip or plain GPIO inputs. The target board is `wemos_d1_mini32`.

## Building and flashing

You need [PlatformIO](https://platformio.org/).

```sh
pio run                                          # build
VERSION=1.2.3 pio run                            # build with a set version (default: from git describe)
pio run -t upload --upload-port <device>.local   # flash over the network
pio device monitor                               # serial console, 115200 baud
```

The firmware is written to `.pio/build/esp32/ha_switch_esp32.bin`. Publishing a GitHub release builds it and attaches it to the release.

## Setting up a new device

1. Flash the device over serial. Network flashing is the default, so change `upload_protocol` in `platformio.ini` for this step.
2. Make sure Wi-Fi credentials are already stored in the device's flash. The firmware has no provisioning code and calls `WiFi.begin()` without credentials.
3. Send the device a config (see below). Changing the type, pins, hostname, or MQTT settings reboots it automatically.

## Configuration

Config is a JSON document stored on the device and managed through the HTTP API:

```sh
curl http://<device>.local/api/config
curl -X POST --data @config.json http://<device>.local/api/config
```

**A POST replaces the entire config.** Include `mdns` and `mqtt` in what you send, or the device loses them. A body that isn't a JSON object gets a 400 and the stored config is kept.

Example dimmer config:

```json
{
  "type": "dimmer",
  "mdns": { "host": "switch-dimmer-2" },
  "mqtt": { "host": "192.168.1.10", "user": "ha", "password": "secret" },
  "pins": {
    "qt": { "sda": 33, "scl": 32, "ch": [2, 0, 5] },
    "led": [21, 19, 22],
    "redLed": 18
  },
  "dimmer": {
    "pins": { "zero": 25, "triac": 26 }
  }
}
```

### Common options

| Key | Default | Description |
| --- | --- | --- |
| `type` | | `dimmer`, `switch`, or `blinds` |
| `mdns.host` | `ha-switch` | Hostname, also used in MQTT topics and as the client ID |
| `mdns.enabled` | `true` | Enable mDNS |
| `ota.enabled` | `true` | Enable network flashing |
| `ota.password` | none | Password for network flashing |
| `mqtt.host`, `mqtt.user`, `mqtt.password` | | Broker address and login. MQTT stays off unless all three are set |
| `mqtt.port` | `1883` | Broker port |
| `mqtt.prefix` | `ha-switch` | Topic prefix |
| `pins.qt` | | QT1070 touch: `sda`, `scl`, and `ch` (chip channel for each key) |
| `pins.input` | | GPIO inputs, up to 3, active low. Used only if `pins.qt` isn't set |
| `pins.led` | | Blue LEDs, up to 3 |
| `pins.redLed` | | Red LED |
| `pins.redLedInvert` | `false` | Make the red LED active low |

### Dimmer options (`dimmer`)

| Key | Default | Description |
| --- | --- | --- |
| `pins.zero`, `pins.triac` | | Zero-cross input and triac output. Both must be RTC-capable GPIOs, unless `onOffOnly` is set |
| `onOffOnly` | `false` | For boards without the dimming changes: hold the triac output high or low, without using the zero-cross input. Changing it reboots the device |
| `curve` | built in | Exactly 100 trigger delays in µs. `lib/dimmer/curve.js` generates these |
| `levels.on.blue`, `levels.on.red`, `levels.on.touch` | `20`, `0`, `255` | LED levels while on |
| `levels.off.blue`, `levels.off.red`, `levels.off.touch` | `0`, `20`, `255` | LED levels while off |

Dimming timing assumes 50 Hz mains and a modified dimmer board. The required changes are listed at the top of `lib/dimmer/dimmer.cpp`.

With `onOffOnly`, the light switches straight on or off, and `curve` and the brightness level have no effect on it. `brightness` is still accepted and reported in `state`. `brightness` with `for` keeps the light fully on for that time.

### Switch options (`switch`)

| Key | Default | Description |
| --- | --- | --- |
| `pins` | | Output pins, up to 3 |
| `resetAfter` | `0` | Turn outputs off after this many seconds; `0` disables it |
| `levels.on.blue`, `levels.on.red` | `20`, `0` | LED levels while on |
| `levels.off.blue`, `levels.off.red` | `0`, `20` | LED levels while off |
| `levels.touch` | `255` | Blue LED level while touched |

### Blinds options (`blinds`)

| Key | Default | Description |
| --- | --- | --- |
| `pins.open`, `pins.close` | | Motor outputs |
| `travel` | `40000` | Full travel time in ms |
| `openCloseDelta` | `1500` | How much faster closing is than opening, in ms over full travel |
| `delay` | `500` | Pause in ms after stopping the motor, before it can change direction |
| `levels.touch`, `levels.red`, `levels.changing` | `255`, `20`, `80` | LED levels |

There are no position sensors. Position is estimated from motor run time, and fully opening or closing runs 1 s longer to re-home.

### Managing a fleet

`util/config/` holds one config per device, named `<hostname>.json`. To push them all:

```sh
node util/apply_config.mjs   # Node 18+, run from the repo root
```

For each file, the script connects to `<hostname>.lan`, sets `mdns.host` from the filename, and copies the `mqtt` settings from the device, which keeps credentials out of the repo. Devices that already match are skipped, and the rest are updated and rebooted.

## MQTT

Topics are `<prefix>/<host>/<topic>`:

| Topic | Direction | Purpose |
| --- | --- | --- |
| `state` | Device publishes, retained | Current state as JSON, sent on change and every 5 minutes |
| `state/set` | Device subscribes | Commands as JSON |
| `online` | Device publishes, retained | `true`, or `false` through the last will |
| `version` | Device publishes | Firmware version |
| `reset-reason` | Device publishes | Reset reason code, sent on the first connection after boot |
| `debug` | Device publishes | Details of messages that failed to parse |

After booting, a device reads its own retained `state` to restore what it was doing, then starts listening on `state/set`. Blinds restore their position and resume a move that the reboot interrupted. Switch outputs that were on a timer (`for` or `resetAfter`) stay off, because the timer doesn't survive a reboot. While any are running, `state` includes `autoOff`, a bitmask of those outputs.

### Commands

```sh
mosquitto_pub -h <broker> -t ha-switch/<host>/state/set -m '{"on":true,"brightness":60}'
```

| Type | Field | Description |
| --- | --- | --- |
| all | `suspendInputs` | `true` stops touch events, while LEDs still respond to touch |
| `dimmer` | `on` | Turn on or off |
| `dimmer` | `brightness` | 1 to 100 |
| `dimmer` | `for` | Used with `brightness`: hold that level as a minimum for this many seconds, even while off |
| `switch` | `on` | `true` or `false` for all outputs, or an array with one value per output, such as `[true,false]`. `null` leaves that output as it is |
| `switch` | `for` | Used with `on`: turn the outputs it switches on back off after this many seconds. Each output has its own timer, and setting an output replaces it. Without `for`, the output uses `resetAfter`. `0` means no timer |
| `blinds` | `openPercent` | 0 (closed) to 100 (open) |

## HTTP API

The API has no authentication, so keep devices on a trusted network. `GET /api/config` also returns the stored MQTT password.

| Endpoint | Description |
| --- | --- |
| `GET /api/status` | Version, uptime, free heap, Wi-Fi, MQTT, input readings, and current state |
| `GET /api/config` | Read the stored config |
| `POST /api/config` | Replace the stored config |
| `POST /api/reboot` | Reboot the device |

## Project layout

| Path | Contents |
| --- | --- |
| `src/main.cpp` | Connects the components and applies the config |
| `lib/switch-common` | Wi-Fi, mDNS, OTA, and MQTT |
| `lib/switch-dimmer`, `lib/switch-onoff`, `lib/switch-blinds` | Behaviour for each type |
| `lib/io`, `lib/qt1070` | Touch and GPIO input, LEDs |
| `lib/dimmer` | Triac control, running on the ULP coprocessor |
| `lib/web`, `lib/configuration` | HTTP API and config storage |
| `util/` | Per-device configs and the script that applies them |
