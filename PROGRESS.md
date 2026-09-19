# Progress

## 2026-09-19

**Done**
- Read the as-wired pinout; confirmed GPIO34/ADC1 is the correct MQ-135 input
  (ADC2 is unusable with WiFi active).
- Scoped the build: one classroom, one ESP32, no laptop or server. Mic reports
  loudness only; audio is never recorded. History is ~3 h in RAM.
- Design spec written and approved:
  `docs/superpowers/specs/2026-09-19-classroom-ecosystem-design.md`
- Dashboard built (`ui/index.html`, 17 KB, no CDN or chart library so the ESP32
  can serve it offline).
- Mock ESP32 (`ui/serve.py`, stdlib only) serving the exact JSON the firmware
  will serve, with forced scenarios for every UI state.
- Verified by rendering: normal / warning / alert / sensor-fault, light and dark.
  Fixed three real defects found that way — a dead sixth chart cell, charts
  auto-scaling flat series into fake drama, and an alert-level reading rendering
  in warning amber.

**Pending**
1. `firmware/` PlatformIO project + `config.h` (pins, thresholds, intervals).
2. Sensor drivers one at a time, each verified over serial alone:
   BME280 → MQ-135 → INMP441.
3. Pure `status` and `history` modules with host unit tests (no hardware).
4. Integration: LEDs, buzzer with hysteresis and mute, web server, mDNS,
   SoftAP fallback.
5. Bench test the whole box, then tune thresholds in the real room.

**Blockers**
- None. Note MQ-135 needs 24–48 h powered burn-in before readings mean anything.
