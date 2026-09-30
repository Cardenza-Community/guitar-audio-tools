// Decibel meter: sound level in dB SPL, A or Z weighted, Fast (1/8 s),
// with Leq (average energy), Max, Min and a rating of the noise.
//
// Keys: a  A/Z weighting     Enter/r  reset Leq/Max/Min
// (No Slow mode any more: with the number held for 0.5 s it was not needed.)
//       c  calibration: ; . +-0.5 dB, , / +-5 dB, Enter saves, c cancels
//
// Range: normally the codec gain is 18 dB (quiet rooms up to about 100 dB).
// When the signal gets close to clipping it switches to 0 dB (up to about
// 120 dB) and back when it is quiet again. The meter gets the samples scaled
// back to the normal range, so Leq/Max/Min continue across a switch.
//
// Like real sound level meters, the big number is rewritten only twice per
// second (at 30 frames per second its tenths changed too fast to read); the
// bar moves smoothly.
#include <memory>
#include "apps.h"
#include "sound_level.h"
#include "../hw/es8311.h"
#include "../services/audio_in.h"
#include "../services/settings.h"
#include "../services/ui.h"

namespace {

const int RATE = 32000;                 // up to 16 kHz, enough for A-weighting
const int NORMAL_GAIN = 18;             // dB
const int LOUD_GAIN = 0;                // dB
const float LOUD_SCALE = 7.943f;        // 10^((18 - 0) / 20): back to normal-range units
const int CLIP_PEAK = 29000;            // switch to the loud range above this sample value
const float BACK_TO_NORMAL_DBFS = -24;  // switch back when Slow stays below this for 2 s

// dB SPL = dBFS (normal range) + calibration. Default: a first estimate from
// the microphone noise floor; press c to calibrate against a phone app.
const float DEFAULT_CALIBRATION = 104.0f;

struct Rating {
  float below;          // this rating applies up to this level (dBA)
  const char *text;
  uint16_t color;
};

const Rating RATINGS[] = {
    {30, "Very quiet", GREEN},        {40, "Quiet room", GREEN},
    {50, "Library, office", GREEN},   {60, "Conversation", GREENYELLOW},
    {70, "Busy office, TV", GREENYELLOW}, {80, "Busy street", YELLOW},
    {85, "Loud", YELLOW},             {95, "Harmful for hours", ORANGE},
    {105, "Very loud!", RED},         {999, "Dangerous!", RED},
};

class DecibelMeterApp : public App {
 public:
  const char *name() const override { return "Decibel meter"; }
  uint32_t sampleRate() const override { return RATE; }
  int micGain() const override { return NORMAL_GAIN; }

  void enter() override {
    meter_.reset(new dsp::SoundLevelMeter(RATE, dsp::Weighting::A));
    calibration_ = settings::getFloat("db_cal", DEFAULT_CALIBRATION);
    calibrated_ = settings::getInt("db_calok", 0);
    loudRange_ = false;
    skipSamples_ = 0;
    calibrating_ = false;
    quietSinceMs_ = 0;
    overload_ = false;
    barPeak_ = 0;
  }

  void exit() override { meter_.reset(); }

  void process(const int16_t *samples, size_t count) override {
    int peak = 0;
    for (size_t i = 0; i < count; i++) peak = max(peak, abs((int)samples[i]));
    overload_ = loudRange_ && peak > 32000;

    // after a range switch, samples recorded with the old gain are still on
    // their way (microphone DMA, stream buffer): they are not measured
    if (skipSamples_ > 0) {
      skipSamples_ -= std::min(skipSamples_, count);
      return;
    }
    meter_->process(samples, count, loudRange_ ? LOUD_SCALE : 1.0f);
    updateRange(peak);
    logToSerial(peak);
  }

  // the M5StickS3 action menu (double click on A)
  int actions(const Action *&items) const override {
    static const Action ACTIONS[] = {
        {"A / Z weighting", {'a'}},
        {"Calibrate / cancel", {'c'}},
        {"Calibration +0.5 dB", {';'}},
        {"Calibration -0.5 dB", {'.'}},
    };
    items = ACTIONS;
    return sizeof(ACTIONS) / sizeof(ACTIONS[0]);
  }

  int help(const ui::HelpItem *&items) const override {
    static const ui::HelpItem HELP[] = {
        {"a", "A: like the ear, Z: flat"},
        {"Enter", "reset Leq, Max, Min"},
        {"c", "calibrate to a phone app"},
        {nullptr, "Leq: average since reset"},
        {nullptr, "Max / Min: loudest / quietest"},
        {nullptr, "LOUD: range for very loud"},
    };
    items = HELP;
    return sizeof(HELP) / sizeof(HELP[0]);
  }

  void onKey(const Key &key) override {
    if (calibrating_) {
      if (key.ch == ';') calEdit_ += 0.5f;
      if (key.ch == '.') calEdit_ -= 0.5f;
      if (key.ch == '/') calEdit_ += 5;
      if (key.ch == ',') calEdit_ -= 5;
      if (key.ch == 'c') calibrating_ = false;
      if (key.enter) {
        calibration_ = calEdit_;
        calibrated_ = true;
        settings::putFloat("db_cal", calibration_);
        settings::putInt("db_calok", 1);
        calibrating_ = false;
        Serial.printf("calibration saved: %.1f\n", calibration_);
      }
      return;
    }
    if (key.ch == 'a')
      meter_->setWeighting(meter_->weighting() == dsp::Weighting::A ? dsp::Weighting::Z
                                                                    : dsp::Weighting::A);
    if (key.ch == 'r' || key.enter) meter_->reset();
    if (key.ch == 'c') {
      calibrating_ = true;
      calEdit_ = calibration_;
    }
  }

  void draw(M5Canvas &c) override {
    float cal = calibrating_ ? calEdit_ : calibration_;
    float level = meter_->fastDb() + cal;
    // the number: held for NUMBER_HOLD_MS (the calibration offset is added
    // afterwards, so changing it shows at once)
    if (millis() - heldMs_ >= NUMBER_HOLD_MS) {
      heldDb_ = level - cal;
      heldMs_ = millis();
    }
    float shown = heldDb_ + cal;
    bool aWeighted = meter_->weighting() == dsp::Weighting::A;

    char right[24];
    snprintf(right, sizeof(right), "%s FAST%s", aWeighted ? "A" : "Z",
             loudRange_ ? " LOUD" : "");
    ui::header(calibrating_ ? "CALIBRATION" : "Decibel meter", right);

    // big number and unit
    c.setTextColor(overload_ ? RED : WHITE);
    c.setTextSize(5);
    c.setCursor(2, 18);
    c.printf("%5.1f", max(shown, 0.0f));
    c.setTextSize(2);
    c.setCursor(152, 18);
    c.print(aWeighted ? "dBA" : "dBZ");
    if (!calibrated_ && !calibrating_) {
      c.setTextSize(1);
      c.setTextColor(ORANGE);
      c.setCursor(152, 38);
      c.print("uncalib.");
    }

    drawBar(c, level);

    if (calibrating_) {
      drawCalibrationHelp(c);
      return;
    }

    // measuring time (top right) and statistics row
    c.setTextSize(1);
    c.setTextColor(WHITE);
    int seconds = (int)meter_->seconds();
    c.setCursor(196, 18);
    c.printf("%02d:%02d", seconds / 60 % 100, seconds % 60);
    const char *labels[] = {"Leq", "Max", "Min"};
    float values[] = {meter_->leqDb() + cal, meter_->maxDb() + cal, meter_->minDb() + cal};
    for (int i = 0; i < 3; i++) {
      c.setCursor(i * 80 + 2, 92);
      c.setTextColor(YELLOW);
      c.print(labels[i]);
      c.setTextColor(WHITE);
      c.printf(" %5.1f", max(values[i], 0.0f));
    }

    // rating of the noise (based on the A-weighted level)
    const Rating *r = &RATINGS[0];
    while (shown >= r->below) r++;
    c.setTextSize(2);
    c.setTextColor(r->color);
    c.setCursor(2, 105);
    c.print(overload_ ? "OVERLOAD" : r->text);

    ui::footerHelp();
  }

 private:
  // Level bar from 30 to 110 dB, coloured by zones, with a 2 s peak marker.
  void drawBar(M5Canvas &c, float level) {
    const int Y = 64, H = 12, X0 = 2, W = 236;
    auto xOf = [&](float db) { return X0 + (int)constrain((db - 30) * W / 80, 0.0f, (float)W); };
    int end = xOf(level);
    for (int x = X0; x < end; x += 3) {
      float db = 30 + (x - X0) * 80.0f / W;
      uint16_t color = db < 55 ? GREEN : db < 70 ? GREENYELLOW : db < 85 ? YELLOW : RED;
      c.fillRect(x, Y, 2, H, color);
    }
    // peak hold
    if (level > barPeak_ || millis() - barPeakMs_ > 2000) {
      barPeak_ = level;
      barPeakMs_ = millis();
    }
    c.drawFastVLine(xOf(barPeak_), Y - 2, H + 4, WHITE);
    // scale
    c.setTextSize(1);
    c.setTextColor(WHITE);
    for (int db = 40; db <= 100; db += 20) {
      c.drawFastVLine(xOf(db), Y + H + 1, 3, DARKGREY);
      c.setCursor(xOf(db) - 5, Y + H + 5);
      c.print(db);
    }
  }

  void drawCalibrationHelp(M5Canvas &c) {
    c.setTextSize(1);
    c.setTextColor(WHITE);
    c.setCursor(2, 92);
    c.print("Play steady noise, measure it with a");
    c.setCursor(2, 102);
    c.print("phone app next to the Cardputer and");
    c.setCursor(2, 112);
    c.printf("set the same value.   (offset %.1f)", calEdit_);
    ui::footer(";. 0.5dB  ,/ 5dB  Enter save  c cancel");
  }

  void updateRange(int peak) {
    if (!loudRange_ && peak > CLIP_PEAK) {
      loudRange_ = true;
      es8311::setPgaGain(LOUD_GAIN);
      skipSamples_ = SKIP_AFTER_SWITCH;
      quietSinceMs_ = 0;
      Serial.println("range: LOUD");
    } else if (loudRange_) {
      if (meter_->slowDb() < BACK_TO_NORMAL_DBFS) {
        if (quietSinceMs_ == 0) quietSinceMs_ = millis();
        if (millis() - quietSinceMs_ > 2000) {
          loudRange_ = false;
          es8311::setPgaGain(NORMAL_GAIN);
          skipSamples_ = SKIP_AFTER_SWITCH;
          Serial.println("range: NORMAL");
        }
      } else {
        quietSinceMs_ = 0;
      }
    }
  }

  void logToSerial(int peak) {
    if (millis() - lastLogMs_ < 250) return;
    lastLogMs_ = millis();
    Serial.printf("fast=%.1f slow=%.1f leq=%.1f max=%.1f dBFS  pga=%d peak=%d  rate=%.2f dropped=%u\n",
                  meter_->fastDb(), meter_->slowDb(), meter_->leqDb(), meter_->maxDb(),
                  es8311::pgaGain(), peak, audio_in::measuredRate(),
                  (unsigned)audio_in::droppedSamples());
  }

  std::unique_ptr<dsp::SoundLevelMeter> meter_;
  float calibration_ = DEFAULT_CALIBRATION, calEdit_ = 0;
  bool calibrated_ = false, calibrating_ = false;
  bool loudRange_ = false, overload_ = false;
  static const size_t SKIP_AFTER_SWITCH = RATE / 10;   // 100 ms
  size_t skipSamples_ = 0;
  uint32_t quietSinceMs_ = 0, lastLogMs_ = 0, barPeakMs_ = 0;
  static const uint32_t NUMBER_HOLD_MS = 500;
  float heldDb_ = 0;
  uint32_t heldMs_ = 0;
  float barPeak_ = 0;
};

DecibelMeterApp instance;

}  // namespace

App *decibelMeterApp() { return &instance; }
