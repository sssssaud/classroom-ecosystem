#include "history.h"

float History::scaleOf(ReadingId id) {
  // Air index needs two decimals; everything else one. All fit in int16:
  // pressure 1100 hPa -> 11000, well inside 32767.
  return (id == R_AIR) ? 100.0f : 10.0f;
}

int16_t History::encode(const Reading& r, ReadingId id) {
  if (!r.valid) return HIST_MISSING;
  const float scaled = r.value * scaleOf(id);
  if (scaled > 32766.0f) return 32766;
  if (scaled < -32767.0f) return -32767;
  return int16_t(scaled);
}

void History::push(const Readings& r) {
  int16_t* slot = buf_ + (uint32_t(head_) * R_COUNT);
  for (uint8_t i = 0; i < R_COUNT; ++i) {
    slot[i] = encode(r.v[i], ReadingId(i));
  }
  head_ = (head_ + 1) % cap_;
  if (count_ < cap_) ++count_;
}

int16_t History::at(uint16_t index, ReadingId id) const {
  if (index >= count_) return HIST_MISSING;
  // Once full, the oldest record sits at head_; before that, at 0.
  const uint16_t start = (count_ == cap_) ? head_ : 0;
  const uint16_t slot = (start + index) % cap_;
  return buf_[uint32_t(slot) * R_COUNT + id];
}
