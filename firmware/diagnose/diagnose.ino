// Bring-up check: every device on its own, so a failure names one part.
// Prints what it finds; the LED and buzzer steps need a human watching.
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <Adafruit_BMP280.h>
#include <ESP_I2S.h>
#include "../classroom_node/config.h"

static Adafruit_BME280 bme;
static Adafruit_BMP280 bmp;
static I2SClass i2s;
static int sda_used = PIN_I2C_SDA, scl_used = PIN_I2C_SCL;

static uint8_t scan(int sda, int scl, uint8_t* found) {
  Wire.end(); Wire.begin(sda, scl); Wire.setClock(100000);
  uint8_t n = 0;
  for (uint8_t a = 1; a < 127; ++a) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0 && n < 8) found[n++] = a;
  }
  return n;
}

static void step1_i2c() {
  Serial.println("\n[1] I2C bus");
  uint8_t f[8];
  uint8_t n = scan(PIN_I2C_SDA, PIN_I2C_SCL, f);
  if (!n) {
    n = scan(PIN_I2C_SCL, PIN_I2C_SDA, f);
    if (n) { sda_used = PIN_I2C_SCL; scl_used = PIN_I2C_SDA;
             Serial.println("    NOTE: answers only with SDA/SCL swapped"); }
  }
  Serial.printf("    SDA=%d SCL=%d ->", sda_used, scl_used);
  if (!n) { Serial.println(" nothing.  FAIL"); return; }
  for (uint8_t i = 0; i < n; ++i) Serial.printf(" 0x%02X", f[i]);
  Serial.println();
}

static void step2_comfort() {
  Serial.println("\n[2] Comfort sensor");
  if (bme.begin(0x76, &Wire) || bme.begin(0x77, &Wire)) {
    bme.setSampling(Adafruit_BME280::MODE_FORCED, Adafruit_BME280::SAMPLING_X1,
                    Adafruit_BME280::SAMPLING_X1, Adafruit_BME280::SAMPLING_X1,
                    Adafruit_BME280::FILTER_OFF);
    bme.takeForcedMeasurement();
    Serial.printf("    BME280 OK  temp=%.1fC  hum=%.0f%%  press=%.0fhPa\n",
                  bme.readTemperature(), bme.readHumidity(), bme.readPressure() / 100.0);
  } else if (bmp.begin(0x76, BMP280_CHIPID) || bmp.begin(0x77, BMP280_CHIPID)) {
    bmp.setSampling(Adafruit_BMP280::MODE_FORCED, Adafruit_BMP280::SAMPLING_X1,
                    Adafruit_BMP280::SAMPLING_X1, Adafruit_BMP280::FILTER_OFF);
    bmp.takeForcedMeasurement();
    Serial.printf("    BMP280 OK  temp=%.1fC  press=%.0fhPa  (no humidity die)\n",
                  bmp.readTemperature(), bmp.readPressure() / 100.0);
  } else {
    Serial.println("    FAIL: no BME280 and no BMP280 answered");
  }
}

static void step3_mq135() {
  Serial.println("\n[3] MQ-135 on GPIO34");
  analogSetPinAttenuation(PIN_MQ135_AO, ADC_11db);
  int mn = 4096, mx = -1; long sum = 0;
  for (int i = 0; i < 40; ++i) {
    const int v = analogRead(PIN_MQ135_AO);
    mn = min(mn, v); mx = max(mx, v); sum += v; delay(20);
  }
  const int avg = sum / 40;
  Serial.printf("    min=%d max=%d avg=%d  (%.2f V at the pin)\n",
                mn, mx, avg, avg * ADC_REF_V / ADC_MAX);
  if (avg < 30)        Serial.println("    FAIL: pegged low - check VCC on 5V");
  else if (avg > 4000) Serial.println("    FAIL: pegged high - check the divider");
  else                 Serial.println("    OK: steady mid-scale reading");
}

static void step4_mic() {
  Serial.println("\n[4] INMP441 on I2S");
  static int32_t b[512];
  bool any = false;
  for (uint8_t a = 0; a < 2; ++a) {
    const i2s_std_slot_mask_t slot = a ? I2S_STD_SLOT_RIGHT : I2S_STD_SLOT_LEFT;
    i2s.end(); delay(30);
    i2s.setPins(PIN_I2S_BCLK, PIN_I2S_WS, -1, PIN_I2S_DIN, -1);
    if (!i2s.begin(I2S_MODE_STD, 16000, I2S_DATA_BIT_WIDTH_32BIT,
                   I2S_SLOT_MODE_MONO, slot)) continue;
    i2s.readBytes(reinterpret_cast<char*>(b), sizeof(b)); delay(30);
    const size_t n = i2s.readBytes(reinterpret_cast<char*>(b), sizeof(b)) / 4;
    double mean = 0; for (size_t i = 0; i < n; ++i) mean += b[i] >> 8; mean /= n ? n : 1;
    double sq = 0; for (size_t i = 0; i < n; ++i) { const double d = (b[i] >> 8) - mean; sq += d * d; }
    const double rms = n ? sqrt(sq / n) : 0;
    Serial.printf("    %-5s slot: rms=%.0f  %s\n", a ? "RIGHT" : "LEFT", rms,
                  rms > 1.0 ? "<-- OK, mic is here" : "silent");
    if (rms > 1.0) any = true;
  }
  if (!any) Serial.println("    FAIL: silent on both slots - check SD on GPIO33");
}

static void step5_leds() {
  Serial.println("\n[5] LEDs - WATCH THE BOARD, 2 s each, twice round");
  const int pin[3]  = { PIN_LED_GREEN, PIN_LED_BLUE, PIN_LED_RED };
  const char* nm[3] = { "GREEN (18)", "BLUE  (19)", "RED   (23)" };
  for (uint8_t round = 0; round < 2; ++round) {
    for (uint8_t i = 0; i < 3; ++i) {
      for (uint8_t j = 0; j < 3; ++j) digitalWrite(pin[j], j == i ? HIGH : LOW);
      Serial.printf("    now ON: %s\n", nm[i]);
      delay(2000);
    }
  }
  for (uint8_t j = 0; j < 3; ++j) digitalWrite(pin[j], LOW);
  Serial.println("    all off");
}

static void step6_buzzer() {
  Serial.println("\n[6] Buzzer - LISTEN, three beeps");
  for (uint8_t i = 0; i < 3; ++i) {
    tone(PIN_BUZZER, 2800); delay(300); noTone(PIN_BUZZER); delay(300);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_BLUE, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  Serial.println("\n\n========== BRING-UP CHECK ==========");
  step1_i2c(); step2_comfort(); step3_mq135(); step4_mic();
  step5_leds(); step6_buzzer();
  Serial.println("\n========== DONE ==========");
}

void loop() { delay(1000); }
