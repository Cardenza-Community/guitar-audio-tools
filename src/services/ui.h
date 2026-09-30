// Screen helpers shared by all apps.
// Everything is drawn into an off-screen canvas and pushed to the display at
// once, so the picture does not flicker.
#pragma once
#include <M5Unified.h>

namespace ui {

const int WIDTH = 240;
const int HEIGHT = 135;
const int HEADER_HEIGHT = 12;    // top bar with the title
const int FOOTER_Y = 126;        // bottom line with key help

extern M5Canvas canvas;

// One line of an app's help page: the key(s) and what they do.
// keys == nullptr: an explanation line without a key (light blue, full width);
// keys == "": the continuation of the line above.
struct HelpItem {
  const char *keys;
  const char *text;
};

void begin();
void clear();
void header(const char *title, const char *right = nullptr);
void footer(const char *help);
// the standard bottom line in yellow: "h: help   Esc: back" (Cardputer),
// "2x A: menu   hold A: back" (StickS3)
void footerHelp();
// full-screen help page (bigger, clearer font), shown while h is active;
// up to 7 lines of about 34 characters
void helpPage(const char *title, const HelpItem *items, int count);
// the StickS3 action menu: a list of labels, the selected one highlighted
void menuPage(const char *title, const char *const *labels, int count, int selected);
// big "MIC GAIN 27 dB" box with a 0...30 dB bar for 1.5 s (after ; or .);
// main draws it over the app with drawFlash()
void flashGain(int db);
// the same for any level, e.g. flashLevel("VOLUME", 7, 10, "")
void flashLevel(const char *label, int value, int max, const char *unit);
void drawFlash();
// the reference pitch in big orange letters at the top right when it is not
// 440 Hz: an accidental , or / in the tuner must not go unnoticed
void drawA4(float a4);
void push();                     // copy the canvas to the display

}  // namespace ui
