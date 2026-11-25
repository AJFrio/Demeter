# Demeter controller firmware

Firmware for the Adafruit ESP32 Feather V2 (BOM C-01). It doses four peristaltic pumps, switches the circulation pump and grow lights through Shelly Plug US Gen4 outlets, and reads pH, nutrient strength, water temperature, flow, and the low-water float.

The closed-loop logic, crop presets, and the HTTP/serial API live in `lib/demeter_core` and are covered by host unit tests. The sketch under `src/` is the Feather binding: pins, Wi-Fi, Shelly RPC, and NVS.

## Nutrient sensor

The current electronics BOM measures nutrients with the DFRobot SEN0244 analog TDS probe (0–1000 ppm) and a DS18B20, not a wide-range EC probe. The controller converts TDS to EC with the 500-scale (NaCl) factor:

```text
ppm = mS/cm * ppmPerMs
mS/cm = ppm / ppmPerMs
```

`ppmPerMs` defaults to 500, so the trustworthy range is about **0–2.0 mS/cm**. Lettuce (1.2–1.8) and basil (1.0–1.6) sit inside that range. Targets above the probe ceiling are rejected.

## Pinout

| Function | Feather | GPIO | Notes |
| --- | --- | --- | --- |
| pH SEN0169-V2 signal | A2 | 34 | ADC1, input only, 0–3 V |
| TDS SEN0244 signal | A3 | 39 | ADC1, input only, 0–2.3 V |
| DS18B20 data | D32 | 32 | 4.7 kΩ pull-up to 3.3 V |
| Float switch | D33 | 33 | Other side to GND. Internal pull-up. Active-low means low water unless `floatActiveLow` is changed |
| Flow SEN0217 | D27 | 27 | Rising edges, 450 pulses/L. 3.3 V pull-up. Do not feed 5 V into the pin |
| pH-up pump PPM | A5 | 4 | 50 Hz servo-style PPM |
| pH-down pump PPM | A1 | 25 | PPM |
| Nutrient A PPM | A0 | 26 | PPM |
| Nutrient B PPM | MI | 21 | PPM |
| Status LED | D13 | 13 | Onboard LED. Slow blink on Wi-Fi, fast blink while disconnected |
| I2C | SDA / SCL | 22 / 20 | Left free for STEMMA QT |
| Battery sense | A13 | 35 | Feather divider. Not used |

GPIO 12 is a boot strap and is left unused. GPIO 14 and 15 can glitch during boot, so they are not pump outputs. Common the Feather ground with the 12 V pump supply. Pump motor power comes from the 12 V adapter, not from the Feather. Logic `+` on each pump board is 3.3 V from the Feather.

## Wiring

1. Share ground between the Feather, the 12 V pump supply, and every sensor.
2. pH and TDS boards: 3.3 V, GND, and the analog signal to A2 and A3.
3. DS18B20: red to 3.3 V, black to GND, yellow/data to D32, plus a 4.7 kΩ resistor from data to 3.3 V. Check the probe's wire colors.
4. Float switch: one wire to D33, the other to GND. Mount it so the contact closes when the reservoir is low, or flip `floatActiveLow` after checking.
5. Flow sensor: power at 3.3 V if the module allows it, otherwise power the sensor from 5 V and level-shift or pull the open-collector output up to 3.3 V only. Signal to D27.
6. Each DFR1261: PPM signal to the GPIO above, logic power from 3.3 V, motor jack on the shared 12 V supply. Dose one pump at a time; the firmware enforces that.
7. Circulation pump plugs into Shelly plug 1. The four Barrina bars plug into Shelly plug 2 (leave the bar switches on). Both plugs join the same LAN as the Feather.

Power the 12 V pumps after the Feather is running when you can. Setup drives a stop pulse (1500 µs) before it does anything else, and a hardware timer cuts a dose off even if the network stack stalls.

## Build and flash

```bash
cd firmware
python3 -m pip install platformio
pio test -e native
pio run -e featheresp32v2
pio run -e featheresp32v2 -t upload
pio device monitor -b 115200
```

The board id is `adafruit_feather_esp32_v2`. USB serial is 115200 8N1. `pio test -e native` runs the dosing, calibration, schedule, and API tests on the host. No board is required for those.

## Wi-Fi

The controller joins the SSID stored in NVS and advertises `demeter.local` (`_http._tcp` and `_demeter._tcp` on port 80). With no SSID, or if the join fails for 20 seconds, it opens a setup AP:

- SSID: `Demeter-Setup`
- Password: `demeter-setup`
- Page: `http://192.168.4.1/`

Serial can set the network too: `wifi <ssid> <password>`. Passwords or SSIDs with spaces need the JSON form in [docs/API.md](docs/API.md).

## Control behavior

- Targets come from the API. Built-in presets are **lettuce** (pH 6.0–7.0, EC 1.2–1.8 mS/cm, 16 h from 06:00) and **basil** (pH 5.5–6.0, EC 1.0–1.6 mS/cm, 16 h from 06:00). Sources for the defaults are the ranges cited on ENG-77.
- Automatic dosing corrects EC first, then pH, with a small proportional dose, one pump at a time, then a mix lockout (default 120 s).
- Nutrient A and B alternate. `nutrientBPerA` scales the B dose (default 1).
- The controller cannot dilute. EC above the band raises `ec_high` and skips nutrient dosing. pH correction still runs.
- A stale, out-of-range, or uncalibrated reading skips dosing. pH needs two or three points. TDS needs a one-point scale unless `useFactoryTds` is true.
- Low water forces the circulation plug off, blocks new doses, and aborts a dose already running.
- No flow while the circulation plug is confirmed on latches `no_flow` and turns the plug off in auto mode. Acknowledge the fault to try again.
- Daily volume limits reset at local midnight after NTP sync.
- Service mode freezes automation and allows manual pump, light, and dose commands. Interlocks still apply.
- Until NTP sync, auto mode does not write the light plug, so the Shelly fallback schedule stays in charge.
- Shelly fallback: circulation is turned on at the top of every hour. Lights get an on event and an off event matching the photoperiod. Low-water protection needs the Feather online; a dead controller will not hold the pump off forever.
- The task watchdog is 15 seconds. Dose length is enforced by an `esp_timer` callback, not only by the main loop.
- Config, calibration, Wi-Fi, and dose totals are stored in NVS and restored after reboot. An in-progress dose does not resume.

## Calibration

Put the controller in service mode first (`mode service` on serial, or `POST /api/v1/mode`).

pH, two or three points, probe in buffer, temperature probe in the same sample:

```text
cal ph 7
cal ph 4
cal ph 10
```

TDS, one point against a known solution. The body can pass `volts` and `tempC` when the capture is scripted; otherwise the live median is used.

```text
cal tds 707
```

Pumps, using the graduated cylinder shipped with each DFR1261. Prime into the cylinder, then set mL/sec. Clockwise (2500 µs) is the default dispense direction. Pass `invert` if the tube runs the other way.

```text
prime ph_up 10
cal pump ph_up 1.25
```

`GET /api/v1/calibration` shows which points and pumps are saved. Automatic nutrient dosing stays off until TDS is calibrated or `useFactoryTds` is set. Automatic pH dosing stays off until two pH points exist.

## API

The web UI should use `http://demeter.local`. The contract is [docs/API.md](docs/API.md). The same routes are available as one-line serial commands and as a JSON object per line, so a host can talk to the board before it is on Wi-Fi.

## Simulation

`POST /api/v1/simulation` feeds the control loop without probes. It is not stored. A reboot returns to the real sensors. Status includes `"simulation": true` while it is on.
