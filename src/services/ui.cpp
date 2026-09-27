#include "ui.h"

namespace ui {

M5Canvas canvas;

void begin() {
  M5Cardputer.Display.setRotation(1);     // landscape
  canvas.createSprite(WIDTH, HEIGHT);
}

void clear() { canvas.fillSprite(BLACK); }

void header(const char *title, const char *right) {
  canvas.fillRect(0, 0, WIDTH, HEADER_HEIGHT - 1, NAVY);
  canvas.setTextSize(1);
  canvas.setTextColor(WHITE);
  canvas.setCursor(3, 2);
  canvas.print(title);
  if (right) {
    canvas.setCursor(WIDTH - 3 - canvas.textWidth(right), 2);
    canvas.print(right);
  }
}

void footer(const char *help) {
  canvas.setTextSize(1);
  canvas.setTextColor(YELLOW);
  canvas.setCursor(3, FOOTER_Y);
  canvas.print(help);
}

void footerHelp() {
  canvas.setTextSize(1);
  canvas.setTextColor(YELLOW);
  canvas.setCursor(3, FOOTER_Y);
  canvas.print("h: help   Esc: back");
}

void helpPage(const char *title, const HelpItem *items, int count) {
  canvas.fillSprite(BLACK);
  header(title, "HELP");
  canvas.setFont(&fonts::DejaVu12);
  canvas.setTextSize(1);
  const int lineHeight = 15, keyWidth = 58;
  for (int i = 0; i < count; i++) {
    int y = HEADER_HEIGHT + 3 + i * lineHeight;
    if (!items[i].keys) {                     // explanation, not a key
      canvas.setTextColor(CYAN);
      canvas.setCursor(4, y);
      canvas.print(items[i].text);
      continue;
    }
    canvas.setTextColor(YELLOW);
    canvas.setCursor(4, y);
    canvas.print(items[i].keys);
    canvas.setTextColor(WHITE);
    canvas.setCursor(keyWidth, y);
    canvas.print(items[i].text);
  }
  canvas.setTextColor(WHITE);
  canvas.setCursor(4, HEIGHT - 13);
  canvas.print("any key: close help");
  canvas.setFont(&fonts::Font0);             // back to the default font
}

static int flashDb = 0;
static uint32_t flashUntil = 0;

void flashGain(int db) {
  flashDb = db;
  flashUntil = millis() + 1500;
}

void drawFlash() {
  if ((int32_t)(flashUntil - millis()) <= 0) return;
  const int w = 180, h = 44, x = (WIDTH - w) / 2, y = (HEIGHT - h) / 2;
  canvas.fillRoundRect(x, y, w, h, 6, NAVY);
  canvas.drawRoundRect(x, y, w, h, 6, YELLOW);
  char text[24];
  snprintf(text, sizeof(text), "MIC GAIN %d dB", flashDb);
  canvas.setTextSize(2);
  canvas.setTextColor(WHITE);
  canvas.setCursor(x + (w - canvas.textWidth(text)) / 2, y + 6);
  canvas.print(text);
  // bar 0 ... 30 dB
  int barW = w - 20;
  canvas.drawRect(x + 10, y + 28, barW, 9, WHITE);
  canvas.fillRect(x + 11, y + 29, (barW - 2) * flashDb / 30, 7, YELLOW);
  canvas.setTextSize(1);
}

void push() { canvas.pushSprite(&M5Cardputer.Display, 0, 0); }

}  // namespace ui
