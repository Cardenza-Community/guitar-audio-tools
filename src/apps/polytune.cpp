// PolyTune: strum all six open strings, see which ones are out of tune.
//
// Flow: wait for a strum (the level jumps by 10 dB or more), skip the first
// 150 ms (pluck noise), collect 1.024 s of sound, analyse all strings at once
// (lib/dsp/polytune) and show the result until the next strum.
// The sound is reduced from 16 kHz to 4 kHz first: strings and their 2nd
// harmonics are below 700 Hz, and 1 s at 4 kHz fits into 16 KB.
//
// The last second before the strum is kept as "background": steady tones in
// the room that were already there are not taken for strings.
//
// Display: one column per string (E A D G B E). The marker shows the deviation:
// up = sharp (too high), down = flat, green centre = within 3 cents. More than
// An arrow above the column says which way to tune (up = tighten, down =
// loosen): yellow for 10...50 cents, red for more than 50 cents (then the
// marker is off the scale). "?" = the string was not heard clearly.
#include <memory>
#include <vector>
#include "apps.h"
#include "decimator.h"
#include "level.h"
#include "notes.h"
#include "polytune.h"
#include "../hw/es8311.h"
#include "../services/audio_in.h"
#include "../services/settings.h"
#include "../services/ui.h"

namespace {

const int RATE = 16000;
const int FACTOR = 4;                       // 16 kHz -> 4 kHz
const float LOW_RATE = RATE / FACTOR;
const size_t LENGTH = 4096;                 // analysed block: 1.024 s at 4 kHz
const size_t SKIP = 600;                    // 150 ms of pluck noise at 4 kHz
const size_t LEVEL_BLOCK = 512;             // level measured every 32 ms (16 kHz samples)
const float ONSET_JUMP_DB = 10;
const float ONSET_MIN_DBFS = -58;
const float IN_TUNE_CENTS = 3;
const float ARROW_CENTS = 10;               // from here an arrow says which way to tune
const bool DUMP_BLOCKS = false;             // development: print analysed blocks

// column geometry
const int COLUMN_WIDTH = 40, MID_Y = 60;
const float SCALE = 0.72f;                  // pixels per cent: +-50 cents = +-36 px

enum class State { Waiting, Collecting };

class PolyTuneApp : public App {
 public:
  const char *name() const override { return "PolyTune"; }
  uint32_t sampleRate() const override { return RATE; }
  int micGain() const override { return 24; }

  void enter() override {
    decimator_.reset(new dsp::Decimator(FACTOR));
    tuner_.reset(new dsp::PolyTuner(LOW_RATE, LENGTH));
    buffer_.assign(SKIP + LENGTH, 0);
    ring_.assign(LENGTH, 0);
    ringPos_ = 0;
    ringFull_ = false;
    decimated_.assign(LEVEL_BLOCK / FACTOR + 1, 0);
    a4_ = settings::getFloat("a4", 440.0f);
    state_ = State::Waiting;
    haveResult_ = false;
    weak_ = false;
    levels_.clear();
    levelPower_ = 0;
    levelCount_ = 0;
  }

  void exit() override {
    decimator_.reset();
    tuner_.reset();
    std::vector<float>().swap(buffer_);
    std::vector<float>().swap(ring_);
    std::vector<float>().swap(decimated_);
  }

  void process(const int16_t *samples, size_t count) override {
    // work in blocks of LEVEL_BLOCK samples: level check, then decimation
    while (count > 0) {
      size_t n = std::min(count, LEVEL_BLOCK - levelCount_);
      for (size_t i = 0; i < n; i++) levelPower_ += (double)samples[i] * samples[i];
      levelCount_ += n;
      size_t low = decimator_->process(samples, n, decimated_.data());
      remember(decimated_.data(), low);
      if (state_ == State::Collecting) collect(decimated_.data(), low);
      samples += n;
      count -= n;
      if (levelCount_ == LEVEL_BLOCK) {
        checkOnset(10 * log10f((float)(levelPower_ / LEVEL_BLOCK) / (32768.0f * 32768.0f) + 1e-12f));
        levelPower_ = 0;
        levelCount_ = 0;
      }
    }
  }

  void onKey(const Key &key) override {
    if (key.ch == ';') es8311::setPgaGain(es8311::pgaGain() + 3);
    if (key.ch == '.') es8311::setPgaGain(es8311::pgaGain() - 3);
  }

  void draw(M5Canvas &c) override {
    char right[24];
    snprintf(right, sizeof(right), "%s  A4=%.0f", state_ == State::Collecting ? "listening" : "", a4_);
    ui::header("PolyTune", right);

    static const char *NAMES[] = {"E", "A", "D", "G", "B", "e"};
    for (int s = 0; s < dsp::GUITAR_STRINGS; s++) {
      int x = s * COLUMN_WIDTH + COLUMN_WIDTH / 2;
      // scale: green centre zone, ticks every 10 cents
      c.fillRect(x - 14, MID_Y - (int)(IN_TUNE_CENTS * SCALE), 29, (int)(2 * IN_TUNE_CENTS * SCALE) + 1,
                 0x0200);   // very dark green
      for (int cents = -50; cents <= 50; cents += 10) {
        int y = MID_Y - (int)(cents * SCALE);
        int w = cents == 0 ? 14 : 6;
        c.drawFastHLine(x - w, y, 2 * w + 1, cents == 0 ? GREEN : DARKGREY);
      }

      const dsp::StringReading &r = result_[s];
      bool show = haveResult_ && r.found;
      if (show) {
        float cents = r.cents;
        uint16_t color = fabsf(cents) <= IN_TUNE_CENTS ? GREEN : fabsf(cents) <= 15 ? YELLOW : ORANGE;
        if (fabsf(cents) > ARROW_CENTS) {
          // an arrow above the column, pointing the way to tune: up = tighten
          // (the string is flat), down = loosen (sharp); red and bigger when
          // more than 50 cents off
          bool far = fabsf(cents) > 50;
          int half = far ? 7 : 5, top = ui::HEADER_HEIGHT + 2, bottom = top + (far ? 9 : 7);
          uint16_t arrow = far ? RED : YELLOW;
          if (cents < 0)
            c.fillTriangle(x - half, bottom, x + half, bottom, x, top, arrow);
          else
            c.fillTriangle(x - half, top, x + half, top, x, bottom, arrow);
        }
        if (fabsf(cents) <= 50) {
          int y = MID_Y - (int)(cents * SCALE);
          c.fillRect(x - 12, y - 2, 25, 5, color);
        }
      }

      // string name and deviation
      c.setTextSize(2);
      c.setTextColor(!haveResult_ ? DARKGREY : show ? WHITE : DARKGREY);
      c.setCursor(x - 5, 102);
      c.print(show || !haveResult_ ? NAMES[s] : "?");
      if (show) {
        char text[8];
        snprintf(text, sizeof(text), "%+d", (int)lroundf(r.cents));
        c.setTextSize(1);
        c.setTextColor(LIGHTGREY);
        c.setCursor(x - c.textWidth(text) / 2, 118);
        c.print(text);
      }
    }
    if (!haveResult_) {
      c.setTextSize(1);
      c.setTextColor(LIGHTGREY);
      const char *hint = state_ == State::Collecting ? "listening..." : "strum all 6 open strings";
      c.setCursor((ui::WIDTH - c.textWidth(hint)) / 2, 118);
      c.print(hint);
    }
    if (weak_) {                    // instead of the key help
      c.setTextSize(1);
      c.setTextColor(ORANGE);
      c.setCursor(3, ui::FOOTER_Y);
      c.print("too quiet: strum louder / closer");
      return;
    }
    ui::footer(";. gain   Esc back");
  }

 private:
  // A strum: the level jumps by ONSET_JUMP_DB above the quietest of the last
  // four blocks and is loud enough. It (re)starts the collection.
  void checkOnset(float db) {
    float quietest = db;
    for (float l : levels_) quietest = std::min(quietest, l);
    bool onset = levels_.size() == 4 && db > ONSET_MIN_DBFS && db - quietest >= ONSET_JUMP_DB &&
                 millis() - lastOnsetMs_ > 300;
    levels_.push_back(db);
    if (levels_.size() > 4) levels_.erase(levels_.begin());
    if (!onset) return;
    lastOnsetMs_ = millis();
    // the sound before this strum is the background (not when a second strum
    // interrupts a collection: then the ring holds the first strum)
    if (state_ == State::Waiting) {
      if (ringFull_) tuner_->setBackground(ring_.data(), ringPos_, a4_);
      else tuner_->clearBackground();
    }
    state_ = State::Collecting;
    collected_ = 0;
    Serial.printf("strum at %.1f dBFS\n", db);
  }

  // keeps the last LENGTH samples (4 kHz) all the time
  void remember(const float *low, size_t n) {
    for (size_t i = 0; i < n; i++) {
      ring_[ringPos_] = low[i];
      if (++ringPos_ == ring_.size()) {
        ringPos_ = 0;
        ringFull_ = true;
      }
    }
  }

  void collect(const float *low, size_t n) {
    for (size_t i = 0; i < n && collected_ < buffer_.size(); i++) buffer_[collected_++] = low[i];
    if (collected_ < buffer_.size()) return;
    state_ = State::Waiting;
    uint32_t start = millis();
    dsp::StringReading r[dsp::GUITAR_STRINGS];
    tuner_->analyse(buffer_.data() + SKIP, a4_, r);
    float strongest = 0;
    for (auto &x : r) strongest = std::max(strongest, x.amplitude);
    // a weak strum (or a handling noise) keeps the previous result on screen
    weak_ = strongest < dsp::MIN_STRUM_AMPLITUDE;
    if (!weak_) {
      std::copy(r, r + dsp::GUITAR_STRINGS, result_);
      haveResult_ = true;
    }
    Serial.printf("analysis %lu ms:", (unsigned long)(millis() - start));
    for (int s = 0; s < dsp::GUITAR_STRINGS; s++)
      Serial.printf("  %d:%s%+.1fc a=%.0f", 6 - s, r[s].found ? "" : "?", r[s].cents, r[s].amplitude);
    if (weak_) Serial.print("  (too weak)");
    Serial.println();
    if (DUMP_BLOCKS) dumpBlock();
  }

  // Development aid: prints the analysed block (4 kHz) so it can be replayed
  // on the PC, in the same format as the Mic test dump.
  void dumpBlock() {
    Serial.printf("DUMP BEGIN rate=%d n=%u\n", (int)LOW_RATE, (unsigned)buffer_.size());
    for (size_t i = 0; i < buffer_.size(); i += 32) {
      for (size_t k = i; k < i + 32 && k < buffer_.size(); k++)
        Serial.printf(k == i ? "%d" : ",%d", (int)lroundf(buffer_[k]));
      Serial.println();
    }
    Serial.println("DUMP END");
  }

  std::unique_ptr<dsp::Decimator> decimator_;
  std::unique_ptr<dsp::PolyTuner> tuner_;
  std::vector<float> buffer_;       // SKIP + LENGTH samples at 4 kHz after a strum
  std::vector<float> decimated_;
  std::vector<float> ring_;         // the last second, for the background
  size_t ringPos_ = 0;
  bool ringFull_ = false;
  size_t collected_ = 0;
  State state_ = State::Waiting;
  dsp::StringReading result_[dsp::GUITAR_STRINGS];
  bool haveResult_ = false;
  bool weak_ = false;                 // the last strum was too quiet
  float a4_ = 440;

  std::vector<float> levels_;
  double levelPower_ = 0;
  size_t levelCount_ = 0;
  uint32_t lastOnsetMs_ = 0;
};

PolyTuneApp instance;

}  // namespace

App *polyTuneApp() { return &instance; }
