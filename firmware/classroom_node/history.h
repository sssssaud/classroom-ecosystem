// Fixed-size ring buffer of readings. Pure; runs on a laptop.
// Values are stored as scaled int16 to keep 3 h of history inside ~11 KB.
#pragma once
#include <stdint.h>
#include "status.h"

static const int16_t HIST_MISSING = INT16_MIN;

class History {
 public:
  History(int16_t* storage, uint16_t capacity) : buf_(storage), cap_(capacity) {}

  void push(const Readings& r);
  uint16_t size() const { return count_; }
  uint16_t capacity() const { return cap_; }

  // index 0 is the oldest retained record.
  int16_t at(uint16_t index, ReadingId id) const;

  static int16_t encode(const Reading& r, ReadingId id);
  static float scaleOf(ReadingId id);

 private:
  int16_t* buf_;
  uint16_t cap_;
  uint16_t count_ = 0;
  uint16_t head_ = 0;   // next write slot
};
