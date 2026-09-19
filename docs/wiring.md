# Wiring

Pins come from `firmware/classroom_node/config.h`; that file is the source of
truth. This page explains the parts the pin numbers do not tell you.

## Power rails

| Rail | Feeds |
|---|---|
| 3.3V | BMP280, INMP441 |
| 5V (VIN) | MQ-135 — its heater will not run at 3.3V |
| GND | everything, on one common ground |

Every ground must be common. A MQ-135 on a separate ground reads nonsense,
because its output is a voltage measured against the ESP32's reference.

## Pins

| Device | Pin | GPIO |
|---|---|---|
| BMP280 | SDA | 21 |
| | SCL | 22 |
| INMP441 | SCK | 26 |
| | WS | 25 |
| | SD | 33 |
| | L/R | tied high on this build (see below) |
| MQ-135 | AO | 34, through a divider (see below) |
| LED green | anode | 18, via 220R |
| LED blue | anode | 19, via 220R |
| LED red | anode | 23, via 220R |
| Buzzer | signal | 27 |

## MQ-135 output divider

The MQ-135's analog output swings to 5V. An ESP32 GPIO tolerates 3.3V, so the
output must be divided before it reaches GPIO34 or the pin is damaged.

    MQ-135 AO --[ 20k ]--+-- GPIO34
                         |
                      [ 10k ]
                         |
                        GND

`MQ_DIVIDER_R1` and `MQ_DIVIDER_R2` in `config.h` must match the resistors
actually fitted, or every air-index reading is scaled wrong.

GPIO34 is on ADC1. ADC2 cannot be read while WiFi is on, so the MQ-135 cannot
move to an ADC2 pin.

## Two things this build got wrong, and how the firmware absorbs them

**SDA and SCL are crossed.** The sensor answers on SDA=22, SCL=21, not the
wired order. `sensorsBegin` scans the wired order, then the swapped one, so
either wiring works and a crossed pair no longer looks like a dead sensor.

**The chip is a BMP280, not a BME280.** Chip ID 0x58, not 0x60. Same footprint,
same addresses, no humidity die inside. The firmware probes for both and uses
whichever answers. A BMP280 gives real temperature and pressure; humidity has
no sensor behind it and is labelled `simulated` everywhere it surfaces.

**The INMP441's L/R pin is tied high**, so it transmits in the I2S right slot
rather than the left. `sensorsBegin` probes both slots and keeps whichever
carries samples, so either strapping works. A slot mismatch reads back as
perfect silence rather than as an error, which is why it first looked dead.
