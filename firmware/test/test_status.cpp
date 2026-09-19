// Host test for the pure firmware logic. No board, no Arduino core.
//   g++ -std=c++17 -Iclassroom_node test/test_status.cpp classroom_node/*.cpp -o /tmp/t
#include <cassert>
#include <cstdio>
#include "status.h"
#include "history.h"

namespace {

const Thresholds T;   // defaults are what the firmware ships with

// A room that is comfortable on every channel.
Readings calm() {
  Readings r;
  const float v[R_COUNT] = { 22.0f, 50.0f, 1010.0f, 1.0f, 45.0f };
  for (uint8_t i = 0; i < R_COUNT; ++i) { r.v[i].value = v[i]; r.v[i].valid = true; }
  return r;
}

Readings with(ReadingId id, float value) {
  Readings r = calm();
  r.v[id].value = value;
  return r;
}

// Hold one set of readings until the dwell timer commits the new state.
StatusOut settle(StatusEngine& e, const Readings& r, uint32_t& t, bool baseline = true) {
  e.update(r, t, baseline);            // arms pending_ at this timestamp
  t += T.dwell_ms;
  return e.update(r, t, baseline);     // dwell elapsed, state commits
}

void test_boot_needs_dwell() {
  StatusEngine e(T);
  uint32_t t = 0;
  assert(e.update(calm(), t, true).state == ST_BOOT);   // never claims OK instantly
  t += T.dwell_ms;
  assert(e.update(calm(), t, true).state == ST_OK);
}

void test_brief_excursion_is_ignored() {
  StatusEngine e(T);
  uint32_t t = 0;
  settle(e, calm(), t);

  e.update(with(R_TEMP, 33.0f), t, true);
  t += T.dwell_ms - 1;
  assert(e.update(with(R_TEMP, 33.0f), t, true).state == ST_OK);   // still under dwell

  t += 1;
  StatusOut o = e.update(with(R_TEMP, 33.0f), t, true);
  assert(o.state == ST_WARN);
  assert(o.level[R_TEMP] == LVL_WARN);
  assert(o.level[R_HUM] == LVL_OK);
}

void test_band_reentry_margin() {
  StatusEngine e(T);
  uint32_t t = 0;
  settle(e, calm(), t);
  settle(e, with(R_TEMP, 31.0f), t);                     // out of band -> WARN

  // Band is 18..30, margin 5% of 12 = 0.6, so 29.8 is inside the band but
  // inside the margin too: still warn, no flapping on the boundary.
  assert(settle(e, with(R_TEMP, 29.8f), t).state == ST_WARN);
  assert(settle(e, with(R_TEMP, 29.0f), t).state == ST_OK);
}

void test_gas_alert_and_release() {
  StatusEngine e(T);
  uint32_t t = 0;
  settle(e, calm(), t);

  StatusOut o = settle(e, with(R_AIR, 3.0f), t);
  assert(o.state == ST_ALERT);
  assert(o.level[R_AIR] == LVL_ALERT);
  assert(o.buzzer);

  // 2.2 is below air_alert but above the 85% release point (2.125): stays latched.
  assert(settle(e, with(R_AIR, 2.2f), t).state == ST_ALERT);
  // 2.0 clears alert but is still over the warn release point (1.275).
  assert(settle(e, with(R_AIR, 2.0f), t).state == ST_WARN);
  assert(settle(e, with(R_AIR, 1.0f), t).state == ST_OK);
}

void test_mute_silences_sound_only() {
  StatusEngine e(T);
  e.setMuteDuration(60000);
  uint32_t t = 0;
  settle(e, calm(), t);
  assert(settle(e, with(R_AIR, 3.0f), t).buzzer);

  e.mute(t);
  StatusOut o = e.update(with(R_AIR, 3.0f), t, true);
  assert(!o.buzzer);
  assert(o.state == ST_ALERT);            // dashboard and red LED stay lit
  assert(o.level[R_AIR] == LVL_ALERT);
  assert(e.muteRemainingMs(t) == 60000);

  t += 60001;
  assert(e.update(with(R_AIR, 3.0f), t, true).buzzer);   // mute expires on its own
}

void test_dead_sensor_is_a_fault_not_a_zero() {
  StatusEngine e(T);
  uint32_t t = 0;
  settle(e, calm(), t);

  Readings r = calm();
  r.v[R_TEMP].valid = false;
  StatusOut o = settle(e, r, t);
  assert(o.state == ST_FAULT);
  assert(o.level[R_TEMP] == LVL_OFFLINE);
}

void test_gas_alert_outranks_fault() {
  StatusEngine e(T);
  uint32_t t = 0;
  settle(e, calm(), t);

  Readings r = with(R_AIR, 3.0f);
  r.v[R_TEMP].valid = false;             // unrelated channel is dead
  StatusOut o = settle(e, r, t);
  assert(o.state == ST_ALERT);           // hazard must not be masked by the fault
  assert(o.buzzer);
  assert(o.level[R_TEMP] == LVL_OFFLINE);
}

void test_missing_baseline_is_not_a_fault() {
  StatusEngine e(T);
  uint32_t t = 0;
  StatusOut o = settle(e, calm(), t, /*baseline=*/false);
  assert(o.level[R_AIR] == LVL_OFFLINE);   // no baseline, no number invented
  assert(o.state == ST_OK);                // and not reported as broken wiring
}

void test_history_encoding() {
  assert(History::encode({22.5f, true}, R_TEMP) == 225);
  assert(History::encode({1.23f, true}, R_AIR) == 123);
  assert(History::encode({1013.0f, true}, R_PRESS) == 10130);
  assert(History::encode({0.0f, false}, R_TEMP) == HIST_MISSING);
  assert(History::encode({9999.0f, true}, R_PRESS) == 32766);   // clamped, not wrapped
}

void test_history_ring_wraps_oldest_first() {
  int16_t storage[3 * R_COUNT];
  History h(storage, 3);
  assert(h.size() == 0);
  assert(h.at(0, R_TEMP) == HIST_MISSING);   // reading past the end is missing

  for (int i = 1; i <= 5; ++i) h.push(with(R_TEMP, float(i)));

  assert(h.size() == 3);
  assert(h.at(0, R_TEMP) == 30);   // pushes 1 and 2 fell off the back
  assert(h.at(1, R_TEMP) == 40);
  assert(h.at(2, R_TEMP) == 50);
  assert(h.at(3, R_TEMP) == HIST_MISSING);

  Readings dead = calm();
  dead.v[R_NOISE].valid = false;
  h.push(dead);
  assert(h.at(2, R_NOISE) == HIST_MISSING);   // gap survives the round trip
  assert(h.at(2, R_TEMP) == 220);
}

}  // namespace

int main() {
  test_boot_needs_dwell();
  test_brief_excursion_is_ignored();
  test_band_reentry_margin();
  test_gas_alert_and_release();
  test_mute_silences_sound_only();
  test_dead_sensor_is_a_fault_not_a_zero();
  test_gas_alert_outranks_fault();
  test_missing_baseline_is_not_a_fault();
  test_history_encoding();
  test_history_ring_wraps_oldest_first();
  std::printf("all status/history tests passed\n");
  return 0;
}
