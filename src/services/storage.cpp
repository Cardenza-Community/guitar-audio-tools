#include "storage.h"
#include <SD.h>
#include <SPI.h>
#include <algorithm>

namespace storage {

// Cardputer ADV microSD slot (from the M5Cardputer sdcard example)
const int PIN_SCK = 40, PIN_MISO = 39, PIN_MOSI = 14, PIN_CS = 12;
const char *DIR = "/recordings";

static bool mounted = false;

bool begin() {
  if (mounted) return true;
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);
  if (!SD.begin(PIN_CS, SPI, 25000000)) return false;
  if (!SD.exists(DIR)) SD.mkdir(DIR);
  mounted = true;
  return true;
}

std::vector<Recording> recordings() {
  std::vector<Recording> list;
  if (!mounted) return list;
  File dir = SD.open(DIR);
  if (!dir) return list;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String name = f.name();
    if (!f.isDirectory() && name.startsWith("REC_") && name.endsWith(".wav")) {
      list.push_back({String(DIR) + "/" + name, name.substring(0, name.length() - 4), (uint32_t)f.size()});
    }
    f.close();
  }
  dir.close();
  std::sort(list.begin(), list.end(), [](const Recording &a, const Recording &b) { return a.name < b.name; });
  return list;
}

String newRecordingPath() {
  int highest = 0;
  for (const Recording &r : recordings()) highest = std::max(highest, (int)r.name.substring(4).toInt());
  char path[40];
  snprintf(path, sizeof(path), "%s/REC_%04d.wav", DIR, highest + 1);
  return String(path);
}

bool remove(const String &path) { return mounted && SD.remove(path); }

uint32_t freeMegabytes() {
  if (!mounted) return 0;
  return (uint32_t)((SD.totalBytes() - SD.usedBytes()) / (1024 * 1024));
}

}  // namespace storage
