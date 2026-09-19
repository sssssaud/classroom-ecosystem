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
- `docs/pinout.jpeg` — the as-wired pinout photo, source of truth for GPIO.
- `docs/superpowers/specs/` — the approved design spec.
- `ui/index.html` — the dashboard. Self-contained; the ESP32 serves this verbatim.
- `ui/serve.py` — mock ESP32 (stdlib only) so the UI can be built with no hardware.
- `firmware/` — PlatformIO project (not yet written).

## Stack
- ESP32 (BME280 on I²C, MQ-135 on ADC1/GPIO34, INMP441 on I²S, 3 LEDs, buzzer).
- Firmware: Arduino framework via PlatformIO. `status` and `history` are pure
  C++ with no Arduino calls so they unit-test on the laptop.
- UI: no framework, no CDN, no chart library — the ESP32 serves it offline.
- No GPU/ML involved; the 6GB VRAM ceiling is irrelevant here.

## Build / run commands
- Dashboard (no hardware): `python3 ui/serve.py` then open http://localhost:8000
- Contract check: `python3 ui/serve.py --selftest`
- Force a UI state: open `?s=warn` / `?s=alert` / `?s=fault` / `?s=nobaseline` / `?s=boot`
- Firmware: `pio run -t upload` (once `firmware/` exists)

## Current status
- Done: design spec approved; dashboard built and verified against mock data in
  light and dark, normal/warn/alert/fault states.
- Pending: firmware — sensor drivers, pure `status`/`history` modules with host
  unit tests, then integration.
- Blockers: none. MQ-135 needs 24–48 h burn-in before its readings mean anything.
