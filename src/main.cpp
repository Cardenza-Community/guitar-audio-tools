// Audiotools for the M5Stack Cardputer ADV
//
// Shows the app launcher, runs the selected app and feeds it with
// microphone samples, key presses and drawing requests. See ARCHITECTURE.md.
// Launcher keys: , / browse, Enter opens, Esc (top left key) returns to it.

#include <M5Cardputer.h>
#include "app.h"
#include "launcher.h"
#include "services/audio_in.h"
#include "services/settings.h"
#include "services/ui.h"

App *current = nullptr;         // the running app, nullptr = launcher

// ---------- apps ----------

void openApp(App *app) {
  current = app;
  current->enter();
  if (!audio_in::start(current->sampleRate(), current->micGain()))
    Serial.println("audio_in::start failed");
}

void closeApp() {
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

void handleKeys() {
  if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return;
  auto state = M5Cardputer.Keyboard.keysState();

  Key key;
  key.enter = state.enter;
  key.del = state.del;
  if (state.word.empty()) {
    deliver(key);
    return;
  }
  for (char ch : state.word) {
    key.ch = ch;
    if (ch == '`') {              // Esc
      if (current) closeApp();
    } else {
      deliver(key);
    }
  }
}

// ---------- main program ----------

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);       // true = enable the keyboard too
  Serial.begin(115200);
  ui::begin();
  settings::begin();
  launcher::begin();
}

void loop() {
  M5Cardputer.update();
  handleKeys();

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
    if (current) current->draw(ui::canvas);
    else launcher::draw();
    ui::push();
  }
  delay(1);
}
