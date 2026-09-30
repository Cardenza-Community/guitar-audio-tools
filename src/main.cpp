// Guitar Audio Tools for the M5Stack Cardputer ADV
//
// Shows the app launcher, runs the selected app and feeds it with
// microphone samples, key presses and drawing requests. See ARCHITECTURE.md.
// Launcher keys: , / browse, Enter opens, Esc (top left key) returns to it.
// Runs on the Cardputer ADV and on the M5StickS3 (see hw/board.h).

#include <M5Unified.h>
#include "app.h"
#include "launcher.h"
#include "version.h"
#include "hw/board.h"
#include "hw/es8311.h"
#include "services/audio_in.h"
#include "services/settings.h"
#include "services/ui.h"

App *current = nullptr;         // the running app, nullptr = launcher
bool showingHelp = false;       // the h key: help page instead of the app
void drawHelp();
void drawMenu();
extern bool menuOpen;

// ---------- apps ----------

void openApp(App *app) {
  current = app;
  showingHelp = false;
  menuOpen = false;
  current->enter();
  if (current->sampleRate() > 0 && !audio_in::start(current->sampleRate(), current->micGain()))
    Serial.println("audio_in::start failed");
}

void closeApp() {
  showingHelp = false;
  menuOpen = false;
  audio_in::stop();
  current->exit();
  current = nullptr;
}

// ---------- input ----------

// A key goes to the running app, or to the launcher when no app runs.
void deliver(const Key &key) {
  if (current) {
    current->onKey(key);
  } else if (App *app = launcher::onKey(key)) {
    openApp(app);
  }
}

// The StickS3 action menu (double click on A): the app's actions, then Help.
bool menuOpen = false;
int menuIndex = 0;

int menuItems(const Action *&actions) { return current ? current->actions(actions) : 0; }

void menuInput(const board::Input &input) {
  const Action *actions = nullptr;
  int count = menuItems(actions) + 1;               // + Help
  if (input.special == board::Special::Back || input.special == board::Special::Menu) {
    menuOpen = false;
  } else if (input.key.ch == '/') {
    menuIndex = (menuIndex + 1) % count;
  } else if (input.key.ch == ',') {
    menuIndex = (menuIndex + count - 1) % count;
  } else if (input.key.enter) {
    menuOpen = false;
    if (menuIndex == count - 1) showingHelp = true;
    else deliver(actions[menuIndex].key);
  }
}

void handleInput() {
  board::update(current && current->capturesKeys());
  board::Input input;
  while (board::nextInput(input)) {
    if (showingHelp) {                              // any key closes the help page
      showingHelp = false;
      continue;
    }
    if (menuOpen) {
      menuInput(input);
      continue;
    }
    switch (input.special) {
      case board::Special::Back:
        if (current) closeApp();
        break;
      case board::Special::Help:
        showingHelp = true;
        break;
      case board::Special::Menu:
        if (current) {
          menuOpen = true;
          menuIndex = 0;
        } else {
          showingHelp = true;                       // the launcher has no actions
        }
        break;
      default:
        deliver(input.key);
    }
  }
}

// The help page. On the StickS3 the controls come first, followed by the
// app's explanations only (its key lines are Cardputer keys).
void drawHelp() {
  const ui::HelpItem *items = nullptr;
  int count = current ? current->help(items) : launcher::help(items);
  const char *title = current ? current->name() : "Guitar Audio Tools";
  const ui::HelpItem *controls = nullptr;
  int controlCount = board::controlsHelp(controls);
  if (controlCount == 0) {
    ui::helpPage(title, items, count);
    return;
  }
  static ui::HelpItem lines[7];
  int n = 0;
  for (int i = 0; i < controlCount && n < 7; i++) lines[n++] = controls[i];
  for (int i = 0; i < count && n < 7; i++)
    if (!items[i].keys) lines[n++] = items[i];
  ui::helpPage(title, lines, n);
}

void drawMenu() {
  const Action *actions = nullptr;
  int count = menuItems(actions);
  static const char *labels[16];
  int n = 0;
  for (int i = 0; i < count && n < 15; i++) labels[n++] = actions[i].label;
  labels[n++] = "Help";
  ui::menuPage(current ? current->name() : "", labels, n, menuIndex);
}

// ---------- main program ----------

void setup() {
  board::begin();
  Serial.begin(115200);
  Serial.println("Guitar Audio Tools " FIRMWARE_VERSION);
  es8311::installQuietMicCallback();  // no pop in the speaker when an app closes
  ui::begin();
  settings::begin();
  launcher::begin();
}

void loop() {
  handleInput();

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
      drawHelp();
    } else if (menuOpen) {
      drawMenu();
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
