#include "settings.h"
#include <Preferences.h>

namespace settings {

static Preferences prefs;

void begin() { prefs.begin("audiotools", false); }

// isKey() first: Preferences logs an error when reading a key that was never saved
float getFloat(const char *key, float defaultValue) {
  return prefs.isKey(key) ? prefs.getFloat(key, defaultValue) : defaultValue;
}
void putFloat(const char *key, float value) { prefs.putFloat(key, value); }
int getInt(const char *key, int defaultValue) {
  return prefs.isKey(key) ? prefs.getInt(key, defaultValue) : defaultValue;
}
void putInt(const char *key, int value) { prefs.putInt(key, value); }

}  // namespace settings
