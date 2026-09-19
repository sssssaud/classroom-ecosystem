// Every pin, threshold and interval lives here. Nothing else hard-codes them.
#pragma once
#include <stdint.h>

// ---- pins (as wired; see docs/pinout.jpeg) ----
#define PIN_I2C_SDA   21
#define PIN_I2C_SCL   22
#define PIN_MQ135_AO  34   // ADC1 only: ADC2 is unusable while WiFi is on
#define PIN_I2S_BCLK  26
#define PIN_I2S_WS    25
#define PIN_I2S_DIN   33
#define PIN_LED_GREEN 18
#define PIN_LED_BLUE  19
#define PIN_LED_RED   23
#define PIN_BUZZER    27   // drives a transistor/MOSFET gate, not the buzzer directly

// ---- timing ----
static const uint32_t SAMPLE_INTERVAL_MS  = 2000;
static const uint32_t HISTORY_INTERVAL_MS = 10000;
static const uint32_t WIFI_TIMEOUT_MS     = 15000;  // then fall back to SoftAP
static const uint32_t MUTE_DURATION_MS    = 600000; // 10 minutes
static const uint32_t CALIBRATE_MS        = 60000;  // clean-air baseline averaging
static const uint32_t BURN_IN_MS          = 86400000UL; // 24 h; readings are rough before this

// ---- comfort thresholds; tune these in the real room ----
static const float TEMP_LO   = 18.0f;
static const float TEMP_HI   = 30.0f;
static const float HUM_LO    = 30.0f;
static const float HUM_HI    = 70.0f;
static const float NOISE_HI  = 75.0f;   // dB
static const float AIR_WARN  = 1.5f;    // x clean-air baseline
static const float AIR_ALERT = 2.5f;

// A crossing must persist this long before the state changes, and an active
// alert only clears once the value falls to RELEASE_FRAC of its threshold.
// Without both, one noisy ADC sample makes the buzzer chirp at random.
static const uint32_t DWELL_MS      = 10000;
static const float    RELEASE_FRAC  = 0.85f;
static const float    BAND_MARGIN   = 0.05f;  // 5% of band width, re-entry margin

// ---- MQ-135 ----
// The divider on the board scales the sensor's 0-5V output into the ADC range.
// Measure your actual resistors and correct this if the index looks wrong.
static const float MQ_DIVIDER_R1 = 20000.0f;
static const float MQ_DIVIDER_R2 = 10000.0f;
static const float MQ_VCC     = 5.0f;      // sensor heater/divider supply
static const float MQ_LOAD_R  = 10000.0f;  // RL on the breakout board
static const float ADC_REF_V  = 3.3f;      // ESP32 ADC full scale at 11 dB
static const int   ADC_MAX    = 4095;
static const float MQ_EMA_ALPHA  = 0.05f;   // heavy smoothing; raw ADC is noisy
// First-order correction: MQ sensors read high when warm and humid.
static const float MQ_TEMP_COEFF = -0.012f;  // per degree C from 20C
static const float MQ_HUM_COEFF  = -0.0018f; // per %RH from 50%

// ---- INMP441 ----
static const uint32_t I2S_SAMPLE_RATE = 16000;
static const uint16_t I2S_FRAMES      = 512;
// dB here is NOT calibrated SPL. We report dBFS + offset; the offset is a pure
// fudge factor. Hold a phone sound-level meter next to the box and shift this
// until the numbers agree. ponytail: one knob beats a calibration routine.
static const float NOISE_DB_OFFSET = 100.0f;

// ---- history ----
static const uint16_t HISTORY_CAPACITY = 1080;  // 3 h at one record per 10 s

// ---- network ----
static const char MDNS_HOST[]   = "classroom";
static const char AP_SSID[]     = "classroom-node";
