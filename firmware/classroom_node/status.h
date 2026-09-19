// Pure decision logic: readings in, room state and outputs out.
// No Arduino calls, so this compiles and runs on a laptop.
#pragma once
#include <stdint.h>

enum Level : uint8_t { LVL_OK = 0, LVL_WARN, LVL_ALERT, LVL_OFFLINE };
enum RoomState : uint8_t { ST_BOOT = 0, ST_OK, ST_WARN, ST_ALERT, ST_FAULT };

enum ReadingId : uint8_t { R_TEMP = 0, R_HUM, R_PRESS, R_AIR, R_NOISE, R_COUNT };

struct Reading {
  float value = 0.0f;
  bool  valid = false;
};

struct Readings {
  Reading v[R_COUNT];
};

struct Thresholds {
  float temp_lo = 18.0f, temp_hi = 30.0f;
  float hum_lo = 30.0f,  hum_hi = 70.0f;
  float noise_hi = 75.0f;
  float air_warn = 1.5f, air_alert = 2.5f;
  float release = 0.85f;
  float band_margin = 0.05f;
  uint32_t dwell_ms = 10000;
};

struct StatusOut {
  RoomState state = ST_BOOT;
  Level level[R_COUNT] = { LVL_OFFLINE, LVL_OFFLINE, LVL_OFFLINE, LVL_OFFLINE, LVL_OFFLINE };
  bool buzzer = false;   // sound the buzzer right now
};

class StatusEngine {
 public:
  explicit StatusEngine(const Thresholds& t) : t_(t) {}

  // now_ms must be monotonic. baseline_set false hides the air index entirely
  // rather than reporting an index against a baseline nobody has set.
  StatusOut update(const Readings& r, uint32_t now_ms, bool baseline_set);

  void mute(uint32_t now_ms) { muted_until_ms_ = now_ms + mute_for_ms_; }
  void setMuteDuration(uint32_t ms) { mute_for_ms_ = ms; }
  bool muted(uint32_t now_ms) const { return int32_t(muted_until_ms_ - now_ms) > 0; }
  uint32_t muteRemainingMs(uint32_t now_ms) const {
    return muted(now_ms) ? muted_until_ms_ - now_ms : 0;
  }
  RoomState committed() const { return committed_; }

 private:
  Level levelFor(ReadingId id, const Reading& r, bool baseline_set) const;

  Thresholds t_;
  RoomState committed_ = ST_BOOT;
  RoomState pending_ = ST_BOOT;
  uint32_t  pending_since_ms_ = 0;
  bool      out_[R_COUNT] = { false, false, false, false, false };
  uint32_t  muted_until_ms_ = 0;
  uint32_t  mute_for_ms_ = 600000;
};
