# Classroom Ecosystem — Design

Date: 2026-09-19
Status: approved (brainstorming complete)

## Goal

A single ESP32 box mounted in one classroom that continuously measures air
quality, comfort and noise, shows the room's state on three indicator LEDs,
sounds a buzzer on a gas alert, and serves its own web dashboard over WiFi.

"Done" = plug the box in, open `http://classroom.local` on a phone, and see
honest live readings plus three hours of history, with the LEDs matching what
the page says.

## Scope

- **One node.** One ESP32, one classroom. No server, no cloud, no laptop.
- **No audio is recorded.** The microphone's samples become a single loudness
  number on-chip and are discarded immediately. Nothing is stored or transmitted.
- **History is volatile.** A rolling ~3 hours held in RAM; lost on reboot. Live
  values and alerts are unaffected by a reboot.

Out of scope for this build: multiple rooms, SD-card logging, audio capture or
classification, user accounts, cloud upload.

## Hardware

Pinout as wired (from `docs/pinout.jpeg`):

| Component | Pin | ESP32 | Notes |
|---|---|---|---|
| BME280 | VCC / GND | 3.3V / GND | |
| | SDA / SCL | GPIO21 / GPIO22 | I²C; probe 0x76 then 0x77 |
| MQ-135 | VCC / GND | 5V (VIN) / GND | heater needs 5V |
| | AO | GPIO34 | via 20k/10k divider; ADC1, input-only |
| INMP441 | VDD / GND | 3.3V / GND | |
| | SCK / WS / SD | GPIO26 / GPIO25 / GPIO33 | I²S |
| | L/R | GND | left channel |
| Green LED | anode | GPIO18 via 220Ω | |
| Blue LED | anode | GPIO19 via 220Ω | |
| Red LED | anode | GPIO23 via 220Ω | |
| Buzzer | control | GPIO27 → MOSFET/transistor gate | buzzer + on 5V |

### Hardware constraints that shape the design

1. **MQ-135 cannot report true ppm.** It is an uncalibrated resistive sensor
   that drifts with temperature and humidity and needs 24–48 h burn-in. The
   system reports a *relative air-quality index against a user-set clean-air
   baseline*, never a fabricated ppm figure.
2. **GPIO34 is ADC1 and input-only.** Correct: ADC2 is unusable while WiFi is
   active. No internal pull-up, which is fine for an analog input.
3. **MQ-135 AO can reach 5V**, above the ESP32's 3.3V ADC limit — hence the
   resistor divider already on the board. The divider ratio is a configurable
   constant, since the installed resistors determine the true scale.

## Architecture

```
 BME280 ──I²C──┐
 MQ-135 ──ADC──┼─→ [ESP32] ──→ status logic ──→ LEDs + buzzer
 INMP441 ─I²S──┘      │
                      └─→ WiFi ──→ browser (http://classroom.local)
```

### Network

- Station mode on the classroom WiFi; mDNS hostname `classroom`.
- **Fallback:** if association fails within 15 s, start a SoftAP so a phone can
  connect directly. Removes total dependence on site WiFi for a live demo.
- Credentials live in `firmware/include/secrets.h`, git-ignored, with a
  committed `secrets.example.h`.

### HTTP API

Served by the ESP32's built-in synchronous web server. The browser polls; there
is no WebSocket. Environmental data changes over seconds, polling is ~20 lines,
and it cannot wedge in a half-open socket state.

| Method | Path | Purpose |
|---|---|---|
| GET | `/` | the dashboard (single embedded HTML document) |
| GET | `/api/state` | live snapshot; polled every 2 s |
| GET | `/api/history` | rolling buffer for the charts |
| POST | `/api/mute` | silence the buzzer for 10 minutes |
| POST | `/api/calibrate` | begin 60 s clean-air baseline capture |

`/api/state` response shape (the contract the UI is built against):

```json
{
  "uptime_s": 12045,
  "status": "ok",
  "message": "Room is normal",
  "muted_until_s": 0,
  "gas_baseline_set": true,
  "burn_in_complete": false,
  "readings": {
    "temperature_c": { "value": 24.8, "ok": true, "band": [18, 30] },
    "humidity_pct":  { "value": 48.2, "ok": true, "band": [30, 70] },
    "pressure_hpa":  { "value": 1008.4, "ok": true, "band": null },
    "air_index":     { "value": 1.14, "ok": true, "band": [0, 1.5] },
    "noise_db":      { "value": 61.0, "ok": true, "band": [0, 75] }
  }
}
```

`status` is one of `boot`, `ok`, `warn`, `alert`, `fault`. A reading that cannot
be taken has `"value": null` and `"ok": false` — never a fabricated `0`.

### Timing and memory

- Sensors sampled every 2 s.
- One history record every 10 s. 1080 records ≈ 3 h.
- 1080 × 5 values × 2 bytes ≈ 11 KB, comfortable within 320 KB RAM.

### Module layout

| File | Responsibility | Isolated test |
|---|---|---|
| `include/config.h` | pins, thresholds, intervals — one source of truth | — |
| `src/sensor_env.*` | BME280 → temperature, humidity, pressure | serial |
| `src/sensor_gas.*` | MQ-135 → smoothed index, baseline calibration | serial |
| `src/sensor_noise.*` | INMP441 I²S → loudness in dB | serial |
| `src/status.*` | **pure**: readings → status + LED pattern + buzzer | host unit test |
| `src/indicators.*` | drives LEDs and buzzer from status | blink test |
| `src/history.*` | **pure**: ring buffer | host unit test |
| `src/web.*` | HTTP routes, embedded page | browser |
| `src/main.cpp` | wiring only | — |

`status` and `history` contain no Arduino calls, so they compile and run as
ordinary C++ on a laptop. That is where the automated check lives; the alert
logic can be verified without the hardware connected.

## Status and alert logic

Blue reports the *system*; green and red report the *room*. Green and red are
mutually exclusive; blue is independent.

| LED | Meaning |
|---|---|
| Blue blinking | booting / connecting to WiFi |
| Blue solid | running and reachable |
| Green solid | all readings within band |
| Green blinking | warning — a reading outside comfort band |
| Red solid | gas alert; buzzer active |
| Red blinking | sensor fault — a channel is not responding |

### Default thresholds

All in `config.h` and expected to be tuned in the real room.

| Reading | Warn | Alert |
|---|---|---|
| Temperature | outside 18–30 °C | — |
| Humidity | outside 30–70 % | — |
| Noise | > 75 dB sustained 30 s | — |
| Air index | > 1.5 × baseline | > 2.5 × baseline |

### Hysteresis

A state change requires the threshold to be crossed continuously for **10 s**,
and an active alert clears only when the reading falls to **85 %** of the
threshold. Without this a single noisy ADC sample makes the buzzer chirp at
random and the box gets unplugged.

### Buzzer

- Fires on a gas alert only. Never on noise — a buzzer that answers a loud room
  by being louder is self-defeating.
- 200 ms beep every 3 s, not continuous.
- **Mute** silences sound for 10 minutes, then auto-unmutes. Mute never clears
  the red LED or the dashboard alert: an alarm can be silenced, a hazard cannot
  be dismissed.

### MQ-135 calibration (the required tuning knob)

- "Set clean-air baseline" in the UI averages 60 s of readings with the room
  aired out and stores the baseline resistance in NVS, surviving power cuts.
- Until set, the dashboard reports **"baseline not set"** and shows no index.
- The BME280 reading applies a first-order temperature/humidity correction to
  the gas value; the coefficients are constants in `config.h`.
- Powered-on hours are tracked; readings are labelled **"stabilising"** until
  the burn-in period has elapsed.

### Sensor faults

BME280 is probed at 0x76 then 0x77. Any channel that fails to read reports
`null`, renders as "offline" in the UI, and blinks the red LED. Zero is never
substituted for a missing reading.

## Web UI

### Explicit non-goals (the "vibe-coded" cluster)

No purple-to-blue gradients, no glassmorphism, no emoji in headings, no neon
glow, no uniform oversized corner radii, no decorative accent colours, no
animated background shapes.

### Direction: instrument panel

- Typography-led: one typeface, a deliberate type scale, **tabular numerals** on
  every reading so digits do not shift horizontally as values update.
- One neutral ground. Colour is semantic only — green/amber/red carry status and
  nothing decorative is coloured.
- Layout: a plain-language status line, a row of reading tiles, then charts.
- Each tile shows its comfort band beneath the value, so a number is legible
  without memorising thresholds.
- Charts: small multiples on a shared time axis, thin strokes, minimal
  gridlines, no area gradients; threshold zones very faintly shaded.
- Light and dark via `prefers-color-scheme`. Correct at phone width.
- **Accessibility:** status is never conveyed by colour alone. Every state
  carries a text label and a distinct icon shape.
- A visible "updated Ns ago" stamp so a frozen page is immediately obvious.

### Build order

The dashboard is built first against mock JSON served locally, with no hardware
attached. The visual design and every alert state are settled on the laptop.
The ESP32 then serves the identical JSON shape; the UI cannot tell the
difference.

## Testing

| Layer | Check |
|---|---|
| `status` | host unit test: threshold crossings, hysteresis dwell and release, mute behaviour, fault precedence |
| `history` | host unit test: ring buffer wrap, ordering |
| Sensors | individual serial sketches before integration |
| UI | mock JSON server, every status state forced by hand |
| Integration | full box on the bench before it goes in a room |

## Risks

| Risk | Mitigation |
|---|---|
| MQ-135 readings meaningless without burn-in | UI labels readings "stabilising"; baseline capture required before any index is shown |
| Site WiFi has a captive portal or blocks mDNS | SoftAP fallback; IP shown on serial |
| Buzzer nuisance in a real classroom | hysteresis, intermittent pattern, mute with auto-release |
| ADC noise on MQ-135 | exponential moving average plus the 10 s dwell |
| History lost on power cut | accepted and documented; SD card is the upgrade path |
