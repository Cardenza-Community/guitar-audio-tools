// Guitar tuner with a needle gauge (-50 ... +50 cents, 5-cent ticks,
// green centre = within 3 cents).
//
// Modes (key m, remembered):
//  - GUITAR: the nearest string of the standard tuning E2 A2 D3 G3 B3 E4.
//    A string that is far out of tune still shows its own name, e.g. "E -90",
//    with a "tune up" / "tune down" hint.
//  - CHROMATIC: the nearest of all 12 notes, for other instruments,
//    other tunings or singing.
// Other keys: , / reference pitch A4 -1 / +1 Hz (remembered), ; . mic gain,
// Enter = m.
#include <memory>
#include <vector>
#include "apps.h"
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
const size_t HOP = 512;                  // a new reading every 32 ms
const float MIN_LEVEL_DBFS = -70;        // quieter than this: no pitch search
const float IN_TUNE_CENTS = 3;
// A plucked string starts sharp and settles (the low E by 20...35 cents in the
// first second, measured). The needle is thin and light during this time.
const uint32_t SETTLE_MS = 500;
// Pitch search range. GUITAR mode only looks where strings can be (E2 - 4
// semitones ... E4 + 4 semitones), so an octave error at the pluck (e.g. the
// 2nd harmonic of the high E, 659 Hz) cannot pin the needle to the right.
const float MIN_HZ = 60, MAX_HZ_GUITAR = 420, MAX_HZ_CHROMATIC = 1100;

// needle gauge geometry
const int PIVOT_X = 120, PIVOT_Y = 122, RADIUS = 100;
const float DEGREES_PER_CENT = 1.4f;     // +-50 cents = +-70 degrees

class TunerApp : public App {
 public:
  const char *name() const override { return "Guitar tuner"; }
  uint32_t sampleRate() const override { return RATE; }
  int micGain() const override { return settings::getInt("g_tuner", 24); }

  void enter() override {
    chromatic_ = settings::getInt("tun_chrom", 0);
    makeDetector();
    window_ = yin_->samplesNeeded();
    history_.assign(window_, 0);
    ordered_.assign(window_, 0);
    signal_.assign(window_, 0);
    writePos_ = newSamples_ = 0;
    smoother_.reset();
    a4_ = settings::getFloat("a4", 440.0f);
    hz_ = 0;
    lastPitchMs_ = 0;
    needleCents_ = 0;
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
        {"Enter", "guitar / chromatic mode"},
        {", /", "reference A4 -1 / +1 Hz"},
        {"; .", "microphone gain + / -"},
        {nullptr, "green: in tune (within 3 c)"},
        {nullptr, "guitar: nearest string EADGBE"},
        {nullptr, "chromatic: nearest of 12 notes"},
    };
    items = HELP;
    return sizeof(HELP) / sizeof(HELP[0]);
  }

  void onKey(const Key &key) override {
    if (key.ch == 'm' || key.enter) {
      chromatic_ = !chromatic_;
      settings::putInt("tun_chrom", chromatic_);
      makeDetector();
      smoother_.reset();
    }
    if (key.ch == ',' || key.ch == '/') {
      a4_ = constrain(a4_ + (key.ch == '/' ? 1 : -1), 430.0f, 450.0f);
      settings::putFloat("a4", a4_);
    }
    if (key.ch == ';' || key.ch == '.') {
      es8311::setPgaGain(es8311::pgaGain() + (key.ch == ';' ? 3 : -3));
      settings::putInt("g_tuner", es8311::pgaGain());
      ui::flashGain(es8311::pgaGain());
    }
  }

  void draw(M5Canvas &c) override {
    char right[24];
    snprintf(right, sizeof(right), "%s  A4=%.0f", chromatic_ ? "CHROMATIC" : "GUITAR", a4_);
    ui::header("Tuner", right);

    // what to show: live pitch, or the last one (grey) for 3 s after it ends
    bool live = hz_ > 0;
    bool show = live || (lastPitchMs_ && millis() - lastPitchMs_ < 3000);
    float cents = 0;
    char noteName[4] = "";
    int octave = 0, stringNumber = 0;
    if (show) {
      if (chromatic_) {
        dsp::Note n = dsp::noteFromFrequency(lastHz_, a4_);
        cents = n.cents;
        snprintf(noteName, sizeof(noteName), "%s", n.name);
        octave = n.octave;
      } else {
        dsp::StringMatch m = dsp::nearestGuitarString(lastHz_, a4_);
        dsp::Note n = dsp::noteFromFrequency(dsp::frequencyOfMidi(m.midi, a4_), a4_);
        cents = m.cents;
        snprintf(noteName, sizeof(noteName), "%s", n.name);
        octave = n.octave;
        stringNumber = 6 - m.index;
      }
    }
    bool inTune = live && fabsf(cents) <= IN_TUNE_CENTS;

    // the needle glides towards its target instead of jumping
    float target = show ? constrain(cents, -50.0f, 50.0f) : 0;
    needleCents_ += 0.35f * (target - needleCents_);

    drawScale(c);
    if (show) {
      // right after a pluck the string is still settling (it starts a little
      // sharp): a thin light needle; then the normal coloured one
      bool settling = live && (int32_t)(settleUntilMs_ - millis()) > 0;
      uint16_t color = !live || settling ? LIGHTGREY
                       : inTune          ? GREEN
                       : fabsf(cents) <= 15 ? YELLOW : ORANGE;
      c.drawWideLine(pointX(38, needleCents_), pointY(38, needleCents_),
                     pointX(RADIUS - 3, needleCents_), pointY(RADIUS - 3, needleCents_),
                     settling ? 0.5f : 1.5f, color);
    }

    // note name in the middle, octave small next to it
    if (show) {
      uint16_t color = live && inTune ? GREEN : WHITE;   // held reading: white (no grey text)
      c.setTextColor(color);
      c.setTextSize(4);
      int w = c.textWidth(noteName);
      c.setCursor(PIVOT_X - w / 2 - 6, 88);
      c.print(noteName);
      c.setTextSize(2);
      c.setCursor(PIVOT_X + w / 2 - 4, 102);
      c.print(octave);

      // deviation in cents (left) and frequency / string (right)
      c.setTextColor(WHITE);
      c.setCursor(2, 100);
      c.printf("%+.0f c", cents);
      if (live && !inTune && fabsf(cents) > 50) {
        c.setTextSize(1);
        c.setTextColor(ORANGE);
        c.setCursor(2, 117);
        c.print(cents < 0 ? "tune up" : "tune down");
      }
      c.setTextSize(1);
      c.setTextColor(WHITE);
      char hzText[16];
      snprintf(hzText, sizeof(hzText), "%.1f Hz", lastHz_);
      c.setCursor(ui::WIDTH - 2 - c.textWidth(hzText), 104);
      c.print(hzText);
      if (stringNumber) {
        char stringText[12];
        snprintf(stringText, sizeof(stringText), "string %d", stringNumber);
        c.setCursor(ui::WIDTH - 2 - c.textWidth(stringText), 114);
        c.print(stringText);
      }
    } else {
      c.setTextSize(1);
      c.setTextColor(WHITE);
      const char *hint = "play a string";
      c.setCursor(PIVOT_X - c.textWidth(hint) / 2, 100);
      c.print(hint);
    }

    ui::drawA4(a4_);
    ui::footerHelp();
  }

 private:
  // Both ranges use the same buffer length (it depends on the lowest pitch).
  void makeDetector() {
    yin_.reset(new dsp::YinDetector(RATE, MIN_HZ, chromatic_ ? MAX_HZ_CHROMATIC : MAX_HZ_GUITAR, 1024));
  }

  static float angleOf(float cents) { return (270 + cents * DEGREES_PER_CENT) * DEG_TO_RAD; }
  static int pointX(float r, float cents) { return PIVOT_X + (int)lroundf(r * cosf(angleOf(cents))); }
  static int pointY(float r, float cents) { return PIVOT_Y + (int)lroundf(r * sinf(angleOf(cents))); }

  // Arc with ticks every 5 cents, labels, and the green "in tune" zone.
  void drawScale(M5Canvas &c) {
    float a0 = 270 - 50 * DEGREES_PER_CENT, a1 = 270 + 50 * DEGREES_PER_CENT;
    c.fillArc(PIVOT_X, PIVOT_Y, RADIUS, RADIUS - 1, a0, a1, DARKGREY);
    c.fillArc(PIVOT_X, PIVOT_Y, RADIUS, RADIUS - 14, 270 - IN_TUNE_CENTS * DEGREES_PER_CENT,
              270 + IN_TUNE_CENTS * DEGREES_PER_CENT, DARKGREEN);
    for (int cents = -50; cents <= 50; cents += 5) {
      int length = cents == 0 ? 16 : (cents % 10 == 0 ? 10 : 5);
      uint16_t color = cents == 0 ? WHITE : LIGHTGREY;
      c.drawLine(pointX(RADIUS, cents), pointY(RADIUS, cents), pointX(RADIUS - length, cents),
                 pointY(RADIUS - length, cents), color);
    }
    c.setTextSize(1);
    c.setTextColor(WHITE);
    const int labels[] = {-50, -25, 25, 50};
    for (int cents : labels) {
      char text[6];
      snprintf(text, sizeof(text), "%+d", cents);
      c.setCursor(pointX(RADIUS - 24, cents) - c.textWidth(text) / 2, pointY(RADIUS - 24, cents) - 3);
      c.print(text);
    }
  }

  void analyse() {
    for (size_t i = 0; i < window_; i++) ordered_[i] = history_[(writePos_ + i) % window_];
    dsp::removeDc(ordered_.data(), signal_.data(), window_);
    float db = dsp::toDbfs(dsp::rms(signal_.data(), window_));
    float raw = db > MIN_LEVEL_DBFS ? yin_->detect(signal_.data(), window_) : 0;
    // YIN assumes exactly RATE samples per second; correct for the real rate
    raw *= audio_in::rateCorrection();

    float aperiodicity = raw > 0 ? yin_->lastAperiodicity() : 1;
    hz_ = smoother_.push(raw, aperiodicity, db);
    if (smoother_.notes() != notesSeen_) {         // a new pluck: let it settle
      notesSeen_ = smoother_.notes();
      settleUntilMs_ = millis() + SETTLE_MS;
    }
    if (hz_ > 0) {
      lastHz_ = hz_;
      lastPitchMs_ = millis();
    }

    if (millis() - lastLogMs_ >= 250) {
      lastLogMs_ = millis();
      Serial.printf("raw=%.2f ap=%.3f steady=%.2f dBFS=%.1f gain=%d a4=%.0f rate=%.2f corr=%+.0fppm dropped=%u\n",
                    raw, aperiodicity, hz_, db, es8311::pgaGain(), a4_, audio_in::measuredRate(),
                    (audio_in::rateCorrection() - 1) * 1e6f, (unsigned)audio_in::droppedSamples());
    }
  }

  std::unique_ptr<dsp::YinDetector> yin_;
  size_t window_ = 0, writePos_ = 0, newSamples_ = 0;
  std::vector<int16_t> history_, ordered_;
  std::vector<float> signal_;
  dsp::PitchSmoother smoother_;

  bool chromatic_ = false;
  float a4_ = 440;
  float hz_ = 0, lastHz_ = 0, needleCents_ = 0;
  uint32_t lastPitchMs_ = 0, lastLogMs_ = 0;
  unsigned notesSeen_ = 0;
  uint32_t settleUntilMs_ = 0;
};

TunerApp instance;

}  // namespace

App *tunerApp() { return &instance; }
