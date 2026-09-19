// Classroom environment node: reads the room, shows it on three LEDs, and
// serves its own dashboard. No cloud, no laptop, no SD card.
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

#include "config.h"
#include "status.h"
#include "history.h"
#include "sensors.h"
#include "page.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#define WIFI_SSID ""
#define WIFI_PASS ""
#endif

static const char* KEYS[R_COUNT] = {
  "temperature_c", "humidity_pct", "pressure_hpa", "air_index", "noise_db"
};

static Thresholds thresholds() {
  Thresholds t;
  t.temp_lo = TEMP_LO;  t.temp_hi = TEMP_HI;
  t.hum_lo  = HUM_LO;   t.hum_hi  = HUM_HI;
  t.noise_hi = NOISE_HI;
  t.air_warn = AIR_WARN; t.air_alert = AIR_ALERT;
  t.release = RELEASE_FRAC;
  t.band_margin = BAND_MARGIN;
  t.dwell_ms = DWELL_MS;
  return t;
}

static StatusEngine engine(thresholds());
static int16_t hist_storage[HISTORY_CAPACITY * R_COUNT];
static History history(hist_storage, HISTORY_CAPACITY);
static WebServer server(80);

static Readings latest;
static StatusOut latest_out;
static uint32_t last_sample_ms = 0;
static uint32_t last_history_ms = 0;
static bool ap_mode = false;

// ---------------------------------------------------------------- indicators

// Blue: on whenever the node has power. Green: blinks steadily while the room
// is normal, dark the instant it is not. Red: a two-pulse alarm cadence, so a
// gas alert cannot be mistaken for green's calm heartbeat across the room.
static void drawIndicators(const StatusOut& o, uint32_t now_ms) {
  digitalWrite(PIN_LED_BLUE, HIGH);

  const bool calm = (now_ms / 500) % 2;                 // 1 Hz, unhurried
  digitalWrite(PIN_LED_GREEN, (o.state == ST_OK && calm) ? HIGH : LOW);

  // Two short pulses then a gap — the cadence smoke alarms use.
  const uint32_t phase = now_ms % 1000;
  const bool alarm = (phase < 120) || (phase >= 220 && phase < 340);
  digitalWrite(PIN_LED_RED, (o.state == ST_ALERT && alarm) ? HIGH : LOW);

  // Buzzer rides the same pulses so light and sound read as one alarm. It
  // follows o.buzzer, so muting silences the sound while red keeps flashing.
  if (o.buzzer && alarm) tone(PIN_BUZZER, 2800);
  else noTone(PIN_BUZZER);
}

// ---------------------------------------------------------------------- JSON

static void appendFloat(String& s, float v, int dp) {
  char buf[16];
  dtostrf(v, 0, dp, buf);
  s += buf;
}

static const char* levelName(Level l) {
  switch (l) {
    case LVL_OK:    return "ok";
    case LVL_WARN:  return "warn";
    case LVL_ALERT: return "alert";
    default:        return "offline";
  }
}

static const char* stateName(RoomState s) {
  switch (s) {
    case ST_OK:    return "ok";
    case ST_WARN:  return "warn";
    case ST_ALERT: return "alert";
    case ST_FAULT: return "fault";
    default:       return "boot";
  }
}

static const char* message(RoomState s, bool baseline_set) {
  if (anySimulated()) return "Running - the tiles marked simulated have no sensor behind them";
  if (!baseline_set) return "Running, but the air index needs a clean-air baseline";
  switch (s) {
    case ST_OK:    return "Classroom conditions are normal";
    case ST_WARN:  return "Conditions outside the comfortable range";
    case ST_ALERT: return "Gas level high \xE2\x80\x94 ventilate the room";
    case ST_FAULT: return "A sensor stopped responding";
    default:       return "Starting up \xE2\x80\x94 warming the gas sensor";
  }
}

static void bandJson(String& s, ReadingId id) {
  switch (id) {
    case R_TEMP:  s += "[18,30]"; break;
    case R_HUM:   s += "[30,70]"; break;
    case R_AIR:   s += "[0,1.5]"; break;
    case R_NOISE: s += "[0,75]";  break;
    default:      s += "null";    break;   // pressure has no comfort band
  }
}

static int dpOf(ReadingId id) {
  if (id == R_AIR) return 2;
  if (id == R_TEMP) return 1;
  return 0;
}

static void handleState() {
  const uint32_t now = millis();
  String s;
  s.reserve(900);
  s += "{\"uptime_s\":";
  s += now / 1000;
  s += ",\"status\":\"";
  s += stateName(latest_out.state);
  s += "\",\"message\":\"";
  s += message(latest_out.state, baselineSet());
  s += "\",\"muted_until_s\":";
  s += engine.muteRemainingMs(now) / 1000;
  s += ",\"gas_baseline_set\":";
  s += baselineSet() ? "true" : "false";
  s += ",\"mq_r0\":";
  s += mqR0();
  s += ",\"mq_rs\":";
  s += mqRsEma();
  s += ",\"burn_in_complete\":";
  s += (now >= BURN_IN_MS) ? "true" : "false";
  s += ",\"readings\":{";

  for (uint8_t i = 0; i < R_COUNT; ++i) {
    const ReadingId id = ReadingId(i);
    if (i) s += ',';
    s += '"'; s += KEYS[i]; s += "\":{\"value\":";
    // A dead sensor reports null, never 0 — a zero would read as a real value.
    if (latest.v[i].valid) appendFloat(s, latest.v[i].value, dpOf(id));
    else s += "null";
    s += ",\"level\":\""; s += levelName(latest_out.level[i]);
    s += "\",\"band\":"; bandJson(s, id);
    if (id == R_AIR && !baselineSet())      s += ",\"note\":\"baseline not set\"";
    else if (!latest.v[i].valid)            s += ",\"note\":\"no response\"";
    // Synthetic values must never reach the page looking like measurements.
    else if (channelSimulated(id)) s += ",\"note\":\"simulated - no sensor for this\"";
    s += '}';
  }
  s += "}}";

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", s);
}

// 1080 points x 5 series is far too big for one String, so stream it.
static void handleHistory() {
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");

  String head = "{\"interval_s\":";
  head += HISTORY_INTERVAL_MS / 1000;
  head += ",\"count\":";
  head += history.size();
  head += ",\"series\":{";
  server.sendContent(head);

  for (uint8_t f = 0; f < R_COUNT; ++f) {
    const ReadingId id = ReadingId(f);
    String chunk;
    chunk.reserve(1024);
    if (f) chunk += ',';
    chunk += '"'; chunk += KEYS[f]; chunk += "\":[";

    for (uint16_t i = 0; i < history.size(); ++i) {
      if (i) chunk += ',';
      const int16_t raw = history.at(i, id);
      if (raw == HIST_MISSING) chunk += "null";   // the chart breaks the line here
      else appendFloat(chunk, raw / History::scaleOf(id), dpOf(id));

      if (chunk.length() > 900) { server.sendContent(chunk); chunk = ""; }
    }
    chunk += ']';
    server.sendContent(chunk);
  }
  server.sendContent("}}");
  server.sendContent("");
}

static void handleMute() {
  engine.mute(millis());
  String s = "{\"muted_until_s\":";
  s += engine.muteRemainingMs(millis()) / 1000;
  s += '}';
  server.send(200, "application/json", s);
}

static void handleCalibrate() {
  startCalibration(millis());
  String s = "{\"calibrating_s\":";
  s += CALIBRATE_MS / 1000;
  s += '}';
  server.send(200, "application/json", s);
}

// ---------------------------------------------------------------------- boot

static void startNetwork() {
  if (strlen(WIFI_SSID) > 0) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    const uint32_t started = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - started < WIFI_TIMEOUT_MS) {
      delay(250);
    }
  }
  // No credentials, or the room's WiFi is down: still serve the dashboard from
  // our own access point rather than becoming a box with three blinking lights.
  if (WiFi.status() != WL_CONNECTED) {
    ap_mode = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID);
    Serial.printf("SoftAP \"%s\" at %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
  } else {
    Serial.printf("WiFi %s at %s\n", WIFI_SSID, WiFi.localIP().toString().c_str());
  }
  // Works in both modes: on the SoftAP the name is the only thing a phone can
  // remember, and typing the IP is exactly what people get wrong.
  if (MDNS.begin(MDNS_HOST)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("http://%s.local\n", MDNS_HOST);
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_BLUE, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_LED_BLUE, HIGH);

  sensorsBegin();   // prints which chip it found, or that none answered
  engine.setMuteDuration(MUTE_DURATION_MS);

  startNetwork();

  server.on("/", HTTP_GET, [] {
    Serial.printf("GET / from %s\n", server.client().remoteIP().toString().c_str());
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html", INDEX_HTML);
  });
  // Anything else is almost always a phone probing for a captive portal; log it
  // so "the page will not load" can be told apart from "nothing ever arrived".
  server.onNotFound([] {
    Serial.printf("404 %s from %s\n", server.uri().c_str(),
                  server.client().remoteIP().toString().c_str());
    server.send(404, "text/plain", "not found");
  });
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/history", HTTP_GET, handleHistory);
  server.on("/api/mute", HTTP_POST, handleMute);
  server.on("/api/calibrate", HTTP_POST, handleCalibrate);
  server.begin();
}

void loop() {
  server.handleClient();
  // Bring-up without a network: 'c' recalibrates, matching the dashboard button.
  if (Serial.available() && Serial.read() == 'c') {
    startCalibration(millis());
    Serial.println("MQ: calibrating for 60 s, keep the air clean");
  }
  const uint32_t now = millis();

  if (now - last_sample_ms >= SAMPLE_INTERVAL_MS) {
    last_sample_ms = now;
    Readings r;                       // fresh each time: stale values never linger
    sensorsRead(r, now);
    latest = r;
    latest_out = engine.update(r, now, baselineSet());
    // Headless heartbeat: the box has no screen, and on the SoftAP the serial
    // line is the only way to see what it decided.
    static const char* kState[] = {"BOOT","OK","WARN","ALERT","FAULT"};
    Serial.printf("[%lus] %-5s air=%.2f temp=%.1f noise=%.0f r0=%.0f rs=%.0f\n",
                  now / 1000UL, kState[latest_out.state],
                  r.v[R_AIR].valid ? r.v[R_AIR].value : -1.0f,
                  r.v[R_TEMP].valid ? r.v[R_TEMP].value : -1.0f,
                  r.v[R_NOISE].valid ? r.v[R_NOISE].value : -1.0f, mqR0(), mqRsEma());
  }

  if (now - last_history_ms >= HISTORY_INTERVAL_MS) {
    last_history_ms = now;
    history.push(latest);
  }

  drawIndicators(latest_out, now);
}
