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
bool has(const char *key);
void remove(const char *key);
// text; `out` gets defaultValue when the key was never saved
void getString(const char *key, char *out, size_t size, const char *defaultValue);
void putString(const char *key, const char *value);

}  // namespace settings
