// Mic test: loudness in dBFS, waveform, pitch and note.
// Keys: ; / . change the codec gain, g runs an automatic gain test.
// Used to check the microphone and as a simple example of an app.
#include <vector>
#include "apps.h"
#include "level.h"
#include "notes.h"
#include "pitch.h"
#include "../hw/es8311.h"
#include "../services/audio_in.h"
#include "../services/ui.h"

namespace {

const int RATE = 16000;
const size_t WINDOW = 2048;      // samples analysed at once (128 ms)
const size_t HOP = 1024;         // analyse after every 1024 new samples (~16x per second)

class MicTestApp : public App {
 public:
  const char *name() const override { return "Mic test"; }
  uint32_t sampleRate() const override { return RATE; }

  void enter() override {
    history_.assign(WINDOW, 0);
    ordered_.assign(WINDOW, 0);
    signal_.assign(WINDOW, 0);
    yin_ = new dsp::YinDetector(RATE, 60, 1000, 1024);
    newSamples_ = 0;
    smoothPower_ = 0;
    shownHz_ = 0;
    sweeping_ = false;
  }

  void exit() override {
    delete yin_;
    yin_ = nullptr;
    // free the memory for the next app
    std::vector<int16_t>().swap(history_);
    std::vector<int16_t>().swap(ordered_);
    std::vector<float>().swap(signal_);
  }

  void process(const int16_t *samples, size_t count) override {
    for (size_t i = 0; i < count; i++) {
      history_[writePos_] = samples[i];
      writePos_ = (writePos_ + 1) % WINDOW;
      if (++newSamples_ >= HOP) {
        newSamples_ = 0;
        analyse();
      }
    }
  }

  void onKey(const Key &key) override {
    if (key.ch == ';') es8311::setPgaGain(es8311::pgaGain() + 3);
    if (key.ch == '.') es8311::setPgaGain(es8311::pgaGain() - 3);
    if (key.ch == 'g' && !sweeping_) startSweep();
  }

  void draw(M5Canvas &c) override {
    char right[24];
    snprintf(right, sizeof(right), "PGA %d dB", es8311::pgaGain());
    ui::header(sweeping_ ? "GAIN TEST - steady tone!" : "Mic test", right);

    // loudness: number + bar (-80 dBFS left, 0 dBFS right)
    c.setTextSize(2);
    c.setTextColor(WHITE);
    c.setCursor(0, 16);
    c.printf("%6.1f dBFS", shownDb_);
    if (peak_ > 32000) {
      c.setTextColor(RED);
      c.setCursor(190, 16);
      c.print("CLIP");
    }
    int width = constrain((int)((shownDb_ + 80) * 3), 0, 240);
    uint16_t color = shownDb_ > -6 ? RED : (shownDb_ > -20 ? YELLOW : GREEN);
    c.fillRect(0, 34, width, 6, color);
    c.drawRect(0, 34, 240, 6, DARKGREY);

    // waveform, starting at an upward zero crossing so it stands still
    const int MID = 72, HEIGHT = 26;
    int start = 0;
    for (int i = 1; i < 400; i++) {
      if (signal_[i - 1] < 0 && signal_[i] >= 0) { start = i; break; }
    }
    float scale = (float)HEIGHT / max(peak_, 300);
    c.drawFastHLine(0, MID, 240, DARKGREY);
    for (int x = 1; x < 240; x++) {
      c.drawLine(x - 1, MID - signal_[start + x - 1] * scale, x, MID - signal_[start + x] * scale, CYAN);
    }

    // pitch and note
    c.setTextSize(2);
    c.setCursor(0, 104);
    if (shownHz_ > 0) {
      dsp::Note n = dsp::noteFromFrequency(shownHz_);
      c.setTextColor(fabsf(n.cents) <= 5 ? GREEN : WHITE);
      c.printf("%6.1f Hz %s%d %+d", shownHz_, n.name, n.octave, (int)lroundf(n.cents));
    } else {
      c.setTextColor(DARKGREY);
      c.print("   --- Hz");
    }

    ui::footer(";/. gain   g gain test   Esc menu");
  }

 private:
  // Copies the ring buffer in time order and computes everything.
  void analyse() {
    for (size_t i = 0; i < WINDOW; i++) ordered_[i] = history_[(writePos_ + i) % WINDOW];
    dsp::removeDc(ordered_.data(), signal_.data(), WINDOW);

    float rms = dsp::rms(signal_.data(), WINDOW);
    float db = dsp::toDbfs(rms);
    peak_ = dsp::peakAbs(ordered_.data(), WINDOW);
    // YIN rejects noise itself, so only skip near-silence;
    // the newest samples are used for the pitch
    float hz = 0;
    if (db > -75) {
      size_t need = yin_->samplesNeeded();
      hz = yin_->detect(signal_.data() + WINDOW - need, need);
    }

    // display smoothing: loudness = average power of ~0.25 s,
    // pitch = last detected value kept for 0.7 s
    smoothPower_ = 0.75f * smoothPower_ + 0.25f * rms * rms;
    shownDb_ = dsp::toDbfs(sqrtf(smoothPower_));
    if (hz > 0) {
      shownHz_ = hz;
      lastPitchMs_ = millis();
    } else if (millis() - lastPitchMs_ > 700) {
      shownHz_ = 0;
    }

    updateSweep(rms);

    if (millis() - lastPrintMs_ >= 250) {
      lastPrintMs_ = millis();
      Serial.printf("gain=%ddB dBFS=%.1f peak=%d f=%.1fHz rate=%.1f dropped=%u\n",
                    es8311::pgaGain(), db, peak_, hz, audio_in::measuredRate(),
                    (unsigned)audio_in::droppedSamples());
    }
  }

  // Automatic gain test: PGA 0, 6 ... 30 dB, 2 s each, average level printed.
  // With a steady tone the level should rise by about 6 dB per step.
  void startSweep() {
    sweeping_ = true;
    sweepGain_ = 0;
    nextSweepStep();
    Serial.println("SWEEP start");
  }

  void nextSweepStep() {
    es8311::setPgaGain(sweepGain_);
    sweepStartMs_ = millis();
    sweepPower_ = 0;
    sweepBlocks_ = 0;
  }

  void updateSweep(float rms) {
    if (!sweeping_ || millis() - sweepStartMs_ < 300) return;   // let the level settle
    sweepPower_ += rms * rms;
    sweepBlocks_++;
    if (millis() - sweepStartMs_ < 2000) return;
    Serial.printf("SWEEP gain=%2d dB  level=%.1f dBFS\n", sweepGain_,
                  dsp::toDbfs(sqrtf(sweepPower_ / sweepBlocks_)));
    sweepGain_ += 6;
    if (sweepGain_ > 30) {
      sweeping_ = false;
      Serial.println("SWEEP done");
    } else {
      nextSweepStep();
    }
  }

  std::vector<int16_t> history_;   // ring buffer of the newest WINDOW samples
  std::vector<int16_t> ordered_;   // the same in time order
  std::vector<float> signal_;      // in time order, DC removed
  size_t writePos_ = 0, newSamples_ = 0;
  dsp::YinDetector *yin_ = nullptr;

  float smoothPower_ = 0, shownDb_ = dsp::SILENCE_DB, shownHz_ = 0;
  int peak_ = 0;
  uint32_t lastPitchMs_ = 0, lastPrintMs_ = 0;

  bool sweeping_ = false;
  int sweepGain_ = 0, sweepBlocks_ = 0;
  uint32_t sweepStartMs_ = 0;
  float sweepPower_ = 0;
};

MicTestApp instance;

}  // namespace

App *micTestApp() { return &instance; }
