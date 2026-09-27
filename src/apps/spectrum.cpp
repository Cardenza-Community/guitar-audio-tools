// Spectrum analyser: bars made of green, yellow and red blocks, like the
// display of a hi-fi system. Two modes (key m, remembered):
//  - 16 bars from 60 Hz to 16 kHz (FFT of 2048 samples),
//  - 26 ISO third-octave bands 50 Hz ... 16 kHz, like a professional analyser,
//    for finding feedback or room resonances (FFT of 4096 samples: the low
//    bands are only 11-15 Hz wide; the bass reacts a little slower).
//
//  - a new spectrum every 32 ms (32 kHz)
//  - bars jump up at once and fall slowly; a peak block stays on top for a
//    moment and then falls too
//  - automatic sensitivity (lib/dsp/auto_range): the top of the scale jumps
//    to the loudest band and comes down slowly, so quiet music from a phone
//    moves the bars as well as a loud stereo
// Keys: Enter (or p) peaks on/off, , / (or r) range of the bars 20 / 30 / 40 dB
// (remembered), m 16 bars / third octaves.
#include <algorithm>
#include <memory>
#include <vector>
#include "apps.h"
#include "level.h"
#include "spectrum.h"
#include "auto_range.h"
#include "../services/settings.h"
#include "../services/ui.h"

namespace {

const int RATE = 32000;
const size_t HOP = 1024;                 // a new spectrum every 32 ms
const int MAX_BANDS = dsp::THIRD_OCTAVE_BANDS;

// the two modes
struct Mode {
  const char *name;         // in the header
  size_t fftSize;
  int bands;
  float minHz, maxHz;
  int barWidth, barStep, left;          // pixels
  const char *const *labels;            // one per band (nullptr = none); no table: every 3rd band's centre
};
// below 60 Hz a phone speaker plays almost nothing; the 16 bars are better
// spent on the audible range
const char *const LABELS_THIRD[26] = {"50", nullptr, nullptr, "100", nullptr, nullptr, "200", nullptr,
                                      nullptr, "400", nullptr, nullptr, nullptr, "1k", nullptr, nullptr,
                                      "2k", nullptr, nullptr, "4k", nullptr, nullptr, "8k", nullptr,
                                      nullptr, "16k"};
const Mode MODES[] = {
    {"16 bars", 2048, 16, 60, 16000, 13, 15, 1, nullptr},                    // 16 x 15 = 240 px
    {"1/3 oct", 4096, dsp::THIRD_OCTAVE_BANDS, dsp::THIRD_OCTAVE_MIN_HZ,
     dsp::THIRD_OCTAVE_MAX_HZ, 7, 9, 3, LABELS_THIRD},                          // 26 x 9 = 234 px
};

// display
const int SEGMENTS = 16, SEGMENT_H = 5, SEGMENT_GAP = 1;
const int BOTTOM_Y = 110;                // lowest block ends here
const float RANGES_DB[] = {20, 30, 40};  // height of a whole bar (key r)
// the automatic sensitivity never goes below this, so silence stays low
const float MIN_TOP_DB = -50;
const float BAR_FALL_DB_S = 30;
const float PEAK_HOLD_MS = 800, PEAK_FALL_DB_S = 20;

class SpectrumApp : public App {
 public:
  const char *name() const override { return "Spectrum"; }
  uint32_t sampleRate() const override { return RATE; }
  int micGain() const override { return 18; }

  void enter() override {
    mode_ = constrain(settings::getInt("spec_mode", 0), 0, 1);
    setUpMode();
    rangeIndex_ = constrain(settings::getInt("spec_range", 1), 0, 2);
    range_.reset(new dsp::AutoRange(RANGES_DB[rangeIndex_], MIN_TOP_DB));
    lastMs_ = millis();
  }

  void exit() override {
    bands_.reset();
    range_.reset();
    std::vector<int16_t>().swap(history_);
    std::vector<int16_t>().swap(ordered_);
  }

  void process(const int16_t *samples, size_t count) override {
    size_t size = history_.size();
    for (size_t i = 0; i < count; i++) {
      history_[writePos_] = samples[i];
      writePos_ = (writePos_ + 1) % size;
      if (++newSamples_ >= HOP) {
        newSamples_ = 0;
        analyse();
      }
    }
  }

  int help(const ui::HelpItem *&items) const override {
    static const ui::HelpItem HELP[] = {
        {"Enter", "peaks on / off"},
        {", /", "bar range 20/30/40 dB"},
        {"", "(smaller = livelier bars)"},
        {"m", "16 bars / 1/3 octaves (26)"},
        {nullptr, "bars: 50-60 Hz (left) - 16 kHz"},
        {nullptr, "the sensitivity is automatic"},
    };
    items = HELP;
    return sizeof(HELP) / sizeof(HELP[0]);
  }

  void onKey(const Key &key) override {
    if (key.ch == 'p' || key.enter) showPeaks_ = !showPeaks_;
    int step = key.ch == '/' || key.ch == 'r' ? 1 : key.ch == ',' ? 2 : 0;   // 2 = one back
    if (step) {
      rangeIndex_ = (rangeIndex_ + step) % 3;
      range_->setRange(RANGES_DB[rangeIndex_]);
      settings::putInt("spec_range", rangeIndex_);
    }
    if (key.ch == 'm') {
      mode_ = 1 - mode_;
      settings::putInt("spec_mode", mode_);
      setUpMode();
    }
  }

  void draw(M5Canvas &c) override {
    char right[24];
    const Mode &m = MODES[mode_];
    snprintf(right, sizeof(right), "%s  %.0f dB%s", m.name, RANGES_DB[rangeIndex_], showPeaks_ ? "  peaks" : "");
    ui::header("Spectrum", right);
    for (int b = 0; b < m.bands; b++) {
      int x = m.left + b * m.barStep;
      int lit = segmentsFor(bar_[b]);
      for (int s = 0; s < SEGMENTS; s++) {
        int y = BOTTOM_Y - (s + 1) * (SEGMENT_H + SEGMENT_GAP) + SEGMENT_GAP;
        c.fillRect(x, y, m.barWidth, SEGMENT_H, s < lit ? colorOf(s) : 0x2104);   // unlit: dark grey
      }
      if (showPeaks_) {
        int p = segmentsFor(peak_[b]);
        if (p > 0) {
          int y = BOTTOM_Y - p * (SEGMENT_H + SEGMENT_GAP) + SEGMENT_GAP;
          c.fillRect(x, y, m.barWidth, SEGMENT_H, WHITE);
        }
      }
    }
    // frequency labels under some bars
    c.setTextSize(1);
    c.setTextColor(WHITE);
    for (int b = 0; b < m.bands; b++) {
      char text[8];
      if (m.labels) {
        if (!m.labels[b]) continue;
        snprintf(text, sizeof(text), "%s", m.labels[b]);
      } else {
        if (b % 3) continue;
        float hz = bands_->centreHz(b);
        if (hz < 1000) snprintf(text, sizeof(text), "%.0f", hz);
        else if (hz < 10000) snprintf(text, sizeof(text), "%.1fk", hz / 1000);
        else snprintf(text, sizeof(text), "%.0fk", hz / 1000);
      }
      int cx = m.left + b * m.barStep + m.barWidth / 2;
      c.setCursor(constrain(cx - c.textWidth(text) / 2, 0, ui::WIDTH - c.textWidth(text)), BOTTOM_Y + 3);
      c.print(text);
    }
    ui::footerHelp();
  }

 private:
  // (re)builds the analyser for the current mode
  void setUpMode() {
    const Mode &m = MODES[mode_];
    bands_.reset();                               // free the old one first (memory)
    bands_.reset(new dsp::SpectrumBands(RATE, m.fftSize, m.bands, m.minHz, m.maxHz));
    history_.assign(m.fftSize, 0);
    ordered_.assign(m.fftSize, 0);
    writePos_ = newSamples_ = 0;
    for (int b = 0; b < MAX_BANDS; b++) {
      bar_[b] = peak_[b] = dsp::SILENCE_DB;
      peakMs_[b] = 0;
    }
    Serial.printf("spectrum: %s, free heap %u\n", m.name, (unsigned)ESP.getFreeHeap());
  }

  static uint16_t colorOf(int segment) {
    return segment >= 13 ? RED : segment >= 10 ? YELLOW : GREEN;
  }

  int segmentsFor(float db) const {
    float fraction = (db - range_->bottom()) / range_->range();
    return constrain((int)(fraction * SEGMENTS + 0.5f), 0, SEGMENTS);
  }

  void analyse() {
    size_t size = history_.size();
    int bandCount = MODES[mode_].bands;
    for (size_t i = 0; i < size; i++) ordered_[i] = history_[(writePos_ + i) % size];
    float levels[MAX_BANDS];
    bands_->analyse(ordered_.data(), levels);

    uint32_t now = millis();
    float dt = (now - lastMs_) / 1000.0f;
    lastMs_ = now;

    range_->update(*std::max_element(levels, levels + bandCount), dt);

    // hi-fi ballistics: up at once, down slowly; peaks hold, then fall
    for (int b = 0; b < bandCount; b++) {
      bar_[b] = std::max(levels[b], bar_[b] - BAR_FALL_DB_S * dt);
      if (bar_[b] >= peak_[b]) {
        peak_[b] = bar_[b];
        peakMs_[b] = now;
      } else if (now - peakMs_[b] > PEAK_HOLD_MS) {
        peak_[b] = std::max(bar_[b], peak_[b] - PEAK_FALL_DB_S * dt);
      }
    }
  }

  std::unique_ptr<dsp::SpectrumBands> bands_;
  std::vector<int16_t> history_, ordered_;
  size_t writePos_ = 0, newSamples_ = 0;
  float bar_[MAX_BANDS], peak_[MAX_BANDS];
  uint32_t peakMs_[MAX_BANDS];
  int mode_ = 0;
  std::unique_ptr<dsp::AutoRange> range_;
  int rangeIndex_ = 1;
  uint32_t lastMs_ = 0;
  bool showPeaks_ = true;
};

SpectrumApp instance;

}  // namespace

App *spectrumApp() { return &instance; }
