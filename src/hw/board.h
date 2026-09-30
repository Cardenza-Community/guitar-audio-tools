// The device the firmware runs on: M5Stack Cardputer ADV (default) or
// M5StickS3 (built with -DBOARD_STICKS3, `pio run -e sticks3`).
// Everything else - apps, DSP, audio input, drawing - is the same on both:
// the same ES8311 codec and microphone, the same ESP32-S3, a 240 x 135 display.
//
// Input is turned into the keys the apps know (struct Key):
//   Cardputer: the keyboard; ` (Esc) = back, h = help.
//   StickS3:   A (front button)  click = Enter, double click = action menu,
//                                hold = back to the launcher
//              B (right button)  click = "/" (next), hold = "," (previous)
#pragma once
#include "../app.h"

namespace board {

enum class Special {
  None,     // an ordinary key for the app or the launcher
  Back,     // back to the launcher
  Help,     // show the help page
  Menu,     // the Stick's action menu
};

struct Input {
  Key key;
  Special special = Special::None;
};

void begin();
// Reads the keyboard or the buttons; call once per loop pass.
// `typing`: the app takes every key (Cardputer: h and Esc go to it too).
void update(bool typing);
// The inputs found by the last update(), one by one; false = no more.
bool nextInput(Input &input);

bool hasKeyboard();
bool hasSdCard();
// Speaker volume (0..255 for M5.Speaker) for the apps' 0..10 scale.
// M5Stack recommends below 75 % on the StickS3 (a louder speaker on battery
// can reset it); the metronome clicks at full volume did not reset it in a
// test on battery (2026-09-30), so 10 is the full volume on both boards.
uint8_t speakerVolume(int volume);
// Sets the speaker up for sounds of `sampleRate` Hz, mono; call before
// M5.Speaker.begin(). On the StickS3 M5Unified uses 22050 Hz stereo: sounds of
// another rate are converted and a metronome click there was sometimes played
// twice (28-32 ms apart, measured) and ±30 ms late. The Cardputer (48 kHz mono)
// is left as it is.
void prepareSpeaker(uint32_t sampleRate);
// The small StickS3 speaker at 75 % is quiet: its metronome clicks ring longer
// (more loudness at the same peak current) and all beats are louder.
bool loudClicks();

// Power off after this long without a key press (0 = never): the StickS3
// runs on a 250 mAh battery; the Cardputer has no automatic power-off.
uint32_t autoPowerOffMs();
void powerOff();

// the bottom line of the apps and of the launcher
const char *footerText();
const char *launcherFooterText();
// the lines of the help page that explain the controls (StickS3 only: the
// apps' own help lists Cardputer keys, so only their explanations are shown)
int controlsHelp(const ui::HelpItem *&items);

}  // namespace board
