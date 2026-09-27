// Mic test: loudness in dBFS, waveform, pitch and note.
// Keys: ; / . change the codec gain, g runs an automatic gain test,
//       d arms a raw sample dump: the recording starts by itself at the next
//       pluck (a jump in level; the click of the d key is ignored), keeps 0.1 s
//       before it and lasts up to 3 s; then it is sent to the serial console.
// Used to check the microphone and as a simple example of an app.
#include <algorithm>
#include <vector>
#include "apps.h"
#include "level.h"
#include "notes.h"
#include "pitch.h"
#include "../hw/es8311.h"
#include "../services/audio_in.h"
#include "../services/settings.h"
#include "../services/ui.h"

namespace {

const int RATE = 16000;
const size_t WINDOW = 2048;      // samples analysed at once (128 ms)
const size_t HOP = 1024;         // analyse after every 1024 new samples (~16x per second)
const size_t DUMP_SAMPLES = 3 * RATE;      // wanted length (shorter if RAM is short)
const size_t PREROLL = RATE / 10;          // 0.1 s kept before the pluck
const uint32_t IGNORE_KEY_MS = 400;        // the key click right after d
const float TRIGGER_DB = 15;               // this much above the quiet level

class MicTestApp : public App {
 public:
  const char *name() const override { return "Mic test"; }
  uint32_t sampleRate() const override { return RATE; }
  int micGain() const override { return settings::getInt("g_mic", 24); }

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
    std::vector<int16_t>().swap(dump_);
    std::vector<int16_t>().swap(preroll_);
    dumpState_ = DumpState::Idle;
  }

  void process(const int16_t *samples, size_t count) override {
    // a dump can also be requested from the PC by sending "d" over serial
    while (Serial.available()) {
      if (Serial.read() == 'd') startDump();
    }
    if (dumpState_ != DumpState::Idle) collectDump(samples, count);
    for (size_t i = 0; i < count; i++) {
      history_[writePos_] = samples[i];
      writePos_ = (writePos_ + 1) % WINDOW;
      if (++newSamples_ >= HOP) {
        newSamples_ = 0;
        analyse();
      }
    }
  }

  int help(const ui::HelpItem *&items) const override {
    static const ui::HelpItem HELP[] = {
        {"; .", "gain + / - (3 dB steps)"},
        {"g", "gain test: play a steady"},
        {"", "tone, see serial log"},
        {"d", "send 3 s of sound to PC"},
        {nullptr, "CLIP: too loud, less gain"},
    };
    items = HELP;
    return sizeof(HELP) / sizeof(HELP[0]);
  }

  void onKey(const Key &key) override {
    if (key.ch == ';' || key.ch == '.') {
      es8311::setPgaGain(es8311::pgaGain() + (key.ch == ';' ? 3 : -3));
      settings::putInt("g_mic", es8311::pgaGain());
      ui::flashGain(es8311::pgaGain());
    }
    if (key.ch == 'g' && !sweeping_) startSweep();
    if (key.ch == 'd') startDump();
  }

  void draw(M5Canvas &c) override {
    char right[24];
    snprintf(right, sizeof(right), "PGA %d dB", es8311::pgaGain());
    const char *title = dumpState_ == DumpState::Armed       ? "ARMED - pluck now!"
                        : dumpState_ == DumpState::Recording ? "RECORDING..."
                        : sweeping_                          ? "GAIN TEST - steady tone!"
                                                             : "Mic test";
    ui::header(title, right);

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
      c.setTextColor(WHITE);
      c.print("   --- Hz");
    }

    ui::footerHelp();
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
      hz = yin_->detect(signal_.data() + WINDOW - need, need) * audio_in::rateCorrection();
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
      Serial.printf("gain=%ddB dBFS=%.1f peak=%d f=%.2fHz rate=%.2f dropped=%u\n",
                    es8311::pgaGain(), db, peak_, hz, audio_in::measuredRate(),
                    (unsigned)audio_in::droppedSamples());
    }
  }

  // Raw sample dump, printed as text lines
  //   DUMP BEGIN rate=<nominal> n=<count>
  //   <32 comma-separated samples per line>
  //   DUMP END
  // Armed by d; starts by itself at the next pluck (see the top of the file).
  void startDump() {
    if (dumpState_ != DumpState::Idle) return;
    size_t room = (ESP.getMaxAllocHeap() > 16384 ? ESP.getMaxAllocHeap() - 16384 : 0) / sizeof(int16_t);
    dumpLength_ = std::min(DUMP_SAMPLES, room);
    if (dumpLength_ < RATE) {
      Serial.println("DUMP: not enough memory");
      return;
    }
    dump_.reserve(dumpLength_);
    preroll_.assign(PREROLL, 0);
    prerollPos_ = 0;
    quietDb_ = 0;
    armedMs_ = millis();
    dumpState_ = DumpState::Armed;
    Serial.printf("DUMP armed (%u samples)\n", (unsigned)dumpLength_);
  }

  void collectDump(const int16_t *samples, size_t count) {
    if (dumpState_ == DumpState::Armed) {
      // level of this chunk
      double p = 0;
      for (size_t i = 0; i < count; i++) p += (double)samples[i] * samples[i];
      float db = 10 * log10f((float)(p / count) / (32768.0f * 32768.0f) + 1e-12f);
      bool listening = millis() - armedMs_ > IGNORE_KEY_MS;
      if (listening && quietDb_ == 0) quietDb_ = db;
      if (listening && db < quietDb_) quietDb_ = db;
      if (listening && db > quietDb_ + TRIGGER_DB) {
        // the pluck: start with the 0.1 s kept before it
        for (size_t i = 0; i < PREROLL; i++) dump_.push_back(preroll_[(prerollPos_ + i) % PREROLL]);
        dumpState_ = DumpState::Recording;
        Serial.println("DUMP recording");
      } else {
        for (size_t i = 0; i < count; i++) {
          preroll_[prerollPos_] = samples[i];
          prerollPos_ = (prerollPos_ + 1) % PREROLL;
        }
        return;
      }
    }
    for (size_t i = 0; i < count && dump_.size() < dumpLength_; i++) dump_.push_back(samples[i]);
    if (dump_.size() < dumpLength_) return;
    Serial.printf("DUMP BEGIN rate=%d n=%u\n", RATE, (unsigned)dump_.size());
    for (size_t i = 0; i < dump_.size(); i += 32) {
      for (size_t k = i; k < i + 32 && k < dump_.size(); k++) Serial.printf(k == i ? "%d" : ",%d", dump_[k]);
      Serial.println();
    }
    Serial.println("DUMP END");
    std::vector<int16_t>().swap(dump_);
    std::vector<int16_t>().swap(preroll_);
    dumpState_ = DumpState::Idle;
  }

  // Automatic gain test: PGA 0, 6 ... 30 dB, 2 s each, average level printed.
  // With a steady tone the level should rise by about 6 dB per step.
  void startSweep() {
    sweeping_ = true;
    gainBeforeSweep_ = es8311::pgaGain();
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
      es8311::setPgaGain(gainBeforeSweep_);          // back to the gain used before
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

  enum class DumpState { Idle, Armed, Recording };
  DumpState dumpState_ = DumpState::Idle;
  std::vector<int16_t> dump_;      // raw samples being collected for a dump
  std::vector<int16_t> preroll_;   // the last 0.1 s while armed
  size_t prerollPos_ = 0, dumpLength_ = 0;
  float quietDb_ = 0;
  uint32_t armedMs_ = 0;

  bool sweeping_ = false;
  int gainBeforeSweep_ = 24;
  int sweepGain_ = 0, sweepBlocks_ = 0;
  uint32_t sweepStartMs_ = 0;
  float sweepPower_ = 0;
};

MicTestApp instance;

}  // namespace

App *micTestApp() { return &instance; }
