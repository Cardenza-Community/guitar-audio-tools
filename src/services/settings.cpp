#include "settings.h"
#include <Preferences.h>

namespace settings {

static Preferences prefs;

// The storage keeps its first name "audiotools" (from before the firmware was
// called Guitar Audio Tools): renaming it would lose the saved settings.
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
bool has(const char *key) { return prefs.isKey(key); }
void remove(const char *key) { prefs.remove(key); }
void getString(const char *key, char *out, size_t size, const char *defaultValue) {
  if (!prefs.isKey(key) || prefs.getString(key, out, size) == 0) snprintf(out, size, "%s", defaultValue);
}
void putString(const char *key, const char *value) { prefs.putString(key, value); }

}  // namespace settings
