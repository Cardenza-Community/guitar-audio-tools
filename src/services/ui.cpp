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

void push() { canvas.pushSprite(&M5Cardputer.Display, 0, 0); }

}  // namespace ui
