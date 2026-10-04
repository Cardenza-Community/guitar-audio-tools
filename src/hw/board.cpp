#include "board.h"
#include <deque>
#include <string>

#ifdef BOARD_STICKS3
#include <M5Unified.h>
#else
#include <M5Cardputer.h>
#endif

namespace board {

static std::deque<Input> pending;

static void push(const Key &key, Special special = Special::None) {
  Input input;
  input.key = key;
  input.special = special;
  pending.push_back(input);
}

bool nextInput(Input &input) {
  if (pending.empty()) return false;
  input = pending.front();
  pending.pop_front();
  return true;
}

#ifdef BOARD_STICKS3

// ---------- M5StickS3: two buttons ----------

void begin() {
  auto cfg = M5.config();
  M5.begin(cfg);
}

void update(bool) {
  M5.update();
  Key key;
  // A: the click count is decided a moment after the release (single or double)
  if (M5.BtnA.wasHold()) push(key, Special::Back);
  if (M5.BtnA.wasDecideClickCount()) {
    if (M5.BtnA.getClickCount() == 1) {
      key.enter = true;
      push(key);
    } else {
      push(key, Special::Menu);
    }
  }
  // B: plain click / hold, no waiting for a double click
  if (M5.BtnB.wasClicked()) {
    key.ch = '/';
    push(key);
  }
  if (M5.BtnB.wasHold()) {
    key.ch = ',';
    push(key);
  }
}

bool hasKeyboard() { return false; }
bool hasSdCard() { return false; }
uint8_t speakerVolume(int volume) { return constrain(volume, 0, 10) * 25; }   // 10 -> 250 (see board.h)
bool loudClicks() { return true; }
uint32_t autoPowerOffMs() { return 5 * 60 * 1000; }
void powerOff() { M5.Power.powerOff(); }

bool onExternalPower() {
  static bool external = false;
  static uint32_t lastRead = 0;
  if (lastRead == 0 || millis() - lastRead > 1000) {   // an I2C read, once per second
    lastRead = millis() | 1;
    external = M5.Power.getVBUSVoltage() > 4000;        // mV, USB gives about 5000
  }
  return external;
}

void prepareSpeaker(uint32_t sampleRate) {
  auto cfg = M5.Speaker.config();
  cfg.sample_rate = sampleRate;
  cfg.stereo = false;
  // M5Unified scales the output by magnification x volume^2; 16 is full level
  // (the Cardputer ADV uses 16), the StickS3 default is 1 = 24 dB quieter.
  cfg.magnification = 16;
  M5.Speaker.config(cfg);
}
const char *footerText() { return "2x A: menu   hold A: back"; }
const char *launcherFooterText() { return "B: next   A: open   2x A: help"; }

int controlsHelp(const ui::HelpItem *&items) {
  static const ui::HelpItem HELP[] = {
      {"A", "main action   2x: menu"},
      {"B", "next   hold: previous"},
      {"hold A", "back to the apps"},
  };
  items = HELP;
  return sizeof(HELP) / sizeof(HELP[0]);
}

#else

// ---------- Cardputer ADV: keyboard ----------

void begin() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);                      // true = enable the keyboard too
  if (M5.isCardenza() && !M5.cardenzaCodecReady()) {
    M5.Display.fillScreen(BLACK);
    M5.Display.drawString("Audio initialization failed", 6, 55);
    for (;;) delay(1000);
  }
}

// The library reports a change whenever the NUMBER of held keys changes and
// then lists all keys held down. In fast typing the keys overlap (the next one
// is pressed before the previous one is released), so only the keys that were
// not held before are passed on; otherwise "ri" would come out as "rri".
void update(bool typing) {
  M5Cardputer.update();
  if (!M5Cardputer.Keyboard.isChange()) return;
  auto state = M5Cardputer.Keyboard.keysState();
  static std::string held;              // keys held at the last change
  static bool heldEnter = false, heldDel = false;

  std::string before = held, fresh;
  for (char ch : state.word) {
    size_t p = before.find(ch);
    if (p != std::string::npos) before.erase(p, 1);
    else fresh += ch;
  }
  Key key;
  key.enter = state.enter && !heldEnter;
  key.del = state.del && !heldDel;
  held.assign(state.word.begin(), state.word.end());
  heldEnter = state.enter;
  heldDel = state.del;
  if (fresh.empty() && !key.enter && !key.del) return;   // a key was released

  if (fresh.empty()) {
    push(key);
    return;
  }
  for (char ch : fresh) {
    key.ch = ch;
    if (!typing && ch == '`') push(key, Special::Back);         // Esc
    else if (!typing && ch == 'h') push(key, Special::Help);
    else push(key);
    key.enter = key.del = false;                          // Enter / Del only once
  }
}

bool hasKeyboard() { return true; }
bool hasSdCard() { return true; }
uint8_t speakerVolume(int volume) { return constrain(volume, 0, 10) * 25; }
void prepareSpeaker(uint32_t) {}
bool loudClicks() { return false; }
uint32_t autoPowerOffMs() { return 0; }
void powerOff() {}
bool onExternalPower() { return false; }
const char *footerText() { return "h: help   Esc: back"; }
const char *launcherFooterText() { return ", / browse   Enter open   h help"; }

int controlsHelp(const ui::HelpItem *&items) {
  items = nullptr;
  return 0;
}

#endif

}  // namespace board
