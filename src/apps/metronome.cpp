// Metronome: clicks with an accented first beat, 30 ... 250 BPM.
//
// Timing: the clicks are started by a FreeRTOS task of their own (core 0,
// high priority), not by the main loop: drawing the display takes up to
// ~15 ms, which would be audible as uneven clicks. The task sleeps until
// ~2 ms before a beat and then waits for the exact microsecond.
// Sound: a short decaying sine ("tick") played with the speaker; the
// microphone is not used (it shares the I2S bus with the speaker).
// The tempo can be taken from the BPM app (key l), which saves its last tempo.
//
// Keys: Enter start/stop, , / tempo -1/+1, - = tempo -5/+5, space tap tempo,
//       m time signature 2/4 3/4 4/4 6/8, ; . volume, l tempo from the BPM app.
#include <M5Unified.h>
#include <atomic>
#include <cmath>
#include <esp_timer.h>
#include "apps.h"
#include "tempo.h"
#include "../hw/board.h"
#include "../hw/es8311.h"
#include "../services/settings.h"
#include "../services/ui.h"

namespace {

const int MIN_BPM = 30, MAX_BPM = 250;
const int METERS[] = {2, 3, 4, 6};         // beats per bar; 6 = 6/8 (accents on 1 and 4)
const char *METER_NAMES[] = {"2/4", "3/4", "4/4", "6/8"};
const int CLICK_RATE = 44100;
const int CLICK_SAMPLES = CLICK_RATE * 40 / 1000;   // 40 ms (the Cardputer's click uses 25 ms)
const uint32_t FLASH_MS = 90;

// the three click sounds: first beat, secondary accent (6/8), other beats
int16_t clickAccent[CLICK_SAMPLES], clickMiddle[CLICK_SAMPLES], clickBeat[CLICK_SAMPLES];

// A decaying sine: 25 ms with a 6 ms decay on the Cardputer; on the StickS3
// (quieter speaker) 40 ms with a 10 ms decay. The rest of the buffer is silence.
void makeClick(int16_t *out, float hz, float level) {
  bool loud = board::loudClicks();
  float decay = loud ? 0.010f : 0.006f;
  int length = loud ? CLICK_SAMPLES : CLICK_RATE * 25 / 1000;
  for (int i = 0; i < CLICK_SAMPLES; i++) {
    float t = (float)i / CLICK_RATE;
    out[i] = i < length ? (int16_t)(level * 30000 * expf(-t / decay) * sinf(2 * (float)M_PI * hz * t)) : 0;
  }
}

class MetronomeApp : public App {
 public:
  const char *name() const override { return "Metronome"; }
  uint32_t sampleRate() const override { return 0; }   // no microphone

  void enter() override {
    bpm_ = constrain(settings::getInt("met_bpm", 120), MIN_BPM, MAX_BPM);
    meter_ = constrain(settings::getInt("met_meter", 2), 0, 3);
    volume_ = constrain(settings::getInt("met_vol", 7), 0, 10);
    fromBpmApp_ = settings::getInt("bpm_last", 0);
    bool loud = board::loudClicks();
    makeClick(clickAccent, 2000, 1.0f);
    makeClick(clickMiddle, 1600, loud ? 0.9f : 0.8f);
    makeClick(clickBeat, 1300, loud ? 0.85f : 0.6f);
    board::prepareSpeaker(CLICK_RATE);             // StickS3: no rate conversion
    M5.Speaker.begin();
    applyVolume();
    running_ = false;
    quit_ = false;
    taskDone_ = false;
    // without memory for the task there is no clock: exit() must not wait for it
    taskOk_ = xTaskCreatePinnedToCore(clockTask, "metronome", 4096, this, 5, nullptr, 0) == pdPASS;
    if (!taskOk_) taskDone_ = true;
  }

  void exit() override {
    running_ = false;
    quit_ = true;
    while (!taskDone_) delay(1);                 // wait for the clock task to end
    M5.Speaker.stop();
    M5.Speaker.end();
    es8311::speakerOff();                        // otherwise the idle amplifier hums
    settings::putInt("met_bpm", bpm_);
    settings::putInt("met_meter", meter_.load());
    settings::putInt("met_vol", volume_);
  }

  void process(const int16_t *, size_t) override {}

  // the M5StickS3 action menu (double click on A)
  int actions(const Action *&items) const override {
    static const Action ACTIONS[] = {
        {"Tempo +5", {'='}},
        {"Tempo -5", {'-'}},
        {"Time signature", {'m'}},
        {"Volume +", {';'}},
        {"Volume -", {'.'}},
        {"Tempo from BPM", {'l'}},
    };
    items = ACTIONS;
    return sizeof(ACTIONS) / sizeof(ACTIONS[0]);
  }

  int help(const ui::HelpItem *&items) const override {
    static const ui::HelpItem HELP[] = {
        {"Enter", "start / stop"},
        {", /", "tempo -1 / +1"},
        {"- =", "tempo -5 / +5"},
        {"space", "tap the tempo"},
        {"m", "2/4  3/4  4/4  6/8"},
        {"; .", "volume + / -"},
        {"l", "take the tempo from BPM"},
    };
    items = HELP;
    return sizeof(HELP) / sizeof(HELP[0]);
  }

  void onKey(const Key &key) override {
    if (key.enter && taskOk_) {
      if (!running_) {
        beat_ = 0;
        nextUs_ = esp_timer_get_time() + 20000;  // first click in 20 ms
      }
      running_ = !running_;
    }
    if (key.ch == ',') setBpm(bpm_ - 1);
    if (key.ch == '/') setBpm(bpm_ + 1);
    if (key.ch == '-') setBpm(bpm_ - 5);
    if (key.ch == '=') setBpm(bpm_ + 5);
    if (key.ch == 'm') {
      meter_ = (meter_ + 1) % 4;
      beat_ = 0;
    }
    if (key.ch == ';' || key.ch == '.') {
      volume_ = constrain(volume_ + (key.ch == ';' ? 1 : -1), 0, 10);
      applyVolume();
      ui::flashLevel("VOLUME", volume_, 10, "");
    }
    if (key.ch == 'l' && fromBpmApp_ > 0) setBpm(fromBpmApp_);
    if (key.ch == ' ') {
      float tapped = taps_.tap(millis());
      if (tapped > 0) setBpm((int)lroundf(tapped));
    }
  }

  void draw(M5Canvas &c) override {
    ui::header("Metronome", METER_NAMES[meter_.load()]);
    int beats = METERS[meter_.load()];
    int shown = shownBeat_;
    bool flash = running_ && (esp_timer_get_time() - lastBeatUs_) < FLASH_MS * 1000;

    // flash the frame on every beat (orange on the first beat)
    if (flash) {
      uint16_t color = shown == 0 ? ORANGE : GREEN;
      for (int i = 0; i < 3; i++) c.drawRect(i, ui::HEADER_HEIGHT + i, ui::WIDTH - 2 * i, ui::HEIGHT - ui::HEADER_HEIGHT - 12 - 2 * i, color);
    }

    // tempo
    c.setTextSize(6);
    c.setTextColor(WHITE);
    char text[8];
    snprintf(text, sizeof(text), "%d", bpm_.load());
    int w = c.textWidth(text);
    c.setCursor((ui::WIDTH - w) / 2 - 20, 20);
    c.print(text);
    c.setTextSize(2);
    c.setCursor((ui::WIDTH + w) / 2 - 14, 44);
    c.print("BPM");

    // beats of the bar
    int boxW = (ui::WIDTH - 20) / beats - 6;
    for (int b = 0; b < beats; b++) {
      int x = 10 + b * (boxW + 6), y = 78;
      bool lit = running_ && b == shown;
      uint16_t color = b == 0 ? ORANGE : (beats == 6 && b == 3) ? YELLOW : GREEN;
      if (lit) c.fillRect(x, y, boxW, 16, color);
      else c.drawRect(x, y, boxW, 16, color);
    }

    // status
    c.setTextSize(1);
    c.setCursor(10, 102);
    c.setTextColor(running_ ? GREEN : WHITE);
    if (!taskOk_) {
      c.setTextColor(RED);
      c.print("not enough memory - Esc, try again");
    } else {
      c.print(running_ ? "running - Enter: stop" : "Enter: start");
    }
    if (fromBpmApp_ > 0 && fromBpmApp_ != bpm_.load()) {
      c.setTextColor(YELLOW);
      c.setCursor(10, 113);
      c.printf("l: take %d BPM from the BPM app", fromBpmApp_);
    }
    ui::footerHelp();
  }

 private:
  void setBpm(int bpm) { bpm_ = constrain(bpm, MIN_BPM, MAX_BPM); }
  void applyVolume() { M5.Speaker.setVolume(board::speakerVolume(volume_)); }

  static void clockTask(void *arg) {
    static_cast<MetronomeApp *>(arg)->clock();
  }

  // the beat clock: exact to about a millisecond
  void clock() {
    while (!quit_) {
      if (!running_) {
        vTaskDelay(pdMS_TO_TICKS(5));
        continue;
      }
      int64_t now = esp_timer_get_time();
      int64_t wait = nextUs_ - now;
      if (wait > 3000) {                         // sleep until ~2 ms before the beat
        vTaskDelay(pdMS_TO_TICKS((wait - 2000) / 1000));
        continue;
      }
      while (esp_timer_get_time() < nextUs_) {}  // the last moment: wait exactly
      click(beat_);
      lastBeatUs_ = esp_timer_get_time();
      shownBeat_ = beat_.load();
      beat_ = (beat_.load() + 1) % METERS[meter_.load()];
      nextUs_ += (int64_t)(60e6 / bpm_);
      if (nextUs_ < esp_timer_get_time()) nextUs_ = esp_timer_get_time() + (int64_t)(60e6 / bpm_);
    }
    taskDone_ = true;
    vTaskDelete(nullptr);
  }

  void click(int beat) {
    const int16_t *sound = beat == 0 ? clickAccent : (METERS[meter_] == 6 && beat == 3) ? clickMiddle : clickBeat;
    M5.Speaker.playRaw(sound, CLICK_SAMPLES, CLICK_RATE, false, 1, 0, true);
  }

  // shared with the clock task on the other core
  std::atomic<bool> running_{false}, quit_{false}, taskDone_{true};
  std::atomic<int> bpm_{120}, beat_{0}, shownBeat_{0}, meter_{2};
  std::atomic<int64_t> nextUs_{0}, lastBeatUs_{0};
  int volume_ = 7, fromBpmApp_ = 0;
  bool taskOk_ = false;
  dsp::TapTempo taps_;
};

MetronomeApp instance;

}  // namespace

App *metronomeApp() { return &instance; }
