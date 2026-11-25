# Demeter controller API

Base URL: `http://demeter.local` (mDNS) or `http://<feather-ip>`.

Every API response is JSON and includes `"ok": true` or `"ok": false`. Errors also include `"error"` (a stable code) and `"message"`. CORS allows any origin, methods `GET, POST, PUT, DELETE, OPTIONS`, and headers `Content-Type`, `Authorization`, and `X-Api-Key`.

If an API token is set, send `Authorization: Bearer <token>` or `X-Api-Key: <token>`. Serial commands skip the token because they require physical access. An empty token leaves the HTTP API open on the LAN.

Units:

- pH is pH.
- `ecMin` / `ecMax` / `readings.ec.mS` are mS/cm.
- `readings.ec.uS` is µS/cm.
- `readings.ec.ppm` and `tdsMinPpm` / `tdsMaxPpm` are 500-scale ppm from the SEN0244. `mS/cm = ppm / ppmPerMs` with `ppmPerMs` default 500.
- Light clock times are local, after the configured UTC offset. `lightOnMinute` is minutes from midnight (0–1439). `photoperiodHours` is 0–24.
- Pump volume is milliliters. Flow is liters per minute.

## Discovery

```http
GET /api/v1
GET /api/v1/help
```

mDNS name: `demeter.local`. Services: `_http._tcp` and `_demeter._tcp` on port 80.

## Status and readings

```http
GET /api/v1/status
GET /api/v1/readings
GET /api/v1/history
GET /api/v1/doses
GET /api/v1/faults
```

`GET /api/v1/status` is the dashboard document:

```json
{
  "ok": true,
  "device": "demeter",
  "version": "1.0.0",
  "mode": "auto",
  "uptimeMs": 12000,
  "simulation": false,
  "time": {"synced": true, "unix": 1700000000, "minuteOfDay": 480, "yyyymmdd": 20231114, "tzOffsetMinutes": 0, "dstOffsetMinutes": 0},
  "wifi": {"ssid": "grow", "connected": true, "ip": "192.168.1.50", "rssi": -48, "mdns": "demeter.local"},
  "readings": {
    "ph": {"value": 6.2, "valid": true, "reason": "ok", "volts": 1.6, "ageMs": 40},
    "ec": {"mS": 1.4, "uS": 1400, "ppm": 700, "valid": true, "saturated": false, "reason": "ok", "volts": 1.1, "ageMs": 40},
    "waterTempC": {"value": 22.0, "valid": true, "ageMs": 800},
    "flowLpm": {"value": 4.2, "valid": true},
    "level": "ok"
  },
  "nutrientSensor": {"model": "SEN0244", "kind": "tds", "maxPpm": 1000, "ppmPerMs": 500, "maxEc": 2.0, "ecFromTds": "mS/cm = ppm / ppmPerMs"},
  "targets": {"profile": "lettuce", "phMin": 6.0, "phMax": 7.0, "ecMin": 1.2, "ecMax": 1.8, "tdsMinPpm": 600, "tdsMaxPpm": 900, "lightOn": "06:00", "lightOnMinute": 360, "photoperiodHours": 16, "lightOffMinute": 1320},
  "circulation": {"desired": true, "confirmed": true, "reachable": true, "configured": true, "host": "192.168.1.40"},
  "lights": {"desired": true, "commanded": true, "confirmed": true, "reachable": true, "configured": true, "host": "192.168.1.41", "onMinute": 360, "offMinute": 1320, "photoperiodHours": 16},
  "dosing": {"running": false, "pump": null, "mixRemainingMs": 0, "block": "", "nextNutrient": "nutrient_a"},
  "faults": []
}
```

`readings.level` is `"ok"` or `"low"`. `lights.commanded` is false in auto mode until the clock is synced, so the Shelly light schedule is left alone. `dosing.block` is the reason the auto loop did not start a dose (`mixing`, `startup`, `low_water`, `ec_invalid`, `tds_uncalibrated`, `ec_high`, and the other codes below). An empty string means the loop is inside the target band.

`GET /api/v1/history` returns up to 60 samples, about one per minute, oldest first. `GET /api/v1/doses` returns the last 16 doses.

Fault codes:

| Code | Meaning |
| --- | --- |
| `low_water` | Float says the reservoir is low. Circulation and dosing are held off |
| `no_flow` | Plug is on but flow stayed under `noFlowLpm`. Latched until acknowledged or flow returns |
| `ph_invalid` / `ec_invalid` / `temp_invalid` | Missing, stale, or out of range |
| `ph_uncalibrated` / `tds_uncalibrated` | Calibration required before that automatic dose |
| `pump_uncalibrated` | The pump the loop wanted has no mL/sec calibration |
| `ec_high` | EC is above the band. Nutrients are not added. The loop cannot dilute |
| `ec_saturated` | TDS probe is at the top of its range |
| `daily_limit` | A pump used its daily volume |
| `shelly_pump_unconfigured` / `shelly_lights_unconfigured` | No plug host yet |
| `shelly_pump_unreachable` / `shelly_lights_unreachable` | Host is set and not answering |
| `time_unsynced` | No NTP time yet |
| `startup` | Startup hold, so the first samples can settle |
| `mixing` | Mix lockout after a dose |
| `loop_stall` | A loop pass was very late |

```http
POST /api/v1/faults/ack
```

Clears a latched no-flow fault and restarts its timer.

## Targets and profiles

```http
GET /api/v1/targets
PUT /api/v1/targets
GET /api/v1/profiles
POST /api/v1/profiles/apply
PUT /api/v1/profiles/{name}
DELETE /api/v1/profiles/{name}
POST /api/v1/profiles/{name}/reset
```

`PUT /api/v1/targets` accepts any subset:

```json
{"phMin": 5.8, "phMax": 6.2, "ecMin": 1.0, "ecMax": 1.6, "lightOn": "06:00", "photoperiodHours": 16}
```

`lightOnMinute` may be sent instead of `lightOn`. If `ecMin` and `ecMax` are omitted and both `tdsMinPpm` and `tdsMaxPpm` are sent, they are converted with `ppmPerMs`. Changing a number without sending `profile` renames the active profile to `custom`. `ecMax` above the probe ceiling (2.0 mS/cm at the default scale) returns `ec_above_sensor_range`.

```http
POST /api/v1/profiles/apply
{"name": "lettuce"}
```

Built-ins:

| Name | pH | EC (mS/cm) | Light |
| --- | --- | --- | --- |
| lettuce | 6.0–7.0 | 1.2–1.8 | 16 h from 06:00 |
| basil | 5.5–6.0 | 1.0–1.6 | 16 h from 06:00 |

`PUT /api/v1/profiles/lettuce` with a target body stores an override. `POST /api/v1/profiles/lettuce/reset` restores the built-in. Custom names are lowercase letters, digits, `_`, and `-`, up to four saved profiles. `DELETE` removes a custom profile and rejects the built-ins.

Profiles and targets are stored in NVS.

## Calibration

```http
GET /api/v1/calibration
POST /api/v1/calibration/ph
POST /api/v1/calibration/ph/clear
POST /api/v1/calibration/tds
POST /api/v1/calibration/tds/clear
POST /api/v1/calibration/pump
```

```json
{"ph": 7.0}
{"ph": 4.0, "volts": 2.0, "tempC": 25}
{"ppm": 707}
{"pump": "ph_up", "mlPerSec": 1.25, "invert": false}
```

When `volts` or `tempC` is omitted, the live sample is used. pH keeps up to three points. A new point within 0.25 pH of an existing one replaces it. The pump id is `ph_up`, `ph_down`, `nutrient_a`, or `nutrient_b`.

## Mode and manual actions

```http
POST /api/v1/mode
{"mode": "service"}
```

`auto` runs the schedule and the dosing loop. `service` holds the current circulation and light commands and disables automatic dosing.

Manual routes require service mode (`service_mode_required`, HTTP 409) and still enforce interlocks:

```http
POST /api/v1/actions/dose
{"pump": "ph_up", "ml": 1.0}

POST /api/v1/actions/prime
{"pump": "nutrient_a", "seconds": 10}

POST /api/v1/actions/circulation
{"on": true}

POST /api/v1/actions/lights
{"on": false}

POST /api/v1/actions/estop
{}
```

`estop` enters service mode, stops the dosing pump, and turns circulation off. Lights stay as they were.

Other 409 codes: `low_water`, `pump_busy`, `daily_limit`, `pump_not_configured`. `invalid_pump`, `invalid_amount`, and `invalid_calibration` are HTTP 400.

Only one dosing pump runs. A low-water event aborts the pulse. Circulation cannot be turned on while the float says the reservoir is low.

## Schedules

```http
GET /api/v1/schedules
```

Returns the Shelly timespecs the firmware writes to the plugs (`Switch.Set` on switch id 0). Light events follow the photoperiod. The circulation fallback is `0 0 * * *` (on at the top of every hour) so the pump recovers if the Feather goes offline. Shelly timespec format is `seconds minutes hours day-of-month weekday`.

The firmware calls the plug at `http://<host>/rpc` with `Switch.Set`, `Switch.GetStatus`, `Schedule.DeleteAll`, `Schedule.Create`, and `Sys.SetConfig` for the timezone. Leave RPC authentication off on the two dedicated plugs, or the controller cannot sign the request. Hosts are IP addresses or hostnames with no scheme.

## Config, Wi-Fi, and simulation

```http
GET /api/v1/config
PUT /api/v1/config
POST /api/v1/wifi
POST /api/v1/simulation
```

`GET /api/v1/config` reports whether a Wi-Fi password and token are set. It does not return them.

`PUT /api/v1/config` fields (all optional):

| Field | Notes |
| --- | --- |
| `shellyPumpHost`, `shellyLightsHost` | Hostname or IP. Empty string clears it |
| `tzOffsetMinutes` | Local time minus UTC, in minutes. `-480` is US Pacific standard |
| `dstOffsetMinutes` | Extra daylight offset, 0–120 |
| `floatActiveLow` | Default true: pin low means low water |
| `flowPulsesPerLiter` | Default 450 |
| `ppmPerMs` | Default 500. Range 300–800 |
| `useFactoryTds` | Allow nutrient dosing on the factory TDS curve |
| `maxDoseMl`, `minDoseMl`, `phGainMlPerPh`, `ecGainMlPerMs`, `dailyLimitMl` | Dose shaping. Defaults 2 mL, 0.3 mL, 4 mL/pH, 8 mL per mS/cm, 50 mL/day |
| `mixLockoutSec` | Default 120 |
| `noFlowLpm`, `noFlowDelaySec` | Default 0.5 L/min for 15 s |
| `apiToken` | 8–48 letters, digits, `.`, `_`, `-` |
| `clearApiToken` | true clears it |

```http
POST /api/v1/wifi
{"ssid": "grow", "password": "secret"}
```

The controller stores the credentials and reconnects. The setup AP form posts the same fields to `POST /setup/wifi` as form data.

```http
POST /api/v1/simulation
{"enabled": true, "ph": 6.2, "ecMs": 1.1, "waterTempC": 22, "flowLpm": 4, "level": "ok"}
```

`level` is `ok` or `low`. Omitted fields stay as they were. This is not saved.

## Serial

115200 8N1, one command per line, one JSON response per line. The JSON line form is the same API:

```json
{"method":"POST","path":"/api/v1/profiles/apply","body":{"name":"basil"}}
```

Text commands:

```text
help
status
readings
targets
faults
doses
history
config
profiles
schedules
mode auto
mode service
estop
ack
apply lettuce
set ph 6.0 7.0
set ec 1.2 1.8
set light 06:00 16
set shelly pump 192.168.1.40
set shelly lights 192.168.1.41
dose ph_up 1.0
prime nutrient_a 10
pump on
pump off
lights on
lights off
cal ph 7
cal ph clear
cal tds 707
cal tds clear
cal pump ph_up 1.25
cal pump ph_down 1.25 invert
wifi grow secret
token set token-1234
token clear
sim on
sim off
sim ph 6.2
sim ec 1.1
sim temp 22
sim flow 4
sim level low
```

`wifi` takes one SSID token and one password token. Use the JSON form when either contains spaces.

## Examples

```bash
curl -s http://demeter.local/api/v1/status
curl -s -X POST http://demeter.local/api/v1/profiles/apply -H 'Content-Type: application/json' -d '{"name":"basil"}'
curl -s -X PUT http://demeter.local/api/v1/targets -H 'Content-Type: application/json' -d '{"phMin":5.8,"phMax":6.2}'
curl -s -X POST http://demeter.local/api/v1/mode -H 'Content-Type: application/json' -d '{"mode":"service"}'
curl -s -X POST http://demeter.local/api/v1/actions/dose -H 'Content-Type: application/json' -d '{"pump":"ph_up","ml":1}'
```
