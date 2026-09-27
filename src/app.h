// Interface that every Audiotools app implements.
// main.cpp shows the menu, starts the selected app and feeds it with audio,
// key presses and drawing requests. See ARCHITECTURE.md.
#pragma once
#include <M5Cardputer.h>
#include "services/ui.h"

// One key press. Handled by main, not passed to apps: Esc (top left, "`")
// goes back to the launcher, h shows the app's help page.
struct Key {
  char ch = 0;          // printable character, 0 if none
  bool enter = false;
  bool del = false;     // backspace
};

class App {
 public:
  virtual ~App() = default;

  virtual const char *name() const = 0;

  // Microphone set-up used while the app runs.
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

  // Help page shown on the h key: returns the number of lines.
  virtual int help(const ui::HelpItem *&items) const {
    items = nullptr;
    return 0;
  }
};
