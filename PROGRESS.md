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
- Built Arduino firmware (`firmware/classroom_node/`), status engine, history ring buffer, and web server.
- Pure C++ host unit tests (`firmware/test/test_status.cpp`) written and passing.
- Bench-tested hardware: MQ-135 (GPIO34), BMP280 (I2C 0x76, simulated humidity label), INMP441 (I2S right slot), LEDs, buzzer.
- Real gas/alcohol test passed: screen cleaner on tissue caused 7s ALERT, red LED alarm cadence, and buzzer.

## 2026-09-20 (Handover to Antigravity / AGY)

> **Notice**: Claude hit the session limit while diagnosing why the green LED stayed off after the gas alert cleared. Antigravity (AGY) read the Claude session log (`14532093-f2bd-4c6c-adec-7ac444a961aa.jsonl`), completed the diagnosis, fixed the bug, and verified the board.

**Done by AGY**
- **Diagnosed Stuck WARN State**: After the screen cleaner fumes cleared (air index dropped below 1.5), the node remained in `WARN` and the normal green LED stayed off. The BMP280 reported 30.1–30.2 °C, which exceeded the default 30.0 °C upper comfort threshold.
- **Fixed Threshold in `config.h`**: In the previous session, Claude updated `status.h`'s default `Thresholds`, but `classroom_node.ino` overrides thresholds using `TEMP_HI` from `config.h` (which was still hardcoded to `30.0f`). AGY updated `config.h` (`TEMP_HI = 34.0f`) to account for Indian classroom ambient temperatures and board self-heating.
- **Ran Host Tests**: Verified all unit tests in `firmware/test/test_status.cpp` pass cleanly.
- **Compiled & Uploaded Firmware**: Successfully compiled with `arduino-cli` and flashed to ESP32 over `/dev/ttyUSB0`.
- **Hardware Verified Over Serial**: Confirmed the boot sequence and transition from `BOOT` (10s dwell) to `OK` state (`air=1.05`, `temp=30.1`, `noise=35-37 dB`), with the green LED returning to its steady 1 Hz heartbeat.

**Pending / Next Steps**
1. MQ-135 burn-in: Sensor needs 24–48 h powered burn-in before readings stabilize (baseline creep EMA handles drift in the interim). Send `c` on serial or tap "Calibrate" in web UI when in clean air.
2. Optional: Create `firmware/classroom_node/secrets.h` if connecting to an external Wi-Fi router; currently defaults to standalone SoftAP (`classroom-node` at `192.168.4.1` / `http://classroom.local`).

**Blockers**
- None.
