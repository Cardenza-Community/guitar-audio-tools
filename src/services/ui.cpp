#include "ui.h"
#include "../hw/board.h"

namespace ui {

M5Canvas canvas;

void begin() {
  M5.Display.setRotation(1);     // landscape
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

int battery(int right) {
#ifdef CARDENZA_TARGET
  return right; // Cardenza has no battery ADC or charging circuit.
#else
  // reading every 2 s: the percentage would flicker otherwise (the Cardputer
  // only measures the battery voltage)
  static int level = -1;
  static bool charging = false;
  static uint32_t lastRead = 0;
  if (lastRead == 0 || millis() - lastRead > 2000) {
    lastRead = millis() | 1;
    level = M5.Power.getBatteryLevel();
    charging = M5.Power.isCharging() == m5::Power_Class::is_charging;
  }
  if (level < 0) return right;
  if (level > 100) level = 100;

  char text[6];
  snprintf(text, sizeof(text), "%d%%", level);
  uint16_t color = level > 50 ? GREEN : level >= 20 ? YELLOW : RED;
  canvas.setTextSize(1);
  int x = right - canvas.textWidth(text);
  canvas.setTextColor(color);
  canvas.setCursor(x, 2);
  canvas.print(text);

  // the battery: 16 x 8 body, 2 px tip on the right, filled by the level
  const int w = 16, h = 8, y = 1;
  x -= w + 2 + 3;
  canvas.drawRect(x, y, w, h, WHITE);
  canvas.fillRect(x + w, y + 2, 2, h - 4, WHITE);
  int fill = (w - 4) * level / 100;
  if (fill < 1) fill = 1;
  canvas.fillRect(x + 2, y + 2, fill, h - 4, color);
  if (charging) {                               // lightning bolt over the battery
    canvas.fillTriangle(x + 9, y - 1, x + 5, y + 4, x + 9, y + 4, YELLOW);
    canvas.fillTriangle(x + 7, y + 3, x + 11, y + 3, x + 7, y + 8, YELLOW);
  }
  return x;
#endif
}

void drawA4(float a4) {
  if (fabsf(a4 - 440.0f) < 0.01f) return;
  char text[12];
  snprintf(text, sizeof(text), "A4 %.0f", a4);
  canvas.setTextSize(2);
  int w = canvas.textWidth(text);
  canvas.fillRect(WIDTH - w - 6, HEADER_HEIGHT, w + 6, 18, BLACK);
  canvas.setTextColor(ORANGE);
  canvas.setCursor(WIDTH - w - 3, HEADER_HEIGHT + 2);
  canvas.print(text);
  canvas.setTextSize(1);
}

void footerHelp() {
  canvas.setTextSize(1);
  canvas.setTextColor(YELLOW);
  canvas.setCursor(3, FOOTER_Y);
  canvas.print(board::footerText());
}

void menuPage(const char *title, const char *const *labels, int count, int selected) {
  canvas.fillSprite(BLACK);
  header(title, "MENU");
  canvas.setFont(&fonts::DejaVu12);
  canvas.setTextSize(1);
  const int lineHeight = 15, visible = 7;
  int first = selected < visible ? 0 : selected - visible + 1;   // keep the selection in view
  for (int i = first; i < count && i < first + visible; i++) {
    int y = HEADER_HEIGHT + 3 + (i - first) * lineHeight;
    if (i == selected) {
      canvas.fillRect(0, y - 1, WIDTH, lineHeight, NAVY);
      canvas.setTextColor(YELLOW);
    } else {
      canvas.setTextColor(WHITE);
    }
    canvas.setCursor(8, y);
    canvas.print(labels[i]);
  }
  canvas.setTextColor(YELLOW);
  canvas.setCursor(4, HEIGHT - 13);
  canvas.print("B: next   A: choose   hold A: close");
  canvas.setFont(&fonts::Font0);
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
  canvas.print(board::hasKeyboard() ? "any key: close help" : "any button: close help");
  canvas.setFont(&fonts::Font0);             // back to the default font
}

static int flashValue = 0, flashMax = 30;
static const char *flashLabel = "MIC GAIN";
static const char *flashUnit = "dB";
static uint32_t flashUntil = 0;

void flashLevel(const char *label, int value, int max, const char *unit) {
  flashLabel = label;
  flashValue = value;
  flashMax = max;
  flashUnit = unit;
  flashUntil = millis() + 1500;
}

void flashGain(int db) { flashLevel("MIC GAIN", db, 30, "dB"); }

void drawFlash() {
  if ((int32_t)(flashUntil - millis()) <= 0) return;
  const int w = 180, h = 44, x = (WIDTH - w) / 2, y = (HEIGHT - h) / 2;
  canvas.fillRoundRect(x, y, w, h, 6, NAVY);
  canvas.drawRoundRect(x, y, w, h, 6, YELLOW);
  char text[24];
  snprintf(text, sizeof(text), "%s %d %s", flashLabel, flashValue, flashUnit);
  canvas.setTextSize(2);
  canvas.setTextColor(WHITE);
  canvas.setCursor(x + (w - canvas.textWidth(text)) / 2, y + 6);
  canvas.print(text);
  // bar 0 ... 30 dB
  int barW = w - 20;
  canvas.drawRect(x + 10, y + 28, barW, 9, WHITE);
  canvas.fillRect(x + 11, y + 29, (barW - 2) * flashValue / flashMax, 7, YELLOW);
  canvas.setTextSize(1);
}

void push() { canvas.pushSprite(&M5.Display, 0, 0); }

}  // namespace ui
