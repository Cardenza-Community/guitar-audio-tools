// Intonation: checks the intonation of each guitar string (electric guitars:
// the saddle of the string can be moved).
//
// 1. Play the open string, or its harmonic at the 12th fret. The app finds
//    out which string it is (the guitar must be tuned first).
// 2. Play the same string pressed at the 12th fret: it must sound exactly one
//    octave above the open string.
// Sharp (too high): the string is too short -> move the saddle back, away from
// the neck. Flat: move it forward, towards the neck. Within 2 cents: OK.
// Each note is measured on its settled part (0.4 ... 1.6 s after the pluck,
// median), so pluck both notes with about the same strength.
// Keys: Enter measure again, r clear all strings, ; . microphone gain.
#include <memory>
#include <vector>
#include "apps.h"
#include "intonation.h"
#include "level.h"
#include "notes.h"
#include "pitch.h"
#include "tuning.h"
#include "../hw/es8311.h"
#include "../services/audio_in.h"
#include "../services/settings.h"
#include "../services/ui.h"

namespace {

const int RATE = 16000;
const size_t HOP = 512;                  // a pitch reading every 32 ms
const float OK_CENTS = 2, CLOSE_CENTS = 5;
const char *STRING_NAMES[] = {"E", "A", "D", "G", "B", "e"};

enum class Step { Reference, Fretted };

class IntonationApp : public App {
 public:
  const char *name() const override { return "Intonation"; }
  uint32_t sampleRate() const override { return RATE; }
  int micGain() const override { return settings::getInt("g_int", 24); }

  void enter() override {
    yin_.reset(new dsp::YinDetector(RATE, 60, 1100, 1024));
    window_ = yin_->samplesNeeded();
    history_.assign(window_, 0);
    ordered_.assign(window_, 0);
    signal_.assign(window_, 0);
    writePos_ = newSamples_ = 0;
    a4_ = settings::getFloat("a4", 440.0f);
    for (float &r : results_) r = NAN;
    lastString_ = -1;                              // the instance lives on between visits
    restart();
  }

  void exit() override {
    yin_.reset();
    std::vector<int16_t>().swap(history_);
    std::vector<int16_t>().swap(ordered_);
    std::vector<float>().swap(signal_);
  }

  void process(const int16_t *samples, size_t count) override {
    for (size_t i = 0; i < count; i++) {
      history_[writePos_] = samples[i];
      writePos_ = (writePos_ + 1) % window_;
      if (++newSamples_ >= HOP) {
        newSamples_ = 0;
        analyse();
      }
    }
  }

  int help(const ui::HelpItem *&items) const override {
    static const ui::HelpItem HELP[] = {
        {nullptr, "1. open string (or its 12th"},
        {nullptr, "   fret harmonic), let it ring"},
        {nullptr, "2. the same string at fret 12"},
        {nullptr, "sharp: saddle back (from neck)"},
        {nullptr, "flat: saddle forward (to neck)"},
        {"Enter", "again   r: clear all"},
        {"; .", "microphone gain + / -"},
    };
    items = HELP;
    return sizeof(HELP) / sizeof(HELP[0]);
  }

  void onKey(const Key &key) override {
    if (key.enter) restart();
    if (key.ch == 'r') {
      for (float &r : results_) r = NAN;
      lastString_ = -1;
      restart();
    }
    if (key.ch == ';' || key.ch == '.') {
      es8311::setPgaGain(es8311::pgaGain() + (key.ch == ';' ? 3 : -3));
      settings::putInt("g_int", es8311::pgaGain());
      ui::flashGain(es8311::pgaGain());
    }
  }

  void draw(M5Canvas &c) override {
    ui::header("Intonation", step_ == Step::Fretted ? STRING_NAMES[reference_.string] : "");
    float now = millis() / 1000.0f;

    // what to play now
    c.setTextSize(2);
    c.setTextColor(YELLOW);
    c.setCursor(4, 16);
    if (step_ == Step::Reference) c.print("1: OPEN string");
    else c.printf("2: %s at FRET 12", STRING_NAMES[reference_.string]);
    c.setTextSize(1);
    c.setTextColor(WHITE);
    c.setCursor(4, 34);
    if (meter_.state() == dsp::NoteMeter::State::Measuring) {
      c.print("measuring - let it ring");
      int w = (int)(150 * meter_.progress(now));
      c.fillRect(150, 34, w / 2, 7, GREEN);
      c.drawRect(150, 34, 75, 7, WHITE);
    } else if (message_[0]) {
      c.setTextColor(ORANGE);
      c.print(message_);
    } else {
      c.print(step_ == Step::Reference ? "(or its harmonic at fret 12)" : "press the string at fret 12");
    }

    // the last result
    if (lastString_ >= 0 && !isnan(results_[lastString_])) {
      float cents = results_[lastString_];
      uint16_t color = fabsf(cents) <= OK_CENTS ? GREEN : fabsf(cents) <= CLOSE_CENTS ? YELLOW : ORANGE;
      c.setTextSize(3);
      c.setTextColor(color);
      c.setCursor(4, 50);
      c.printf("%s %+.1f c", STRING_NAMES[lastString_], cents);
      c.setTextSize(1);
      c.setCursor(4, 78);
      c.setTextColor(WHITE);
      if (fabsf(cents) <= OK_CENTS) c.print("OK - intonation is fine");
      else if (cents > 0) c.print("sharp: move the saddle BACK (from neck)");
      else c.print("flat: move the saddle FORWARD (to neck)");
    }

    // all strings
    for (int s = 0; s < dsp::GUITAR_STRINGS; s++) {
      int x = s * 40 + 2;
      c.setTextSize(1);
      c.setCursor(x, 100);
      c.setTextColor(YELLOW);
      c.print(STRING_NAMES[s]);
      c.setCursor(x, 111);
      float r = results_[s];
      if (isnan(r)) {
        c.setTextColor(WHITE);
        c.print("-");
      } else {
        c.setTextColor(fabsf(r) <= OK_CENTS ? GREEN : fabsf(r) <= CLOSE_CENTS ? YELLOW : ORANGE);
        if (fabsf(r) <= OK_CENTS) c.print("OK");
        else c.printf("%+.0f", r);
      }
    }
    ui::footerHelp();
  }

 private:
  void restart() {
    step_ = Step::Reference;
    meter_.reset();
    message_ = "";
  }

  void analyse() {
    for (size_t i = 0; i < window_; i++) ordered_[i] = history_[(writePos_ + i) % window_];
    dsp::removeDc(ordered_.data(), signal_.data(), window_);
    float db = dsp::toDbfs(dsp::rms(signal_.data(), window_));
    float hz = db > -70 ? yin_->detect(signal_.data(), window_) * audio_in::rateCorrection() : 0;
    float now = millis() / 1000.0f;

    dsp::NoteMeter::State state = meter_.push(now, hz, db);
    if (state == dsp::NoteMeter::State::Failed) {
      message_ = "too short or unclear - pluck again";
      meter_.reset();
      return;
    }
    if (state != dsp::NoteMeter::State::Done) return;
    float measured = meter_.result();
    meter_.reset();

    if (step_ == Step::Reference) {
      dsp::IntonationReference ref = dsp::classifyReference(measured, a4_);
      Serial.printf("reference %.2f Hz -> string %d%s\n", measured, ref.string, ref.harmonic ? " (harmonic)" : "");
      if (ref.string < 0) {
        message_ = "not an open string - tune first";
        return;
      }
      reference_ = ref;
      step_ = Step::Fretted;
      message_ = "";
    } else {
      float fromOctave = dsp::intonationCents(measured, reference_.octaveHz);
      Serial.printf("fret 12 %.2f Hz, octave %.2f Hz -> %+.1f c\n", measured, reference_.octaveHz, fromOctave);
      if (fabsf(fromOctave) > 100) {
        message_ = "not the 12th fret of this string";
        return;
      }
      results_[reference_.string] = fromOctave;
      lastString_ = reference_.string;
      restart();                                    // ready for the next string
    }
  }

  std::unique_ptr<dsp::YinDetector> yin_;
  size_t window_ = 0, writePos_ = 0, newSamples_ = 0;
  std::vector<int16_t> history_, ordered_;
  std::vector<float> signal_;
  dsp::NoteMeter meter_;
  Step step_ = Step::Reference;
  dsp::IntonationReference reference_;
  float results_[dsp::GUITAR_STRINGS];
  int lastString_ = -1;
  const char *message_ = "";
  float a4_ = 440;
};

IntonationApp instance;

}  // namespace

App *intonationApp() { return &instance; }
