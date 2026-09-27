#include "launcher.h"
#include "apps/apps.h"
#include "services/settings.h"
#include "services/ui.h"

namespace launcher {

// Icons are drawn with simple shapes around the centre point (x, y),
// about 64 x 56 pixels.
typedef void (*IconFn)(M5Canvas &c, int x, int y);

static void iconDecibel(M5Canvas &c, int x, int y) {
  y += 14;
  // scale: green, yellow, red part of a half circle (0 degrees = right, clockwise)
  c.fillArc(x, y, 26, 31, 180, 265, GREEN);
  c.fillArc(x, y, 26, 31, 270, 315, YELLOW);
  c.fillArc(x, y, 26, 31, 320, 360, RED);
  c.drawWideLine(x, y, x + 15, y - 20, 1.5f, WHITE);    // needle
  c.fillCircle(x, y, 4, WHITE);
  c.setTextSize(2);
  c.setTextColor(WHITE);
  c.setCursor(x - 11, y - 1 + 6);
  c.print("dB");
}

static void iconTuner(M5Canvas &c, int x, int y) {
  // big note name above a cents scale with a green centre
  c.setTextSize(4);
  c.setTextColor(WHITE);
  c.setCursor(x - 11, y - 26);
  c.print("A");
  int sy = y + 16;
  c.drawFastHLine(x - 30, sy, 61, DARKGREY);
  for (int i = -3; i <= 3; i++) c.drawFastVLine(x + i * 10, sy - 4, 9, i == 0 ? GREEN : DARKGREY);
  c.fillTriangle(x - 5, sy + 12, x + 5, sy + 12, x, sy + 5, GREEN);
}

static void iconStrumTuner(M5Canvas &c, int x, int y) {
  // six strings, each with its own tuning marker
  const int offset[] = {-8, 3, 0, 12, -3, 0};
  c.drawFastHLine(x - 30, y, 61, GREEN);
  for (int i = 0; i < 6; i++) {
    int sx = x - 25 + i * 10;
    c.drawFastVLine(sx, y - 26, 53, DARKGREY);
    c.fillRect(sx - 3, y + offset[i] - 2, 7, 5, offset[i] == 0 ? GREEN : ORANGE);
  }
}

static void iconSpectrum(M5Canvas &c, int x, int y) {
  // bars made of blocks: green at the bottom, yellow, red at the top
  const int heights[] = {4, 7, 9, 6, 8, 5, 3};
  for (int b = 0; b < 7; b++) {
    for (int k = 0; k < heights[b]; k++) {
      uint16_t color = k >= 8 ? RED : (k >= 6 ? YELLOW : GREEN);
      c.fillRect(x - 31 + b * 9, y + 24 - k * 6, 7, 4, color);
    }
  }
}

static void iconBpm(M5Canvas &c, int x, int y) {
  // "BPM" above a row of beats
  c.setTextSize(3);
  c.setTextColor(WHITE);
  c.setCursor(x - 26, y - 22);
  c.print("BPM");
  for (int i = 0; i < 5; i++) {
    int bx = x - 28 + i * 14;
    c.fillRect(bx, y + 16 - (i % 4 == 0 ? 12 : 6), 6, i % 4 == 0 ? 12 : 6, i % 4 == 0 ? ORANGE : CYAN);
  }
}

static void iconMetronome(M5Canvas &c, int x, int y) {
  // metronome
  c.drawTriangle(x - 18, y + 26, x + 18, y + 26, x - 6, y - 26, WHITE);
  c.drawTriangle(x - 18, y + 26, x + 18, y + 26, x + 6, y - 26, WHITE);
  c.drawWideLine(x, y + 18, x + 16, y - 20, 1.2f, ORANGE);   // pendulum
  c.fillCircle(x + 11, y - 8, 4, ORANGE);
}

static void iconIntonation(M5Canvas &c, int x, int y) {
  // a guitar neck with frets and the double dot of the 12th fret
  c.fillRect(x - 32, y - 12, 64, 24, 0x7A00);                    // brown fretboard
  for (int f = 0; f < 6; f++) c.drawFastVLine(x - 30 + f * 12, y - 12, 24, LIGHTGREY);
  for (int s = 0; s < 4; s++) c.drawFastHLine(x - 32, y - 9 + s * 6, 64, WHITE);
  c.fillCircle(x + 12, y - 6, 3, WHITE);
  c.fillCircle(x + 12, y + 6, 3, WHITE);
  c.setTextSize(1);
  c.setTextColor(YELLOW);
  c.setCursor(x + 6, y + 18);
  c.print("12");
}

static void iconVocal(M5Canvas &c, int x, int y) {
  // microphone
  c.fillRoundRect(x - 9, y - 28, 18, 32, 9, WHITE);
  c.drawArc(x, y - 4, 15, 17, 0, 180, WHITE);
  c.fillRect(x - 1, y + 13, 3, 10, WHITE);
  c.fillRect(x - 12, y + 23, 25, 3, WHITE);
}

static void iconRecorder(M5Canvas &c, int x, int y) {
  // record button
  c.drawRoundRect(x - 28, y - 26, 57, 53, 8, WHITE);
  c.fillCircle(x, y - 4, 14, RED);
  c.setTextSize(1);
  c.setTextColor(WHITE);
  c.setCursor(x - 8, y + 15);
  c.print("REC");
}

static void iconMicTest(M5Canvas &c, int x, int y) {
  // waveform
  int prevY = y;
  for (int i = -30; i <= 30; i++) {
    int wy = y - (int)(22 * sinf(i * 0.21f) * (1 - abs(i) / 40.0f));
    if (i > -30) c.drawLine(x + i - 1, prevY, x + i, wy, CYAN);
    prevY = wy;
  }
  c.drawFastHLine(x - 30, y, 61, DARKGREY);
}

struct Entry {
  const char *title;
  const char *description;
  IconFn icon;
  App *app;        // nullptr = not written yet
};

// Adding an app: write it in src/apps/, declare it in apps.h, add a line here.
static Entry entries[] = {
    {"Decibel meter", "sound level in dB", iconDecibel, decibelMeterApp()},
    {"Guitar tuner", "needle tuner, cents", iconTuner, tunerApp()},
    {"Strum Tuner", "all strings at once", iconStrumTuner, strumTunerApp()},
    {"Spectrum", "music analyser bars", iconSpectrum, spectrumApp()},
    {"BPM", "tempo: listen or tap", iconBpm, bpmApp()},
    {"Metronome", "click, accents, tap", iconMetronome, metronomeApp()},
    {"Intonation", "guitar setup, fret 12", iconIntonation, intonationApp()},
    {"Vocal trainer", "sing the target note", iconVocal, nullptr},
    {"Recorder", "WAV on the SD card", iconRecorder, nullptr},
    {"Mic test", "diagnostics", iconMicTest, micTestApp()},
};
static const int COUNT = sizeof(entries) / sizeof(entries[0]);

static int selected = 0;

void begin() {
  selected = constrain(settings::getInt("last_app", 0), 0, COUNT - 1);
}

void draw() {
  auto &c = ui::canvas;
  const Entry &e = entries[selected];

  char position[8];
  snprintf(position, sizeof(position), "%d/%d", selected + 1, COUNT);
  ui::header("Guitar Audio Tools", position);

  e.icon(c, ui::WIDTH / 2, 50);

  // arrows on both sides
  c.fillTriangle(6, 50, 16, 40, 16, 60, DARKGREY);
  c.fillTriangle(ui::WIDTH - 7, 50, ui::WIDTH - 17, 40, ui::WIDTH - 17, 60, DARKGREY);

  c.setTextSize(2);
  c.setTextColor(WHITE);
  c.setCursor((ui::WIDTH - c.textWidth(e.title)) / 2, 88);
  c.print(e.title);

  c.setTextSize(1);
  c.setTextColor(e.app ? WHITE : ORANGE);
  const char *line = e.app ? e.description : "coming soon";
  c.setCursor((ui::WIDTH - c.textWidth(line)) / 2, 108);
  c.print(line);

  c.setTextSize(1);
  c.setTextColor(YELLOW);
  c.setCursor(3, ui::FOOTER_Y);
  c.print(", / browse   Enter open   h help");
}

static const ui::HelpItem HELP[] = {
    {", /", "previous / next app"},
    {"Enter", "open the app"},
    {"Esc", "back here from an app"},
    {"h", "help (in every app)"},
    {nullptr, "the last opened app is shown"},
    {nullptr, "after power-on"},
};

int help(const ui::HelpItem *&items) {
  items = HELP;
  return sizeof(HELP) / sizeof(HELP[0]);
}

App *onKey(const Key &key) {
  if (key.ch == ',') selected = (selected + COUNT - 1) % COUNT;
  if (key.ch == '/') selected = (selected + 1) % COUNT;
  if (key.enter && entries[selected].app) {
    settings::putInt("last_app", selected);
    return entries[selected].app;
  }
  return nullptr;
}

}  // namespace launcher
