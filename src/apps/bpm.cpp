// BPM detector: the tempo of music, or tap tempo.
//
// AUTO: listens to the music (onset envelope -> tempo estimator, see
//       lib/dsp/onset and lib/dsp/tempo); the first reading after about 3 s,
//       then twice per second over a window growing up to 6 s. The shown
//       tempo keeps its octave (a reading of exactly half or double counts as
//       the same tempo). A new tempo is shown when it lasts 1.5 s (3 readings
//       in a row); 2/3 or 3/2 of the shown tempo - a typical misreading of
//       music with strong off-beats - has to last 3 s (6 readings).
// TAP:  pressing Enter or the space bar switches to tapping at once; the tapped
//       tempo is shown after 3 taps, exactly as tapped (no octave guessing).
//       3 s without a tap: back to listening, which then keeps the tapped
//       octave. (Claps need no tapping: AUTO hears them like drum hits.)
// A dot flashes on every beat; in AUTO it follows the music (every clear onset
// near a predicted beat pulls the beat clock a little towards it).
// Keys: Enter/space tap, , halve / / double the reading (for tempos read at
//       the other octave), r start again, d dump the last 8 s of per-band
//       onsets to the serial console (also on "d" sent over serial).
#include <algorithm>
#include <memory>
#include <vector>
#include "apps.h"
#include "onset.h"
#include "tempo.h"
#include "../services/settings.h"
#include "../services/ui.h"

namespace {

const int RATE = 16000;
const float ESTIMATE_EVERY_S = 0.5f;
// How clearly the rhythm repeats (0 ... 1), measured on real songs from a phone
// speaker: clear pop/rock 0.35 ... 0.65, a soft ballad or the fading end of a
// song 0.0 ... 0.26.
const float MIN_CONFIDENCE = 0.1f;       // below this a reading is ignored
const float SURE_CONFIDENCE = 0.3f;      // below this the tempo is shown grey, "uncertain"
const uint32_t FLASH_MS = 100;
const float SAME_TEMPO = 0.04f;          // readings within 4 % are the same tempo
const int CONFIRM_NEW = 3;               // readings in a row for a new tempo
const int CONFIRM_RELATED = 6;           // ... for 2/3 or 3/2 of the shown tempo
const size_t DUMP_FRAMES = 1000;         // 8 s of per-band onsets at 125 frames/s
const uint32_t TAP_TIMEOUT_MS = 3000;    // no tap for this long: back to listening

class BpmApp : public App {
 public:
  const char *name() const override { return "BPM"; }
  uint32_t sampleRate() const override { return RATE; }
  int micGain() const override { return 24; }

  void enter() override {
    onsets_.reset(new dsp::OnsetDetector(RATE));
    restart();                                    // builds the tempo estimator
  }

  void exit() override {
    // the Metronome offers the last tempo from here
    if (bpm_ > 0) settings::putInt("bpm_last", (int)lroundf(bpm_ * factor_));
    onsets_.reset();
    tempo_.reset();
    std::vector<uint16_t>().swap(dumpRing_);
    dumpPos_ = dumpCount_ = 0;                    // the ring is gone: nothing to dump
  }

  void process(const int16_t *samples, size_t count) override {
    while (Serial.available())
      if (Serial.read() == 'd') dump();
    size_t n = onsets_->process(samples, count);
    uint32_t now = millis();
    for (size_t i = 0; i < n; i++) {
      tempo_->push(onsets_->value(i));
      remember(onsets_->bandFlux(i));
      if (onsets_->isOnset(i) && !tap_) nudgeBeatClock(now);
    }
    if (tap_ && now - lastTapMs_ > TAP_TIMEOUT_MS) tap_ = false;   // back to listening
    if (!tap_ && tempo_->ready() && now - lastEstimateMs_ >= ESTIMATE_EVERY_S * 1000) {
      lastEstimateMs_ = now;
      dsp::TempoEstimator::Result r = tempo_->estimate();
      confidence_ = r.confidence;
      // slow average of the confidence: decides whether the tempo is shown as sure
      sureness_ += 0.3f * (r.confidence - sureness_);
      if (r.bpm > 0 && r.confidence >= MIN_CONFIDENCE) update(r.bpm, r.confidence);
    }
  }

  // the M5StickS3 action menu (double click on A)
  int actions(const Action *&items) const override {
    static const Action ACTIONS[] = {
        {"Listen again", {'r'}},
    };
    items = ACTIONS;
    return sizeof(ACTIONS) / sizeof(ACTIONS[0]);
  }

  int help(const ui::HelpItem *&items) const override {
    static const ui::HelpItem HELP[] = {
        {"Enter", "tap the beat (or space);"},
        {"", "3 s no tap: listen again"},
        {", /", "tempo /2  x2"},
        {"r", "start again"},
        {nullptr, "orange tempo: rhythm unclear"},
        {nullptr, "the dot flashes on the beat"},
        {nullptr, "claps are heard like drums"},
    };
    items = HELP;
    return sizeof(HELP) / sizeof(HELP[0]);
  }

  void onKey(const Key &key) override {
    if (key.ch == 'r') restart();
    if (key.ch == 'd') dump();
    if (key.ch == ',') factor_ = std::max(0.25f, factor_ / 2);
    if (key.ch == '/') factor_ = std::min(4.0f, factor_ * 2);
    if (key.ch == ' ' || key.enter) {
      uint32_t now = millis();
      if (!tap_) {                             // start a new series of taps
        tap_ = true;
        taps_.reset();
        factor_ = 1;                           // show exactly what is tapped
      }
      lastTapMs_ = now;
      tapAt(now);
    }
  }

  void draw(M5Canvas &c) override {
    ui::header("BPM", tap_ ? "TAP" : "AUTO");
    uint32_t now = millis();
    advanceBeatClock(now);

    // tempo: grey while the rhythm is not clear
    float shown = bpm_ * factor_;
    bool sure = tap_ || sureness_ >= SURE_CONFIDENCE;
    c.setTextSize(5);
    c.setTextColor(bpm_ == 0 ? WHITE : sure ? WHITE : ORANGE);
    c.setCursor(4, 24);
    if (bpm_ > 0) c.printf("%4.0f", shown);
    else c.print(" ---");
    if (bpm_ > 0 && !sure) {
      c.setTextSize(1);
      c.setTextColor(ORANGE);
      c.setCursor(130, 68);
      c.print("uncertain");
    }
    if (factor_ != 1) {
      c.setTextSize(1);
      c.setTextColor(ORANGE);
      c.setCursor(4, 68);
      c.printf("reading %s%g", factor_ > 1 ? "x" : "/", factor_ > 1 ? factor_ : 1 / factor_);
    }

    // beat dot
    bool flash = bpm_ > 0 && now - lastBeatMs_ < FLASH_MS;
    c.fillCircle(210, 44, 16, flash ? GREEN : 0x2104);
    c.drawCircle(210, 44, 16, DARKGREY);

    // status line
    c.setTextSize(1);
    c.setCursor(4, 84);
    if (tap_) {
      c.setTextColor(YELLOW);
      if (taps_.taps() < 3) c.printf("keep tapping... (%d)", taps_.taps());
      else c.printf("%d taps", taps_.taps());
    } else if (bpm_ == 0) {
      c.setTextColor(WHITE);
      c.print(tempo_->ready() ? "play music with a clear beat..." : "listening...");
    } else {
      // how clearly the rhythm repeats
      c.setTextColor(WHITE);
      c.print("rhythm");
      int w = (int)(150 * std::min(1.0f, confidence_ / 0.6f));
      uint16_t color = confidence_ < MIN_CONFIDENCE ? RED : confidence_ < 0.3f ? YELLOW : GREEN;
      c.fillRect(50, 84, w, 7, color);
      c.drawRect(50, 84, 150, 7, DARKGREY);
    }

    ui::footerHelp();
  }

 private:
  void restart() {
    tempo_.reset(new dsp::TempoEstimator(onsets_->frameRate()));
    taps_.reset();
    tap_ = false;
    bpm_ = 0;
    confidence_ = 0;
    sureness_ = 0;
    factor_ = 1;
    candidate_ = 0;
    candidateCount_ = 0;
    lastEstimateMs_ = 0;
  }

  static bool same(float a, float b) { return fabsf(a / b - 1) < SAME_TEMPO; }

  // a new reading from the estimator
  void update(float bpm, float confidence) {
    if (bpm_ == 0) {
      setBpm(bpm);
      return;
    }
    // exactly half or double of the shown tempo: the same tempo, other octave
    if (same(bpm * 2, bpm_)) bpm *= 2;
    else if (same(bpm / 2, bpm_)) bpm /= 2;
    if (same(bpm, bpm_)) {
      // calm small changes; an unsure reading (e.g. the fading end of a song)
      // moves the shown tempo less
      float weight = 0.5f * std::min(1.0f, confidence / 0.5f);
      setBpm(bpm_ + weight * (bpm - bpm_));
      candidate_ = 0;
      candidateCount_ = 0;
      return;
    }
    // another tempo: show it only when it lasts
    if (candidate_ > 0 && same(bpm, candidate_)) {
      candidateCount_++;
    } else {
      candidate_ = bpm;
      candidateCount_ = 1;
    }
    bool related = same(bpm * 3, bpm_ * 2) || same(bpm * 2, bpm_ * 3);
    if (candidateCount_ >= (related ? CONFIRM_RELATED : CONFIRM_NEW)) {
      setBpm(bpm);
      candidate_ = 0;
      candidateCount_ = 0;
    }
  }

  // the last DUMP_FRAMES frames of per-band onsets, as 1/1000 units
  void remember(const float *bands) {
    int b = onsets_->bands();
    if (dumpRing_.size() != DUMP_FRAMES * b) {
      dumpRing_.assign(DUMP_FRAMES * b, 0);
      dumpPos_ = dumpCount_ = 0;
    }
    for (int k = 0; k < b; k++)
      dumpRing_[dumpPos_ * b + k] = (uint16_t)std::min(65535.0f, bands[k] * 1000);
    dumpPos_ = (dumpPos_ + 1) % DUMP_FRAMES;
    if (dumpCount_ < DUMP_FRAMES) dumpCount_++;
  }

  void dump() {
    int b = onsets_->bands();
    Serial.printf("BANDS BEGIN rate=%.1f bands=%d n=%u shown=%.1f\n", onsets_->frameRate(), b,
                  (unsigned)dumpCount_, bpm_ * factor_);
    size_t oldest = (dumpPos_ + DUMP_FRAMES - dumpCount_) % DUMP_FRAMES;
    for (size_t f = 0; f < dumpCount_; f++) {
      size_t row = (oldest + f) % DUMP_FRAMES;
      for (int k = 0; k < b; k++) Serial.printf(k ? ",%u" : "%u", dumpRing_[row * b + k]);
      Serial.println();
    }
    Serial.println("BANDS END");
  }

  void setBpm(float bpm) {
    if (bpm_ == 0) nextBeatMs_ = millis();
    bpm_ = bpm;
  }

  void tapAt(uint32_t ms) {
    float bpm = taps_.tap(ms);
    if (bpm > 0) setBpm(bpm);
    nextBeatMs_ = ms;                           // the beat is where you tap
  }

  // move the beat clock forward and remember when the last beat was
  void advanceBeatClock(uint32_t now) {
    if (bpm_ <= 0) return;
    float period = 60000.0f / (bpm_ * factor_);
    while ((int32_t)(now - nextBeatMs_) >= 0) {
      lastBeatMs_ = nextBeatMs_;
      nextBeatMs_ += (uint32_t)period;
    }
  }

  // a clear onset near a predicted beat pulls the beat clock towards it
  void nudgeBeatClock(uint32_t now) {
    if (bpm_ <= 0) return;
    advanceBeatClock(now);
    float period = 60000.0f / (bpm_ * factor_);
    float sinceLast = (float)(now - lastBeatMs_);
    float error = sinceLast < period / 2 ? sinceLast : sinceLast - period;   // + late, - early
    if (fabsf(error) < period / 4) nextBeatMs_ += (int32_t)(0.3f * error);
  }

  std::unique_ptr<dsp::OnsetDetector> onsets_;
  std::unique_ptr<dsp::TempoEstimator> tempo_;
  dsp::TapTempo taps_;
  bool tap_ = false;
  float bpm_ = 0, confidence_ = 0, factor_ = 1;
  float sureness_ = 0;                 // slow average of the confidence
  float candidate_ = 0;                // a new tempo waiting for confirmation
  int candidateCount_ = 0;             // readings in a row that agree on it
  std::vector<uint16_t> dumpRing_;
  size_t dumpPos_ = 0, dumpCount_ = 0;
  uint32_t lastEstimateMs_ = 0, nextBeatMs_ = 0, lastBeatMs_ = 0;
  uint32_t lastTapMs_ = 0;
};

BpmApp instance;

}  // namespace

App *bpmApp() { return &instance; }
