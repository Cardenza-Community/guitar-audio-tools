// Where the recordings are: the SD card of the Cardputer ADV (SPI), or the
// flash memory of the M5StickS3 (LittleFS, no SD card).
// Recordings live in /recordings as REC_0001.wav, REC_0002.wav ... or under a
// name given after recording; nothing else on the card is touched.
#pragma once
#include <Arduino.h>
#include <FS.h>
#include <vector>

namespace storage {

// Mounts the card / the flash file system (only the first time);
// false = no card or not readable.
bool begin();

// The file system of the recordings (SD or LittleFS) for opening files.
fs::FS &fs();

struct Recording {
  String path;       // e.g. /recordings/REC_0003.wav
  String name;       // e.g. REC_0003
  uint32_t bytes;    // file size
};

// All recordings, oldest first.
std::vector<Recording> recordings();

// A path for a new recording (the next free number).
String newRecordingPath();

bool remove(const String &path);
bool exists(const String &path);
bool rename(const String &from, const String &to);

// The path of a recording with this name (e.g. "riff 1" -> /recordings/riff 1.wav).
String recordingPath(const String &name);

// Free space.
uint64_t freeBytes();
uint32_t freeMegabytes();

}  // namespace storage
