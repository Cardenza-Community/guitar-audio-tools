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

// Automatic power-off (StickS3): after board::autoPowerOffMs() without a key
// press; a warning is shown for the last WARN_MS, any key cancels it.
// Never while powered from USB: the 5 minutes start again after unplugging.
const uint32_t WARN_MS = 10000;
uint32_t lastActivityMs = 0;

void checkPowerOff() {
  uint32_t limit = board::autoPowerOffMs();
  if (limit == 0) return;
  if ((current && current->keepsAwake()) || board::onExternalPower()) lastActivityMs = millis();
  if (millis() - lastActivityMs < limit) return;
  Serial.println("power off (inactivity)");
  if (current) closeApp();                           // saves the app's settings
  board::powerOff();
}

void drawPowerOffWarning() {
  uint32_t limit = board::autoPowerOffMs();
  uint32_t idle = millis() - lastActivityMs;
  if (limit == 0 || idle + WARN_MS < limit) return;
  auto &c = ui::canvas;
  c.fillRect(10, 40, ui::WIDTH - 20, 50, NAVY);
  c.drawRect(10, 40, ui::WIDTH - 20, 50, YELLOW);
  c.setTextSize(2);
  c.setTextColor(YELLOW);
  char text[24];
  snprintf(text, sizeof(text), "Power off in %u s", (unsigned)((limit - idle + 999) / 1000));
  c.setCursor((ui::WIDTH - c.textWidth(text)) / 2, 48);
  c.print(text);
  c.setTextSize(1);
  c.setTextColor(WHITE);
  c.setCursor((ui::WIDTH - c.textWidth("press a button to stay on")) / 2, 72);
  c.print("press a button to stay on");
}

void handleInput() {
  board::update(current && current->capturesKeys());
  board::Input input;
  while (board::nextInput(input)) {
    uint32_t idle = millis() - lastActivityMs;
    lastActivityMs = millis();
    if (board::autoPowerOffMs() && idle + WARN_MS >= board::autoPowerOffMs())
      continue;                                     // the key only cancelled the power-off
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
  checkPowerOff();

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
    drawPowerOffWarning();
    ui::push();
  }
  delay(1);
}
