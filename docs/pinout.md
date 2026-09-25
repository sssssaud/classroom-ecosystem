# Pinout

Transcribed from the hand-drawn wiring sheet, then reconciled against
`firmware/classroom_node/config.h`, which is the source of truth. Where the
sheet and the firmware disagree, the firmware wins and the difference is listed
at the bottom.

`docs/wiring.md` explains the parts a pin number cannot tell you (power rails,
the I²C cross, the BMP/BME mix-up, the I2S slot).

## Sensors

| Component | Component pin | ESP32 pin | `config.h` |
|---|---|---|---|
| **BMP280** (sheet says BME280 — see note 2) | VCC | 3.3V | — |
| | GND | GND | — |
| | SDA | GPIO 21 | `PIN_I2C_SDA` |
| | SCL | GPIO 22 | `PIN_I2C_SCL` |
| **MQ-135** | VCC | 5V / VIN | heater will not run at 3.3V |
| | GND | GND | — |
| | AO | GPIO 34, **through a divider** | `PIN_MQ135_AO` |
| **INMP441** | VDD | 3.3V | — |
| | GND | GND | — |
| | SCK / BCLK | GPIO 26 | `PIN_I2S_BCLK` |
| | WS / LRCL | GPIO 25 | `PIN_I2S_WS` |
| | SD / DOUT | GPIO 33 | `PIN_I2S_DIN` |
| | L/R | GND on the sheet — **tied high on this build** (note 3) | — |

## Indicators

| Component | Pin | ESP32 pin | `config.h` |
|---|---|---|---|
| **Green LED** | anode (+) | GPIO 18, via 220 Ω | `PIN_LED_GREEN` |
| | cathode (−) | GND | — |
| **Blue LED** | anode (+) | GPIO 19, via 220 Ω | `PIN_LED_BLUE` |
| | cathode (−) | GND | — |
| **Red LED** | anode (+) | GPIO 23, via 220 Ω | `PIN_LED_RED` |
| | cathode (−) | GND | — |
| **Buzzer** | control | GPIO 27, **via a transistor/MOSFET gate** — not driven directly | `PIN_BUZZER` |
| | + | 5V | — |
| | − | GND | — |

## MQ-135 output divider

The MQ-135's analog output swings to 5V; an ESP32 GPIO tolerates 3.3V, so it
must be divided before it reaches GPIO 34.

    MQ-135 AO --[ 20k ]--+-- GPIO34
                         |
                      [ 10k ]
                         |
                        GND

`MQ_DIVIDER_R1 = 20000` (series, from AO) and `MQ_DIVIDER_R2 = 10000` (to GND)
in `config.h` must match the resistors actually fitted. `sensors.cpp` reconstructs
the real AO voltage as `v_node * (R1 + R2) / R2`, so wrong values scale every
air-index reading wrong.

GPIO 34 is on ADC1. ADC2 cannot be read while WiFi is on, so the MQ-135 cannot
move to an ADC2 pin.

## Where the hand-drawn sheet differs from the build

1. **Divider resistors are swapped on the sheet.** It shows 10 kΩ in series from
   AO and 20 kΩ to GND. The firmware is built for the opposite — 20 kΩ series,
   10 kΩ to GND. Building it the sheet's way puts ~3.3 V on GPIO 34 instead of
   ~1.67 V and makes every reading wrong. **Follow the table above, not the sheet.**
2. **The chip is a BMP280, not a BME280.** Chip ID 0x58, not 0x60 — same
   footprint and addresses, no humidity die inside. Humidity is labelled
   `simulated` everywhere it surfaces.
3. **The INMP441's L/R pin is tied high**, not grounded as the sheet shows, so it
   transmits in the I2S right slot. `sensorsBegin` probes both slots and keeps
   whichever carries samples, so either strapping works.
4. **SDA and SCL are physically crossed on this build** — the sensor answers on
   SDA = 22, SCL = 21. `sensorsBegin` scans the wired order and then the swapped
   one, so either wiring works.
