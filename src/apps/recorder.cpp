// Recorder: WAV recordings on the SD card (/recordings/REC_0001.wav ...),
// 16 kHz mono 16-bit (about 115 MB per hour), and playback on the speaker.
//
// Recording: the samples are collected in a 16 KB buffer and written in one
// go; the audio input keeps 0.5 s, enough for the occasional slow SD write.
// The WAV header is written with size 0 first and fixed when recording stops.
// Playback: the microphone is stopped (it shares the I2S bus with the speaker),
// the file is streamed to the speaker in blocks (three buffers take turns),
// then the microphone starts again.
// After a recording it is normalized: the whole file is amplified so that its
// loudest moment reaches -1 dBFS (at most +30 dB) - the microphone is weak, so
// recordings are otherwise quiet on the speaker and on a PC. Then its name can
// be typed (letters, digits, - _ and spaces;
// Enter saves, an empty name or Esc keeps REC_0005 etc.).
// Keys: Enter record / stop, space play / stop, , / previous / next recording,
//       Del delete (press twice), ; . microphone gain (volume while playing).
//
// M5StickS3 (no SD card, no keyboard): a simple recorder of ideas in the flash
// memory (about 5.5 minutes in total): 8 kHz (the microphone's 16 kHz halved
// by the dsp::Decimator), no names. A record / stop, B play / stop, hold B
// next recording; the menu has the previous recording, delete (choose it
// twice), gain and volume.
#include <M5Unified.h>
#include <memory>
#include <vector>
#include "apps.h"
#include "decimator.h"
#include "level.h"
#include "wav.h"
#include "../hw/board.h"
#include "../hw/es8311.h"
#include "../services/audio_in.h"
#include "../services/settings.h"
#include "../services/storage.h"
#include "../services/ui.h"

namespace {

const int RATE = 16000;                  // microphone
#ifdef BOARD_STICKS3
const int FILE_RATE = 8000;              // the recordings (flash memory is small)
#else
const int FILE_RATE = RATE;
#endif
const int DECIMATE = RATE / FILE_RATE;
enum Command { PREVIOUS = 1 };           // Stick menu (Key::command)
// StickS3: its own settings, loud by default (a quiet microphone and speaker)
#ifdef BOARD_STICKS3
const char *GAIN_KEY = "g_rec_s", *VOLUME_KEY = "rec_vol_s";
const int DEFAULT_GAIN = 30, DEFAULT_VOLUME = 10;
#else
const char *GAIN_KEY = "g_rec", *VOLUME_KEY = "rec_vol";
const int DEFAULT_GAIN = 24, DEFAULT_VOLUME = 7;
#endif
const size_t WRITE_BLOCK = 8192;         // samples written at once (16 KB)
const size_t PLAY_BLOCK = 2048;          // samples per speaker buffer
const uint32_t DELETE_CONFIRM_MS = 3000;
const size_t MAX_NAME = 20;

enum class Mode { Idle, Recording, Normalizing, Playing };
const size_t NORMALIZE_BLOCK = 4096;     // samples per step (4 steps per loop pass)

class RecorderApp : public App {
 public:
  const char *name() const override { return "Recorder"; }
  uint32_t sampleRate() const override { return RATE; }
  int micGain() const override { return settings::getInt(GAIN_KEY, DEFAULT_GAIN); }

  void enter() override {
    haveCard_ = storage::begin();
    refresh();
    selected_ = list_.empty() ? -1 : (int)list_.size() - 1;
    mode_ = Mode::Idle;
    naming_ = false;
    message_ = haveCard_ ? "" : board::hasSdCard() ? "no SD card" : "no memory for recordings";
    decimator_.reset(DECIMATE > 1 ? new dsp::Decimator(DECIMATE) : nullptr);
    volume_ = settings::getInt(VOLUME_KEY, DEFAULT_VOLUME);
  }

  void exit() override {
    if (mode_ == Mode::Recording) stopRecording();
    while (mode_ == Mode::Normalizing) normalizeStep();  // finish it before leaving
    if (mode_ == Mode::Playing) stopPlaying(false);
    std::vector<int16_t>().swap(block_);
  }

  void process(const int16_t *samples, size_t count) override {
    // level of the incoming sound (shown also when not recording)
    int peak = 0;
    for (size_t i = 0; i < count; i++) peak = std::max(peak, abs((int)samples[i]));
    level_ = std::max(level_ * 0.9f, (float)peak);
    if (peak > 32000) clipMs_ = millis();
    if (mode_ != Mode::Recording) return;
    recPeak_ = std::max(recPeak_, peak);
    if (decimator_) {                                   // StickS3: 16 -> 8 kHz
      float reduced[256 / 2 + 2];
      for (size_t done = 0; done < count; done += 256) {
        size_t n = decimator_->process(samples + done, std::min<size_t>(256, count - done), reduced);
        for (size_t i = 0; i < n; i++) add((int16_t)constrain(lroundf(reduced[i]), -32768, 32767));
      }
    } else {
      for (size_t i = 0; i < count; i++) add(samples[i]);
    }
  }

  void tick() override {
    if (mode_ == Mode::Recording && full_) {             // no space left
      stopRecording();
      message_ = "memory full - recording stopped";
    }
    if (mode_ == Mode::Playing) feedSpeaker();
    if (mode_ == Mode::Normalizing) normalizeStep();
  }

  // the M5StickS3 action menu (double click on A)
  int actions(const Action *&items) const override {
    static const Action ACTIONS[] = {
        {"Previous recording", {0, false, false, PREVIOUS}},
        {"Delete (choose twice)", {0, false, true}},
        {"Gain / volume +", {';'}},
        {"Gain / volume -", {'.'}},
    };
    items = ACTIONS;
    return sizeof(ACTIONS) / sizeof(ACTIONS[0]);
  }

  int help(const ui::HelpItem *&items) const override {
#ifdef BOARD_STICKS3
    static const ui::HelpItem HELP[] = {
        {nullptr, "A: record / stop"},
        {nullptr, "B: play / stop, hold B: next"},
        {nullptr, "8 kHz WAV in the flash memory"},
        {nullptr, "~5.5 minutes in total"},
    };
#else
    static const ui::HelpItem HELP[] = {
        {"Enter", "record / stop"},
        {"space", "play / stop the selected"},
        {", /", "previous / next recording"},
        {"Del", "delete (press twice)"},
        {"; .", "mic gain (volume: playing)"},
        {nullptr, "files: /recordings on the SD"},
        {nullptr, "16 kHz mono WAV, ~115 MB/h"},
    };
#endif
    items = HELP;
    return sizeof(HELP) / sizeof(HELP[0]);
  }

  bool capturesKeys() const override { return naming_; }

  void onKey(const Key &original) override {
    if (!haveCard_) return;
    if (naming_) {
      nameKey(original);
      return;
    }
    // StickS3: B = play / stop, hold B = next, menu "previous" = ","
    Key key = original;
    if (!board::hasKeyboard()) {
      if (key.ch == '/') key.ch = ' ';
      else if (key.ch == ',') key.ch = '/';
      if (key.command == PREVIOUS) key.ch = ',';
    }
    if (key.enter) {
      if (mode_ == Mode::Idle) startRecording();
      else if (mode_ == Mode::Recording) stopRecording();
    }
    if (key.ch == ' ') {
      if (mode_ == Mode::Idle) startPlaying();
      else if (mode_ == Mode::Playing) stopPlaying(true);
    }
    if (mode_ == Mode::Idle && !list_.empty()) {
      int n = (int)list_.size();
      if (board::hasKeyboard()) {
        if (key.ch == ',') selected_ = std::max(0, selected_ - 1);
        if (key.ch == '/') selected_ = std::min(n - 1, selected_ + 1);
      } else {                                          // StickS3: round, the newest is selected
        if (key.ch == ',') selected_ = (selected_ + n - 1) % n;
        if (key.ch == '/') selected_ = (selected_ + 1) % n;
      }
    }
    if (key.del && mode_ == Mode::Idle && selected_ >= 0) {
      if (deleteAsked()) {
        storage::remove(list_[selected_].path);
        refresh();
        selected_ = std::min(selected_, (int)list_.size() - 1);
        deleteAskedMs_ = 0;
        message_ = "deleted";
      } else {
        deleteAskedMs_ = millis() | 1;                // 0 means "not asked"
      }
    }
    if (key.ch == ';' || key.ch == '.') {
      int step = key.ch == ';' ? 1 : -1;
      if (mode_ == Mode::Playing) {
        volume_ = constrain(volume_ + step, 0, 10);
        M5.Speaker.setVolume(board::speakerVolume(volume_));
        settings::putInt(VOLUME_KEY, volume_);
        ui::flashLevel("VOLUME", volume_, 10, "");
      } else {
        es8311::setPgaGain(es8311::pgaGain() + 3 * step);
        settings::putInt(GAIN_KEY, es8311::pgaGain());
        ui::flashGain(es8311::pgaGain());
      }
    }
  }

  void draw(M5Canvas &c) override {
    if (naming_) {
      drawNaming(c);
      return;
    }
    char right[24];
    if (!haveCard_) {
      snprintf(right, sizeof(right), board::hasSdCard() ? "no SD" : "no memory");
    } else if (board::hasSdCard()) {
      snprintf(right, sizeof(right), "%u MB free", (unsigned)freeMb_);
    } else {                                            // StickS3: minutes left
      uint32_t seconds = (uint32_t)(freeBytes_ / (FILE_RATE * 2));
      snprintf(right, sizeof(right), "%u:%02u left", (unsigned)(seconds / 60), (unsigned)(seconds % 60));
    }
    ui::header("Recorder", right);

    // state and time
    c.setTextSize(3);
    c.setCursor(4, 18);
    if (mode_ == Mode::Recording) {
      c.setTextColor(RED);
      c.print("REC ");
      printTime(c, recorded_ / FILE_RATE);
    } else if (mode_ == Mode::Playing) {
      c.setTextColor(GREEN);
      c.print("PLAY ");
      printTime(c, played_ / playRate_);
    } else if (mode_ == Mode::Normalizing) {
      c.setTextColor(YELLOW);
      c.printf("LOUDER %d%%", (int)(100.0f * (normPos_ - dsp::WAV_HEADER_BYTES) /
                                     std::max<uint32_t>(1, normEnd_ - dsp::WAV_HEADER_BYTES)));
    } else {
      c.setTextColor(WHITE);
      c.print(haveCard_ ? "READY" : board::hasSdCard() ? "NO SD CARD" : "NO MEMORY");
    }

    // input level (not while playing: the microphone is off)
    if (mode_ != Mode::Playing) {
      int w = (int)(232 * std::min(1.0f, level_ / 32768.0f));
      bool clip = millis() - clipMs_ < 1000;
      c.fillRect(4, 48, w, 8, clip ? RED : w > 160 ? YELLOW : GREEN);
      c.drawRect(4, 48, 232, 8, WHITE);
      if (clip) {
        c.setTextSize(1);
        c.setTextColor(RED);
        c.setCursor(190, 38);
        c.print("CLIP");
      }
    }

    // the selected recording (long names in small letters)
    c.setTextSize(2);
    c.setCursor(4, 66);
    if (selected_ >= 0) {
      const storage::Recording &r = list_[selected_];
      if (r.name.length() > 10) c.setTextSize(1);
      c.setTextColor(YELLOW);
      c.printf("%s ", selected_ > 0 ? "<" : " ");
      c.setTextColor(WHITE);
      c.print(r.name);
      c.print(" ");
      printTime(c, r.bytes > dsp::WAV_HEADER_BYTES ? (r.bytes - dsp::WAV_HEADER_BYTES) / 2 / FILE_RATE : 0);
      c.setTextColor(YELLOW);
      c.print(selected_ < (int)list_.size() - 1 ? " >" : "");
      c.setTextSize(1);
      c.setTextColor(WHITE);
      c.setCursor(4, 86);
      c.printf("%d of %d recordings", selected_ + 1, (int)list_.size());
    } else if (haveCard_) {
      c.setTextColor(WHITE);
      c.print("no recordings yet");
    }

    // messages
    c.setTextSize(1);
    c.setCursor(4, 100);
    if (deleteAsked()) {
      c.setTextColor(ORANGE);
      c.print(board::hasKeyboard() ? "delete? press Del again" : "delete? choose Delete again");
    } else if (message_[0]) {
      c.setTextColor(ORANGE);
      c.print(message_);
    } else if (mode_ == Mode::Idle && haveCard_) {
      c.setTextColor(WHITE);
      c.print("Enter: record   space: play");
    }
    if (audio_in::droppedSamples() > droppedAtStart_ && mode_ == Mode::Recording) {
      c.setTextColor(RED);
      c.setCursor(4, 112);
      c.print(board::hasSdCard() ? "SD card too slow: gaps!" : "memory too slow: gaps!");
    }
    ui::footerHelp();
  }

 private:
  bool deleteAsked() const { return deleteAskedMs_ != 0 && millis() - deleteAskedMs_ < DELETE_CONFIRM_MS; }

  static void printTime(M5Canvas &c, uint32_t seconds) {
    c.printf("%u:%02u", (unsigned)(seconds / 60), (unsigned)(seconds % 60));
  }

  void refresh() {
    list_ = storage::recordings();
    freeMb_ = storage::freeMegabytes();
    freeBytes_ = storage::freeBytes();
  }

  // ---------- recording ----------

  void startRecording() {
    path_ = storage::newRecordingPath();
    file_ = storage::fs().open(path_, FILE_WRITE);
    if (!file_) {
      message_ = "cannot create the file";
      return;
    }
    uint8_t header[dsp::WAV_HEADER_BYTES];
    dsp::makeWavHeader(header, FILE_RATE, 0);
    file_.write(header, sizeof(header));
    block_.clear();
    block_.reserve(WRITE_BLOCK);
    if (decimator_) decimator_->reset();
    full_ = false;
    recorded_ = 0;
    recPeak_ = 0;
    droppedAtStart_ = audio_in::droppedSamples();
    message_ = "";
    mode_ = Mode::Recording;
  }

  void add(int16_t sample) {
    block_.push_back(sample);
    recorded_++;
    if (block_.size() == WRITE_BLOCK) flush();
  }

  void flush() {
    size_t bytes = block_.size() * sizeof(int16_t);
    if (bytes && file_.write((const uint8_t *)block_.data(), bytes) != bytes) full_ = true;
    block_.clear();
  }

  void stopRecording() {
    flush();
    // position(), not size(): size() of a file open for writing is still 0
    uint32_t dataBytes = file_.position() - dsp::WAV_HEADER_BYTES;
    uint8_t header[dsp::WAV_HEADER_BYTES];
    dsp::makeWavHeader(header, FILE_RATE, dataBytes);
    file_.seek(0);
    file_.write(header, sizeof(header));
    file_.close();
    normGain_ = dsp::normalizeGain(recPeak_);
    Serial.printf("recorded %s, %u bytes, peak %d, gain %.1f dB\n", path_.c_str(), (unsigned)dataBytes,
                  recPeak_, 20 * log10f(normGain_));
    if (normGain_ > 1.12f) {                            // more than 1 dB to gain
      file_ = storage::fs().open(path_, "r+");
      if (file_) {
        normPos_ = dsp::WAV_HEADER_BYTES;
        normEnd_ = file_.size();
        normBuffer_.assign(NORMALIZE_BLOCK, 0);
        mode_ = Mode::Normalizing;
        return;
      }
    }
    finishRecording();
  }

  // amplifies a few blocks of the file in place, then continues next pass
  void normalizeStep() {
    for (int step = 0; step < 4 && normPos_ < normEnd_; step++) {
      size_t bytes = std::min<size_t>(NORMALIZE_BLOCK * sizeof(int16_t), normEnd_ - normPos_);
      file_.seek(normPos_);
      int got = file_.read((uint8_t *)normBuffer_.data(), bytes);
      if (got <= 0) {
        normPos_ = normEnd_;
        break;
      }
      dsp::applyGain(normBuffer_.data(), got / sizeof(int16_t), normGain_);
      file_.seek(normPos_);
      file_.write((const uint8_t *)normBuffer_.data(), got);
      normPos_ += got;
    }
    if (normPos_ >= normEnd_) {
      file_.close();
      std::vector<int16_t>().swap(normBuffer_);
      finishRecording();
    }
  }

  void finishRecording() {
    mode_ = Mode::Idle;
    refresh();
    selectPath(path_);                                  // the new recording
    naming_ = board::hasKeyboard();                     // ask for a name (not on the Stick)
    name_ = "";
    message_ = "";
  }

  void selectPath(const String &path) {
    for (size_t i = 0; i < list_.size(); i++)
      if (list_[i].path == path) selected_ = (int)i;
  }

  // ---------- naming a new recording ----------

  void nameKey(const Key &key) {
    if (key.enter) {
      finishNaming();
      return;
    }
    if (key.del && name_.length() > 0) name_.remove(name_.length() - 1);
    if (key.ch == '`') {                                // Esc: keep the automatic name
      naming_ = false;
      return;
    }
    char ch = key.ch;
    bool allowed = isalnum((unsigned char)ch) || ch == '-' || ch == '_' || ch == ' ';
    if (allowed && name_.length() < MAX_NAME) name_ += ch;
  }

  void finishNaming() {
    name_.trim();
    if (name_.length() == 0) {                          // keep REC_xxxx
      naming_ = false;
      return;
    }
    String target = storage::recordingPath(name_);
    if (storage::exists(target)) {
      message_ = "this name exists - another one";
      return;
    }
    if (!storage::rename(path_, target)) {
      message_ = "could not rename";
      return;
    }
    refresh();
    selectPath(target);
    naming_ = false;
    message_ = "";
  }

  void drawNaming(M5Canvas &c) {
    ui::header("Recorder", "SAVED");
    c.setTextSize(1);
    c.setTextColor(WHITE);
    c.setCursor(4, 18);
    c.print("Name the recording:");
    c.drawRect(2, 30, 236, 24, YELLOW);
    c.setTextSize(2);
    c.setCursor(6, 35);
    c.print(name_);
    if ((millis() / 400) % 2) c.print("_");               // blinking cursor
    c.setTextSize(1);
    c.setCursor(4, 60);
    c.print("Enter: save");
    c.setCursor(4, 71);
    c.printf("empty or Esc: keep %s", list_.empty() || selected_ < 0 ? "" : list_[selected_].name.c_str());
    c.setCursor(4, 82);
    c.print("letters, digits, - _ and spaces");
    if (message_[0]) {
      c.setTextColor(ORANGE);
      c.setCursor(4, 98);
      c.print(message_);
    }
  }

  // ---------- playback ----------

  void startPlaying() {
    if (selected_ < 0) return;
    file_ = storage::fs().open(list_[selected_].path, FILE_READ);
    uint8_t header[dsp::WAV_HEADER_BYTES];
    uint32_t dataBytes;
    if (!file_ || file_.read(header, sizeof(header)) != sizeof(header) ||
        !dsp::readWavHeader(header, playRate_, dataBytes)) {
      if (file_) file_.close();
      message_ = "not a playable WAV file";
      return;
    }
    audio_in::stop();                                  // the speaker needs the I2S bus
    board::prepareSpeaker(playRate_);                  // StickS3: no rate conversion
    M5.Speaker.begin();
    M5.Speaker.setVolume(board::speakerVolume(volume_));
    for (auto &b : playBuffers_) b.assign(PLAY_BLOCK, 0);
    nextBuffer_ = 0;
    played_ = 0;
    fileDone_ = false;
    message_ = "";
    mode_ = Mode::Playing;
  }

  // keep up to two blocks queued on the speaker channel; three buffers take
  // turns, so a buffer is only refilled after the speaker has released it
  void feedSpeaker() {
    while (!fileDone_ && M5.Speaker.isPlaying(0) < 2) {
      std::vector<int16_t> &b = playBuffers_[nextBuffer_];
      int bytes = file_.read((uint8_t *)b.data(), PLAY_BLOCK * sizeof(int16_t));
      if (bytes <= 0) {
        fileDone_ = true;
        break;
      }
      M5.Speaker.playRaw(b.data(), bytes / sizeof(int16_t), playRate_, false, 1, 0, false);
      played_ += bytes / sizeof(int16_t);
      nextBuffer_ = (nextBuffer_ + 1) % 3;
    }
    if (fileDone_ && M5.Speaker.isPlaying(0) == 0) stopPlaying(true);
  }

  void stopPlaying(bool restartMic) {
    M5.Speaker.stop();
    M5.Speaker.end();
    es8311::speakerOff();                              // otherwise the idle amplifier hums
    file_.close();
    mode_ = Mode::Idle;
    for (auto &b : playBuffers_) std::vector<int16_t>().swap(b);
    if (restartMic) audio_in::start(RATE, micGain());
  }

  bool haveCard_ = false;
  std::vector<storage::Recording> list_;
  int selected_ = -1;
  uint32_t freeMb_ = 0;
  Mode mode_ = Mode::Idle;
  const char *message_ = "";
  uint32_t deleteAskedMs_ = 0, clipMs_ = 0;
  float level_ = 0;

  File file_;
  String path_;
  std::vector<int16_t> block_;
  uint32_t recorded_ = 0, droppedAtStart_ = 0;         // recorded_: samples in the file
  std::unique_ptr<dsp::Decimator> decimator_;          // StickS3: 16 -> 8 kHz
  bool full_ = false;                                  // a write failed: no space left
  uint64_t freeBytes_ = 0;
  int recPeak_ = 0;
  float normGain_ = 1;
  uint32_t normPos_ = 0, normEnd_ = 0;
  std::vector<int16_t> normBuffer_;

  std::vector<int16_t> playBuffers_[3];
  int nextBuffer_ = 0;
  uint32_t played_ = 0, playRate_ = RATE;
  bool fileDone_ = false;
  int volume_ = 7;
  bool naming_ = false;
  String name_;
};

RecorderApp instance;

}  // namespace

App *recorderApp() { return &instance; }
