# class-ecosystem

Classroom environment monitor: one ESP32 box that measures air quality, comfort
and noise in a single classroom, shows the room's state on three LEDs, sounds a
buzzer on a gas alert, and serves its own web dashboard over WiFi.
Stack: C++ (ESP32 / PlatformIO) for firmware, vanilla HTML+JS for the dashboard,
Python stdlib for the mock server used during UI work.

## Project goal
Plug the box in, open `http://classroom.local` on a phone, and see honest live
readings plus three hours of history, with the LEDs matching what the page says.
No laptop, no server, no cloud.

## Directory map
- `docs/pinout.jpeg` — the as-wired pinout photo.
- `docs/wiring.md` — rails, the MQ-135 divider, and the three wiring quirks this build has.
- `docs/superpowers/specs/` — the approved design spec.
- `ui/index.html` — the dashboard. Self-contained; the ESP32 serves this verbatim.
- `ui/serve.py` — mock ESP32 (stdlib only) so the UI can be built with no hardware.
- `firmware/classroom_node/` — the firmware (arduino-cli, ESP32 core 3.x).
- `firmware/diagnose/` — per-device bring-up check: I2C scan, ADC, I2S, LEDs, buzzer.
- `firmware/test/` — host tests for the pure modules, plain g++, no board.

## Stack
- ESP32 (BME280 on I²C, MQ-135 on ADC1/GPIO34, INMP441 on I²S, 3 LEDs, buzzer).
- Firmware: Arduino framework via arduino-cli. `status` and `history` are pure
  C++ with no Arduino calls so they unit-test on the laptop.
- UI: no framework, no CDN, no chart library — the ESP32 serves it offline.
- No GPU/ML involved; the 6GB VRAM ceiling is irrelevant here.

## Build / run commands
- Dashboard (no hardware): `python3 ui/serve.py` then open http://localhost:8000
- Contract check: `python3 ui/serve.py --selftest`
- Force a UI state: open `?s=warn` / `?s=alert` / `?s=fault` / `?s=nobaseline` / `?s=boot`
- Firmware: `arduino-cli compile --fqbn esp32:esp32:esp32 firmware/classroom_node`
  then `arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:esp32 firmware/classroom_node`
- Host tests: `g++ -std=c++17 -Ifirmware/classroom_node firmware/test/test_status.cpp firmware/classroom_node/status.cpp firmware/classroom_node/history.cpp -o /tmp/t && /tmp/t`

## Current status
- Done: spec, dashboard, firmware, host tests. Flashed and verified on hardware:
  all five tiles live, history streaming, LEDs and buzzer on the agreed cadence.
- MQ-135 (GPIO34) and INMP441 (I2S) are confirmed working against the board.
  The mic transmits on the RIGHT slot because its L/R pin is strapped high;
  `sensorsBegin` probes both slots, so either strapping works.
- **No BME280 is fitted.** Nothing answers anywhere on the I2C bus, on either
  pin order — a wiring or power fault, not an address or driver problem. Until
  one is fitted, `sensorsRead` synthesizes temperature/humidity/pressure and
  every path labels them `simulated`; the branch is skipped the moment a real
  BME280 answers at boot. These are not measurements. See `firmware/diagnose`.
- Blockers: none. MQ-135 needs 24–48 h burn-in before its readings mean
  anything, so re-run Calibrate in clean air once the board has been on a while.
