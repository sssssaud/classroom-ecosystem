# class-ecosystem

A single ESP32 box that watches one classroom: temperature, humidity, pressure,
relative air quality and noise level. Three LEDs show the room's state at a
glance, a buzzer fires on a gas alert, and the box serves its own web dashboard
over WiFi — no laptop, no server, no cloud.

## What it measures

| Sensor | Reading | Honest limits |
|---|---|---|
| BME280 / BMP280 | temperature, humidity, pressure | BMP280 has no humidity sensor, so humidity is clearly labelled simulated |
| MQ-135 | **relative** air-quality index | not ppm — see below |
| INMP441 | noise level in dB | audio is never recorded |

**The MQ-135 does not report CO₂ in ppm, and this project will not pretend it
does.** It is an uncalibrated resistive sensor that drifts with temperature and
humidity and needs 24–48 h of burn-in. What it *can* do honestly is report how
much worse the air is than a baseline you set yourself with the windows open,
and raise an alarm when that rises sharply.

**No audio leaves the chip.** The microphone's samples become a single loudness
number on the ESP32 and are discarded immediately. Nothing is recorded, stored
or transmitted.

## Indicators

| LED | Meaning |
|---|---|
| Blue blinking | connecting to WiFi |
| Blue solid | running and reachable |
| Green solid | room normal |
| Green blinking | warning — something outside comfort range |
| Red solid | gas alert, buzzer active |
| Red blinking | sensor fault |

## Run the dashboard without hardware

```bash
python3 ui/serve.py          # http://localhost:8000
python3 ui/serve.py --selftest
```

Force any state by adding `?s=warn`, `?s=alert`, `?s=fault`, `?s=nobaseline`
or `?s=boot` to the URL.

## Wiring

Full pin table: [`docs/pinout.md`](docs/pinout.md). Rails, the I²C cross and
the other build quirks: [`docs/wiring.md`](docs/wiring.md).
MQ-135's analog output runs through a 20k/10k divider because it can reach 5V
and the ESP32's ADC tops out at 3.3V. GPIO34 is on ADC1, which is the only ADC
that works while WiFi is on.

## Firmware

Build and upload with Arduino CLI:

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 firmware/classroom_node
arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:esp32 firmware/classroom_node
```

Without `firmware/classroom_node/secrets.h`, the ESP32 starts its own WiFi
network named `classroom-node`; open `http://192.168.4.1` or
`http://classroom.local`.

## Status

Dashboard and firmware are built and verified. MQ-135 still needs 24-48 hours
of burn-in before its baseline is meaningful; recalibrate in clean air after
burn-in using the web UI or by sending `c` over serial.
