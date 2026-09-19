// Hardware reads. Everything Arduino-specific lives behind this interface so
// status.cpp / history.cpp stay testable on the laptop.
#pragma once
#include <stdint.h>
#include "status.h"

// false means the BME280 never answered on either address; the node still runs
// and reports that channel offline rather than refusing to boot.
bool sensorsBegin();

// Fills every channel. Any sensor that fails sets valid=false — never 0.
void sensorsRead(Readings& out, uint32_t now_ms);

// True when no BME280 answered, so temperature/humidity/pressure are synthetic.
// Callers must label those readings; they are not measurements.
bool bmeSimulated();

// ---- MQ-135 clean-air baseline ----
bool  baselineSet();
float baselineR0();
void  startCalibration(uint32_t now_ms);
bool  calibrating(uint32_t now_ms);
void  clearBaseline();
