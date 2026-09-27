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
    String lower = name;
    lower.toLowerCase();                  // other apps may write .WAV
    if (!f.isDirectory() && lower.endsWith(".wav")) {
      list.push_back({String(DIR) + "/" + name, name.substring(0, name.length() - 4), (uint32_t)f.size()});
    }
    f.close();
  }
  dir.close();
  std::sort(list.begin(), list.end(), [](const Recording &a, const Recording &b) { return a.name < b.name; });
  return list;
}

// The next free REC_nnnn.wav. Checked against the card as well, so that a file
// of any other app is never overwritten (FAT does not tell REC_0005.WAV from
// REC_0005.wav, and an open for writing would empty it).
String newRecordingPath() {
  int number = 0;
  for (const Recording &r : recordings()) {
    String upper = r.name;
    upper.toUpperCase();
    if (upper.startsWith("REC_")) number = std::max(number, (int)r.name.substring(4).toInt());
  }
  char path[40];
  do {
    number++;
    snprintf(path, sizeof(path), "%s/REC_%04d.wav", DIR, number);
  } while (SD.exists(path));
  return String(path);
}

bool remove(const String &path) { return mounted && SD.remove(path); }
bool exists(const String &path) { return mounted && SD.exists(path); }
bool rename(const String &from, const String &to) { return mounted && SD.rename(from, to); }
String recordingPath(const String &name) { return String(DIR) + "/" + name + ".wav"; }

uint32_t freeMegabytes() {
  if (!mounted) return 0;
  return (uint32_t)((SD.totalBytes() - SD.usedBytes()) / (1024 * 1024));
}

}  // namespace storage
