// Spectrum analyser: 16 bars from 60 Hz to 16 kHz made of green, yellow and
// red blocks, like the display of a hi-fi system.
//
//  - a new spectrum every 32 ms (FFT of 2048 samples at 32 kHz, half overlap)
//  - bars jump up at once and fall slowly; a peak block stays on top for a
//    moment and then falls too
//  - automatic sensitivity (lib/dsp/auto_range): the top of the scale jumps
//    to the loudest band and comes down slowly, so quiet music from a phone
//    moves the bars as well as a loud stereo
// Keys: p peaks on/off, r range of the bars 20 / 30 / 40 dB (remembered).
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
const size_t FFT_SIZE = 2048;
const size_t HOP = 1024;                 // a new spectrum every 32 ms
const int BANDS = 16;
// below 60 Hz a phone speaker plays almost nothing; the bars are better spent
// on the audible range
const float MIN_HZ = 60, MAX_HZ = 16000;

// display
const int SEGMENTS = 16, SEGMENT_H = 5, SEGMENT_GAP = 1;
const int BAR_W = 13, BAR_STEP = 15;     // 16 bars x 15 px = 240 px
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
    bands_.reset(new dsp::SpectrumBands(RATE, FFT_SIZE, BANDS, MIN_HZ, MAX_HZ));
    history_.assign(FFT_SIZE, 0);
    ordered_.assign(FFT_SIZE, 0);
    writePos_ = newSamples_ = 0;
    for (int b = 0; b < BANDS; b++) {
      bar_[b] = peak_[b] = dsp::SILENCE_DB;
      peakMs_[b] = 0;
    }
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
    for (size_t i = 0; i < count; i++) {
      history_[writePos_] = samples[i];
      writePos_ = (writePos_ + 1) % FFT_SIZE;
      if (++newSamples_ >= HOP) {
        newSamples_ = 0;
        analyse();
      }
    }
  }

  void onKey(const Key &key) override {
    if (key.ch == 'p') showPeaks_ = !showPeaks_;
    if (key.ch == 'r') {
      rangeIndex_ = (rangeIndex_ + 1) % 3;
      range_->setRange(RANGES_DB[rangeIndex_]);
      settings::putInt("spec_range", rangeIndex_);
    }
  }

  void draw(M5Canvas &c) override {
    char right[24];
    snprintf(right, sizeof(right), "%.0f dB%s", RANGES_DB[rangeIndex_], showPeaks_ ? "  peaks" : "");
    ui::header("Spectrum", right);
    for (int b = 0; b < BANDS; b++) {
      int x = b * BAR_STEP + 1;
      int lit = segmentsFor(bar_[b]);
      for (int s = 0; s < SEGMENTS; s++) {
        int y = BOTTOM_Y - (s + 1) * (SEGMENT_H + SEGMENT_GAP) + SEGMENT_GAP;
        c.fillRect(x, y, BAR_W, SEGMENT_H, s < lit ? colorOf(s) : 0x2104);   // unlit: dark grey
      }
      if (showPeaks_) {
        int p = segmentsFor(peak_[b]);
        if (p > 0) {
          int y = BOTTOM_Y - p * (SEGMENT_H + SEGMENT_GAP) + SEGMENT_GAP;
          c.fillRect(x, y, BAR_W, SEGMENT_H, WHITE);
        }
      }
    }
    // frequency labels under every third bar
    c.setTextSize(1);
    c.setTextColor(DARKGREY);
    for (int b = 0; b < BANDS; b += 3) {
      char text[8];
      float hz = bands_->centreHz(b);
      if (hz < 1000) snprintf(text, sizeof(text), "%.0f", hz);
      else if (hz < 10000) snprintf(text, sizeof(text), "%.1fk", hz / 1000);
      else snprintf(text, sizeof(text), "%.0fk", hz / 1000);
      int cx = b * BAR_STEP + 1 + BAR_W / 2;
      c.setCursor(constrain(cx - c.textWidth(text) / 2, 0, ui::WIDTH - c.textWidth(text)), BOTTOM_Y + 3);
      c.print(text);
    }
    ui::footer("p peaks   r range   Esc back");
  }

 private:
  static uint16_t colorOf(int segment) {
    return segment >= 13 ? RED : segment >= 10 ? YELLOW : GREEN;
  }

  int segmentsFor(float db) const {
    float fraction = (db - range_->bottom()) / range_->range();
    return constrain((int)(fraction * SEGMENTS + 0.5f), 0, SEGMENTS);
  }

  void analyse() {
    for (size_t i = 0; i < FFT_SIZE; i++) ordered_[i] = history_[(writePos_ + i) % FFT_SIZE];
    float levels[BANDS];
    bands_->analyse(ordered_.data(), levels);

    uint32_t now = millis();
    float dt = (now - lastMs_) / 1000.0f;
    lastMs_ = now;

    range_->update(*std::max_element(levels, levels + BANDS), dt);

    // hi-fi ballistics: up at once, down slowly; peaks hold, then fall
    for (int b = 0; b < BANDS; b++) {
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
  float bar_[BANDS], peak_[BANDS];
  uint32_t peakMs_[BANDS];
  std::unique_ptr<dsp::AutoRange> range_;
  int rangeIndex_ = 1;
  uint32_t lastMs_ = 0;
  bool showPeaks_ = true;
};

SpectrumApp instance;

}  // namespace

App *spectrumApp() { return &instance; }
