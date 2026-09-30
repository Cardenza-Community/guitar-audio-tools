#include "storage.h"
#include <algorithm>
#ifdef BOARD_STICKS3
#include <LittleFS.h>
#else
#include <SD.h>
#include <SPI.h>
#endif

namespace storage {

const char *DIR = "/recordings";
static bool mounted = false;

#ifdef BOARD_STICKS3

// M5StickS3: no SD card - the recordings live in the flash memory, in the
// "spiffs" partition (about 5.4 MB, partitions_sticks3.csv), with LittleFS.
fs::FS &fs() { return LittleFS; }

bool begin() {
  if (mounted) return true;
  if (!LittleFS.begin(true)) return false;          // true: format it the first time
  if (!LittleFS.exists(DIR)) LittleFS.mkdir(DIR);
  mounted = true;
  return true;
}

uint64_t freeBytes() { return mounted ? LittleFS.totalBytes() - LittleFS.usedBytes() : 0; }

#else

// Cardputer ADV microSD slot (from the M5Cardputer sdcard example)
const int PIN_SCK = 40, PIN_MISO = 39, PIN_MOSI = 14, PIN_CS = 12;

fs::FS &fs() { return SD; }

bool begin() {
  if (mounted) return true;
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);
  if (!SD.begin(PIN_CS, SPI, 25000000)) return false;
  if (!fs().exists(DIR)) SD.mkdir(DIR);
  mounted = true;
  return true;
}

uint64_t freeBytes() { return mounted ? SD.totalBytes() - SD.usedBytes() : 0; }

#endif

std::vector<Recording> recordings() {
  std::vector<Recording> list;
  if (!mounted) return list;
  File dir = fs().open(DIR);
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
  } while (fs().exists(path));
  return String(path);
}

bool remove(const String &path) { return mounted && fs().remove(path); }
bool exists(const String &path) { return mounted && fs().exists(path); }
bool rename(const String &from, const String &to) { return mounted && fs().rename(from, to); }
String recordingPath(const String &name) { return String(DIR) + "/" + name + ".wav"; }

uint32_t freeMegabytes() { return (uint32_t)(freeBytes() / (1024 * 1024)); }

}  // namespace storage
