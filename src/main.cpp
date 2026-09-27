// Guitar Audio Tools for the M5Stack Cardputer ADV
//
// Shows the app launcher, runs the selected app and feeds it with
// microphone samples, key presses and drawing requests. See ARCHITECTURE.md.
// Launcher keys: , / browse, Enter opens, Esc (top left key) returns to it.

#include <M5Cardputer.h>
#include <string>
#include "app.h"
#include "launcher.h"
#include "version.h"
#include "hw/es8311.h"
#include "services/audio_in.h"
#include "services/settings.h"
#include "services/ui.h"

App *current = nullptr;         // the running app, nullptr = launcher
bool showingHelp = false;       // the h key: help page instead of the app

// ---------- apps ----------

void openApp(App *app) {
  current = app;
  showingHelp = false;
  current->enter();
  if (current->sampleRate() > 0 && !audio_in::start(current->sampleRate(), current->micGain()))
    Serial.println("audio_in::start failed");
}

void closeApp() {
  showingHelp = false;
  audio_in::stop();
  current->exit();
  current = nullptr;
}

// ---------- keyboard ----------

// A key goes to the running app, or to the launcher when no app runs.
void deliver(const Key &key) {
  if (current) {
    current->onKey(key);
  } else if (App *app = launcher::onKey(key)) {
    openApp(app);
  }
}

// The library reports a change whenever the NUMBER of held keys changes and
// then lists all keys held down. In fast typing the keys overlap (the next one
// is pressed before the previous one is released), so only the keys that were
// not held before are passed on; otherwise "ri" would come out as "rri".
void handleKeys() {
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

  // while the help page is shown, any key closes it (Esc too)
  if (showingHelp) {
    showingHelp = false;
    return;
  }

  if (fresh.empty()) {
    deliver(key);
    return;
  }
  for (char ch : fresh) {
    key.ch = ch;
    if (current && current->capturesKeys()) {
      deliver(key);               // typing: every key goes to the app
    } else if (ch == '`') {       // Esc
      if (current) closeApp();
    } else if (ch == 'h') {       // help page
      showingHelp = true;
    } else {
      deliver(key);
    }
    key.enter = key.del = false;  // Enter / Del only once
  }
}

// ---------- main program ----------

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);       // true = enable the keyboard too
  Serial.begin(115200);
  Serial.println("Guitar Audio Tools " FIRMWARE_VERSION);
  es8311::installQuietMicCallback();  // no pop in the speaker when an app closes
  ui::begin();
  settings::begin();
  launcher::begin();
}

void loop() {
  M5Cardputer.update();
  handleKeys();

  if (current) current->tick();

  // hand all waiting microphone samples to the running app
  if (current) {
    static int16_t chunk[256];
    size_t count;
    while ((count = audio_in::read(chunk, 256)) > 0) current->process(chunk, count);
  }

  // redraw about 30 times per second
  static uint32_t lastDraw = 0;
  if (millis() - lastDraw >= 33) {
    lastDraw = millis();
    ui::clear();
    if (showingHelp) {
      const ui::HelpItem *items = nullptr;
      int count = current ? current->help(items) : launcher::help(items);
      ui::helpPage(current ? current->name() : "Guitar Audio Tools", items, count);
    } else if (current) {
      current->draw(ui::canvas);
      ui::drawFlash();
    } else {
      launcher::draw();
    }
    ui::push();
  }
  delay(1);
}
