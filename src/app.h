// Interface that every Audiotools app implements.
// main.cpp shows the menu, starts the selected app and feeds it with audio,
// key presses and drawing requests. See ARCHITECTURE.md.
#pragma once
#include <M5Cardputer.h>

// One key press. The Esc key (top left, "`") is handled by main: back to menu.
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
};
