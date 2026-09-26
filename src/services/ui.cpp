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
  canvas.setTextColor(DARKGREY);
  canvas.setCursor(3, FOOTER_Y);
  canvas.print(help);
}

void push() { canvas.pushSprite(&M5Cardputer.Display, 0, 0); }

}  // namespace ui
