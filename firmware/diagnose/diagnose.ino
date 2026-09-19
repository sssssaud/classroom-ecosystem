// Hardware bring-up check: exercises each device on its own and prints raw
// numbers, so a silent channel can be told from a miswired one.
//   arduino-cli compile --fqbn esp32:esp32:esp32 diagnose && upload
#include <Wire.h>
#include <ESP_I2S.h>
#include "../classroom_node/config.h"

static void scanBus(int sda, int scl) {
  Wire.end();
  Wire.begin(sda, scl, 100000);
  delay(50);
  Serial.printf("  SDA=%2d SCL=%2d ->", sda, scl);
  int seen = 0;
  for (uint8_t a = 1; a < 127; ++a) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { Serial.printf(" 0x%02X", a); ++seen; }
  }
  Serial.println(seen ? "" : " (nothing)");
}

static int readReg(uint8_t addr, uint8_t reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return -1;
  if (Wire.requestFrom(addr, (uint8_t)1) != 1) return -1;
  return Wire.read();
}

static I2SClass i2s;
static bool i2s_ok = false;

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n\n========== HARDWARE DIAGNOSE ==========");

  Serial.println("\n[1] I2C scan, wired order and swapped:");
  scanBus(PIN_I2C_SDA, PIN_I2C_SCL);
  scanBus(PIN_I2C_SCL, PIN_I2C_SDA);

  Serial.println("\n[2] BME280 chip ID (reg 0xD0; expect 0x60, BMP280 is 0x58):");
  Wire.end();
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 100000);
  delay(50);
  for (uint8_t addr : { 0x76, 0x77 }) {
    const int id = readReg(addr, 0xD0);
    if (id < 0) Serial.printf("  0x%02X: no ACK\n", addr);
    else        Serial.printf("  0x%02X: id=0x%02X %s\n", addr, id,
                              id == 0x60 ? "<- BME280" : (id == 0x58 ? "<- BMP280 (no humidity)" : "<- unknown"));
  }

  Serial.println("\n[3] MQ-135 raw ADC on GPIO34 (floating pin drifts; a wired one is steady):");
  analogSetPinAttenuation(PIN_MQ135_AO, ADC_11db);
  int lo = 5000, hi = -1; long sum = 0;
  for (int i = 0; i < 40; ++i) {
    const int v = analogRead(PIN_MQ135_AO);
    lo = min(lo, v); hi = max(hi, v); sum += v;
    delay(10);
  }
  Serial.printf("  min=%d max=%d avg=%ld  (0 or 4095 pegged = check wiring)\n", lo, hi, sum / 40);

  Serial.println("\n[4] INMP441 I2S:");
  i2s.setPins(PIN_I2S_BCLK, PIN_I2S_WS, -1, PIN_I2S_DIN, -1);
  i2s_ok = i2s.begin(I2S_MODE_STD, I2S_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_32BIT,
                     I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT);
  Serial.printf("  driver started: %s\n", i2s_ok ? "yes" : "NO");

  Serial.println("\n[5] LEDs one at a time, 1 s each. Watch the board.");
  const int pins[] = { PIN_LED_GREEN, PIN_LED_BLUE, PIN_LED_RED };
  const char* names[] = { "GREEN (18)", "BLUE (19)", "RED (23)" };
  for (int i = 0; i < 3; ++i) pinMode(pins[i], OUTPUT);
  for (int i = 0; i < 3; ++i) {
    Serial.printf("  -> %s on\n", names[i]);
    digitalWrite(pins[i], HIGH); delay(1000); digitalWrite(pins[i], LOW);
  }

  Serial.println("\n[6] Buzzer, two 300 ms beeps. Listen.");
  pinMode(PIN_BUZZER, OUTPUT);
  for (int i = 0; i < 2; ++i) { tone(PIN_BUZZER, 2800); delay(300); noTone(PIN_BUZZER); delay(200); }

  Serial.println("\n[7] Live loop: ADC + mic below. Talk or clap to move the mic numbers.\n");
}

void loop() {
  const int adc = analogRead(PIN_MQ135_AO);

  long mn = 0, mx = 0; double sq = 0; size_t n = 0;
  if (i2s_ok) {
    static int32_t buf[512];
    const size_t got = i2s.readBytes(reinterpret_cast<char*>(buf), sizeof(buf));
    n = got / sizeof(int32_t);
    if (n) {
      double sum = 0;
      for (size_t i = 0; i < n; ++i) sum += double(buf[i] >> 8);
      const double mean = sum / n;
      mn = mx = buf[0] >> 8;
      for (size_t i = 0; i < n; ++i) {
        const long s = buf[i] >> 8;
        mn = min(mn, s); mx = max(mx, s);
        sq += (s - mean) * (s - mean);
      }
      sq = sqrt(sq / n);
    }
  }
  Serial.printf("ADC=%4d | mic n=%3u min=%8ld max=%8ld rms=%8.0f\n", adc, (unsigned)n, mn, mx, sq);
  delay(500);
}
