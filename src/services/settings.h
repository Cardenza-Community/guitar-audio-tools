// Settings kept in flash (NVS), surviving power-off:
// microphone calibration, reference pitch, gains...
#pragma once
#include <Arduino.h>

namespace settings {

void begin();

float getFloat(const char *key, float defaultValue);
void putFloat(const char *key, float value);
int getInt(const char *key, int defaultValue);
void putInt(const char *key, int value);

}  // namespace settings
