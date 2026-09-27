// App launcher: a carousel showing one app at a time with a big icon.
// Keys: , / (left/right arrows) browse, Enter opens the app.
// The last opened app is remembered (NVS) and shown after power-on.
#pragma once
#include "app.h"

namespace launcher {

void begin();                 // call after settings::begin()
void draw();

// Handles a key; returns the app to open, or nullptr.
App *onKey(const Key &key);

// Help page of the launcher (the h key); returns the number of lines.
int help(const ui::HelpItem *&items);

}  // namespace launcher
