#include "sensors.h"

#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <Adafruit_BME280.h>
#include <ESP_I2S.h>
#include <math.h>

#include "config.h"

namespace {

Adafruit_BME280 bme;
bool bme_ok = false;

I2SClass i2s;
bool i2s_ok = false;

Preferences prefs;
float r0_ = 0.0f;              // clean-air resistance; 0 means "never calibrated"
float rs_ema_ = 0.0f;
bool  rs_primed_ = false;

uint32_t cal_until_ms_ = 0;
double   cal_sum_ = 0.0;
uint32_t cal_n_ = 0;

// Raw sensor resistance from one ADC read, before any compensation.
// Returns 0 on an implausible reading so the caller can mark the channel dead.
float readRs() {
  const int adc = analogRead(PIN_MQ135_AO);
  const float v_node = (float(adc) / ADC_MAX) * ADC_REF_V;
  const float v_ao = v_node * (MQ_DIVIDER_R1 + MQ_DIVIDER_R2) / MQ_DIVIDER_R2;
  // At the rails the divider maths blows up (or goes negative), and a floating
  // ADC pin sits near 0. Either way we have no usable resistance.
  if (v_ao < 0.05f || v_ao >= MQ_VCC - 0.05f) return 0.0f;
  return MQ_LOAD_R * (MQ_VCC - v_ao) / v_ao;
}

// MQ sensors read lower when warm and humid. First-order correction only.
float compensate(float rs, const Reading& temp, const Reading& hum) {
  float k = 1.0f;
  if (temp.valid) k += MQ_TEMP_COEFF * (temp.value - 20.0f);
  if (hum.valid)  k += MQ_HUM_COEFF * (hum.value - 50.0f);
  if (k < 0.2f) k = 0.2f;   // never let the correction invert the reading
  return rs / k;
}

bool readNoiseDb(float& db) {
  if (!i2s_ok) return false;

  static int32_t buf[I2S_FRAMES];
  const size_t want = sizeof(buf);
  const size_t got = i2s.readBytes(reinterpret_cast<char*>(buf), want);
  const size_t n = got / sizeof(int32_t);
  if (n < 32) return false;

  // INMP441 sends 24-bit samples left-aligned in a 32-bit slot, with a DC
  // offset that would otherwise dominate the RMS.
  double sum = 0.0;
  for (size_t i = 0; i < n; ++i) sum += double(buf[i] >> 8);
  const double mean = sum / double(n);

  double sq = 0.0;
  for (size_t i = 0; i < n; ++i) {
    const double s = double(buf[i] >> 8) - mean;
    sq += s * s;
  }
  const double rms = sqrt(sq / double(n));
  if (rms < 1.0) { db = NOISE_DB_OFFSET - 138.0f; return true; }   // silence floor

  db = float(20.0 * log10(rms / 8388608.0)) + NOISE_DB_OFFSET;
  return true;
}

}  // namespace

bool sensorsBegin() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

  // Print whatever is actually on the bus. "Not found" on its own cannot tell
  // a miswired sensor from one sitting at an address we never tried.
  Serial.print("I2C scan:");
  uint8_t seen = 0;
  for (uint8_t addr = 1; addr < 127; ++addr) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf(" 0x%02X", addr);
      ++seen;
    }
  }
  Serial.println(seen ? "" : " nothing responded (check SDA/SCL/VCC/GND)");

  // Breakouts ship on either address; probe both rather than make the user care.
  bme_ok = bme.begin(0x76, &Wire) || bme.begin(0x77, &Wire);
  if (bme_ok) {
    bme.setSampling(Adafruit_BME280::MODE_FORCED,
                    Adafruit_BME280::SAMPLING_X1,   // temperature
                    Adafruit_BME280::SAMPLING_X1,   // pressure
                    Adafruit_BME280::SAMPLING_X1,   // humidity
                    Adafruit_BME280::FILTER_OFF);
  }

  analogSetPinAttenuation(PIN_MQ135_AO, ADC_11db);

  i2s.setPins(PIN_I2S_BCLK, PIN_I2S_WS, -1, PIN_I2S_DIN, -1);
  i2s_ok = i2s.begin(I2S_MODE_STD, I2S_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_32BIT,
                     I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT);

  prefs.begin("classroom", false);
  r0_ = prefs.getFloat("mq_r0", 0.0f);

  return bme_ok;
}

void sensorsRead(Readings& out, uint32_t now_ms) {
  // ---- BME280 ----
  if (bme_ok) {
    if (bme.takeForcedMeasurement()) {
      const float t = bme.readTemperature();
      const float h = bme.readHumidity();
      const float p = bme.readPressure() / 100.0f;
      // A disconnected BME280 reads back as NaN rather than failing loudly.
      out.v[R_TEMP]  = { t, !isnan(t) };
      out.v[R_HUM]   = { h, !isnan(h) };
      out.v[R_PRESS] = { p, !isnan(p) && p > 300.0f && p < 1100.0f };
    }
  }

  // ---- MQ-135 ----
  const float rs_raw = readRs();
  if (rs_raw > 0.0f) {
    const float rs = compensate(rs_raw, out.v[R_TEMP], out.v[R_HUM]);
    rs_ema_ = rs_primed_ ? (MQ_EMA_ALPHA * rs + (1.0f - MQ_EMA_ALPHA) * rs_ema_) : rs;
    rs_primed_ = true;

    if (calibrating(now_ms)) {
      cal_sum_ += rs;
      ++cal_n_;
    } else if (cal_n_ > 0) {                 // window just closed
      r0_ = float(cal_sum_ / double(cal_n_));
      prefs.putFloat("mq_r0", r0_);
      cal_sum_ = 0.0;
      cal_n_ = 0;
    }

    // Index is R0/Rs: resistance falls as gas rises, so the number rises with
    // gas. 1.0 means "same as the clean air you calibrated in".
    if (r0_ > 0.0f && rs_ema_ > 0.0f) {
      out.v[R_AIR] = { r0_ / rs_ema_, true };
    }
  }

  // ---- INMP441 ----
  float db = 0.0f;
  if (readNoiseDb(db)) out.v[R_NOISE] = { db, true };
}

bool  baselineSet()  { return r0_ > 0.0f; }
float baselineR0()   { return r0_; }

void startCalibration(uint32_t now_ms) {
  cal_until_ms_ = now_ms + CALIBRATE_MS;
  cal_sum_ = 0.0;
  cal_n_ = 0;
}

bool calibrating(uint32_t now_ms) {
  return int32_t(cal_until_ms_ - now_ms) > 0;
}

void clearBaseline() {
  r0_ = 0.0f;
  prefs.remove("mq_r0");
}
