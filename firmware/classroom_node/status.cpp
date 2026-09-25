#include "status.h"

namespace {

// Once a reading has left its band it must come back inside by `margin` before
// it counts as normal again. Stops a value sitting exactly on the line from
// toggling the LED every sample.
bool inBand(float v, float lo, float hi, bool was_out, float margin_frac) {
  const float m = (hi - lo) * margin_frac;
  return was_out ? (v >= lo + m && v <= hi - m) : (v >= lo && v <= hi);
}

bool over(float v, float thr, bool was_over, float release) {
  return was_over ? (v > thr * release) : (v > thr);
}

}  // namespace

Level StatusEngine::levelFor(ReadingId id, const Reading& r, bool baseline_set) const {
  if (!r.valid) return LVL_OFFLINE;

  switch (id) {
    case R_TEMP:
      return inBand(r.value, t_.temp_lo, t_.temp_hi, out_[id], t_.band_margin) ? LVL_OK : LVL_WARN;
    case R_HUM:
      return inBand(r.value, t_.hum_lo, t_.hum_hi, out_[id], t_.band_margin) ? LVL_OK : LVL_WARN;
    case R_NOISE:
      return over(r.value, t_.noise_hi, out_[id], t_.release) ? LVL_WARN : LVL_OK;
    case R_AIR:
      if (!baseline_set) return LVL_OFFLINE;   // no baseline, no number
      if (over(r.value, t_.air_alert, out_[id], t_.release)) return LVL_ALERT;
      return over(r.value, t_.air_warn, out_[id], t_.release) ? LVL_WARN : LVL_OK;
    case R_PRESS:
    default:
      return LVL_OK;   // informational only, no comfort band
  }
}

StatusOut StatusEngine::update(const Readings& r, uint32_t now_ms, bool baseline_set) {
  StatusOut out;

  bool any_alert = false, any_warn = false, any_fault = false;
  for (uint8_t i = 0; i < R_COUNT; ++i) {
    const ReadingId id = ReadingId(i);
    const Level lv = levelFor(id, r.v[i], baseline_set);
    out.level[i] = lv;
    out_[i] = (lv == LVL_WARN || lv == LVL_ALERT);

    if (lv == LVL_ALERT) any_alert = true;
    else if (lv == LVL_WARN) any_warn = true;
    // A missing air index without a baseline is expected, not a wiring fault.
    // Noise is useful, but not safety-critical; a dead mic should show as "no
    // response" on the dashboard without turning the classroom status red.
    else if (lv == LVL_OFFLINE && !(id == R_AIR && !baseline_set) && id != R_NOISE) any_fault = true;
  }

  // Gas outranks a dead sensor: a real hazard must not be masked by a fault
  // on an unrelated channel.
  RoomState instant = ST_OK;
  if (any_alert)      instant = ST_ALERT;
  else if (any_fault) instant = ST_FAULT;
  else if (any_warn)  instant = ST_WARN;

  if (instant != pending_) {
    pending_ = instant;
    pending_since_ms_ = now_ms;
  }
  if (pending_ != committed_ && (now_ms - pending_since_ms_) >= t_.dwell_ms) {
    committed_ = pending_;
  }

  out.state = committed_;
  out.buzzer = (committed_ == ST_ALERT) && !muted(now_ms);
  return out;
}
