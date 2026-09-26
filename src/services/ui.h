// Screen helpers shared by all apps.
// Everything is drawn into an off-screen canvas and pushed to the display at
// once, so the picture does not flicker.
#pragma once
#include <M5Cardputer.h>

namespace ui {

const int WIDTH = 240;
const int HEIGHT = 135;
const int HEADER_HEIGHT = 12;    // top bar with the title
const int FOOTER_Y = 126;        // bottom line with key help

extern M5Canvas canvas;

void begin();
void clear();
void header(const char *title, const char *right = nullptr);
void footer(const char *help);
void push();                     // copy the canvas to the display

}  // namespace ui
