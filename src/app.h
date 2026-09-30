// Interface that every Guitar Audio Tools app implements.
// main.cpp shows the menu, starts the selected app and feeds it with audio,
// key presses and drawing requests. See ARCHITECTURE.md.
#pragma once
#include <M5Unified.h>
#include "services/ui.h"

// One key press. Handled by main, not passed to apps: Esc (top left, "`")
// goes back to the launcher, h shows the app's help page.
// On the M5StickS3 (two buttons) the buttons are turned into the same keys
// (see hw/board.h); `command` carries app-specific actions chosen in the
// Stick's action menu (e.g. "next root" in Chords).
struct Key {
  char ch = 0;          // printable character, 0 if none
  bool enter = false;
  bool del = false;     // backspace
  int command = 0;      // app-specific action from the Stick menu, 0 = none
};

// One entry of the Stick's action menu: a label and the key it sends.
struct Action {
  const char *label;
  Key key;
};

class App {
 public:
  virtual ~App() = default;

  virtual const char *name() const = 0;

  // Microphone set-up used while the app runs; sample rate 0 = the app does not
  // use the microphone (e.g. it plays sound: the speaker shares the I2S bus).
  virtual uint32_t sampleRate() const { return 16000; }
  virtual int micGain() const { return 24; }        // dB, 0..30

  // Opened / closed: allocate big buffers in enter(), free them in exit().
  virtual void enter() {}
  virtual void exit() {}

  // New audio, called with small chunks of a continuous stream.
  virtual void process(const int16_t *samples, size_t count) = 0;

  // Draw the whole screen into the canvas (about 30 times per second).
  virtual void draw(M5Canvas &canvas) = 0;

  virtual void onKey(const Key &key) {}

  // Called on every pass of the main loop (e.g. to feed the speaker).
  virtual void tick() {}

  // true while the app wants every key, e.g. for typing a name: then h and
  // Esc ("`") are passed to the app too instead of opening help / going back.
  virtual bool capturesKeys() const { return false; }

  // Actions offered in the M5StickS3 menu (double click on A): everything the
  // two buttons do not reach directly. Returns the number of actions.
  virtual int actions(const Action *&items) const {
    items = nullptr;
    return 0;
  }

  // Help page shown on the h key: returns the number of lines.
  virtual int help(const ui::HelpItem *&items) const {
    items = nullptr;
    return 0;
  }
};
