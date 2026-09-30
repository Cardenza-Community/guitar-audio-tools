// Scales: a scale on the whole fretboard (frets 0-12 or 12-24), high E string
// at the top as in the Chords app. The root is orange, the other notes green,
// the "blue note" (b5) of the blues scale light blue. Small dots without note
// names: 13 columns of labelled dots were too busy on the small display.
//
// Keys: a...g the key (# or b right after the letter: sharp / flat; b alone is
// the note B), , / the scale, ; . frets 0-12 / 12-24. No microphone.
// M5StickS3 (no keyboard): A = next key, B = next scale (hold: previous),
// the menu (double click on A) has the previous key and the fret range.
#include <cstring>
#include "apps.h"
#include "theory.h"
#include "../hw/board.h"
#include "tuning.h"
#include "../services/settings.h"
#include "../services/ui.h"

namespace {

const int NUT_X = 24;              // line after the first column (open strings / fret 12)
const int FRET_WIDTH = 18;
const int FRETS_SHOWN = 12;        // after the first column
const int STRING_TOP = 42;         // the high E string
const int STRING_GAP = 14;
const int DOT_RADIUS = 4;
const char *STRING_LABELS = "EADGBe";
const int MARKED_FRETS[] = {3, 5, 7, 9, 12, 15, 17, 19, 21, 24};
const char *LETTERS = "cdefgab";
const int LETTER_PITCH[] = {0, 2, 4, 5, 7, 9, 11};

// Stick menu commands (Key::command)
enum Command { NEXT_ROOT = 1, PREVIOUS_ROOT };
// the 12 keys in their usual spelling, by pitch class: letter (0 = C) and pitch
const dsp::SpelledNote KEYS[] = {{0, 0}, {0, 1}, {1, 2}, {2, 3}, {2, 4}, {3, 5},
                                 {3, 6}, {4, 7}, {5, 8}, {5, 9}, {6, 10}, {6, 11}};

class ScalesApp : public App {
 public:
  const char *name() const override { return "Scales"; }
  uint32_t sampleRate() const override { return 0; }   // no microphone

  void enter() override {
    int root = settings::getInt("sc_root", 5 * 100 + 9);   // letter * 100 + pitch: A
    root_ = {root / 100 % 7, root % 100 % 12};
    type_ = constrain(settings::getInt("sc_type", 0), 0, dsp::SCALE_TYPE_COUNT - 1);
    firstFret_ = 0;
    accidentalAllowed_ = false;
    update();
  }

  void exit() override {
    settings::putInt("sc_root", root_.letter * 100 + root_.pitch);
    settings::putInt("sc_type", type_);
  }

  void process(const int16_t *, size_t) override {}

  // the M5StickS3 action menu (double click on A)
  int actions(const Action *&items) const override {
    static const Action ACTIONS[] = {
        {"Next key", {0, false, false, NEXT_ROOT}},
        {"Previous key", {0, false, false, PREVIOUS_ROOT}},
        {"Frets 0-12", {';'}},
        {"Frets 12-24", {'.'}},
    };
    items = ACTIONS;
    return sizeof(ACTIONS) / sizeof(ACTIONS[0]);
  }

  int help(const ui::HelpItem *&items) const override {
    static const ui::HelpItem HELP[] = {
        {"a...g", "the key (then # or b)"},
        {", /", "previous / next scale"},
        {"; .", "frets 0-12 / 12-24"},
        {nullptr, "orange = root (1)"},
        {nullptr, "blue = blue note (blues)"},
    };
    items = HELP;
    return sizeof(HELP) / sizeof(HELP[0]);
  }

  void onKey(const Key &key) override {
    int command = key.command;
    if (!board::hasKeyboard() && key.enter) command = NEXT_ROOT;   // StickS3: A
    if (command) {
      int pitch = (root_.pitch + (command == NEXT_ROOT ? 1 : 11)) % 12;
      root_ = KEYS[pitch];
      update();
      return;
    }
    char ch = key.ch;
    if ((ch == '#' || ch == 'b') && accidentalAllowed_) {
      root_.pitch = (root_.pitch + (ch == '#' ? 1 : 11)) % 12;
      accidentalAllowed_ = false;
      update();
      return;
    }
    const char *letter = ch ? strchr(LETTERS, ch) : nullptr;
    if (letter) {
      int index = letter - LETTERS;
      // LETTERS starts with C, the spelled-note letters too (0 = C ... 6 = B)
      root_ = {index, LETTER_PITCH[index]};
      accidentalAllowed_ = true;
      update();
      return;
    }
    accidentalAllowed_ = false;
    if (ch == ',') type_ = (type_ + dsp::SCALE_TYPE_COUNT - 1) % dsp::SCALE_TYPE_COUNT;
    if (ch == '/') type_ = (type_ + 1) % dsp::SCALE_TYPE_COUNT;
    if (ch == ';') firstFret_ = 0;
    if (ch == '.') firstFret_ = 12;
    update();
  }

  void draw(M5Canvas &c) override {
    ui::header("Scales", firstFret_ ? "frets 12-24" : "frets 0-12");

    // "A minor pentatonic"
    char root[6];
    dsp::noteName(root_, root);
    c.setTextSize(2);
    c.setTextColor(ORANGE);
    c.setCursor(4, 16);
    c.print(root);
    c.setTextColor(WHITE);
    c.print(" ");
    c.print(dsp::SCALE_TYPES[type_].name);

    drawFretboard(c);
    ui::footerHelp();
  }

 private:
  void update() {
    noteCount_ = dsp::scaleNotes(root_, type_, notes_, 7);
  }

  static int stringY(int s) { return STRING_TOP + (5 - s) * STRING_GAP; }

  // x of the centre of a fret column; column 0 = left of the nut line
  static int columnX(int column) {
    return column == 0 ? NUT_X - 8 : NUT_X + (column - 1) * FRET_WIDTH + FRET_WIDTH / 2;
  }

  void drawFretboard(M5Canvas &c) {
    int right = NUT_X + FRETS_SHOWN * FRET_WIDTH - 1;
    c.setTextSize(1);
    for (int s = 0; s < 6; s++) {
      c.drawFastHLine(NUT_X - 14, stringY(s), right - NUT_X + 15, LIGHTGREY);
      c.setTextColor(YELLOW);
      c.setCursor(1, stringY(s) - 3);
      c.print(STRING_LABELS[s]);
    }
    int top = stringY(5), height = stringY(0) - stringY(5) + 1;
    for (int f = 1; f <= FRETS_SHOWN; f++) c.drawFastVLine(NUT_X + f * FRET_WIDTH - 1, top, height, LIGHTGREY);
    if (firstFret_ == 0) c.fillRect(NUT_X - 2, top, 3, height, WHITE);   // the nut
    else c.drawFastVLine(NUT_X, top, height, LIGHTGREY);

    // numbers of the marked frets (3, 5, 7, 9, 12...) under the strings
    c.setTextColor(WHITE);
    for (int fret : MARKED_FRETS) {
      int column = fret - firstFret_;
      if (column < 0 || column > FRETS_SHOWN) continue;
      char number[4];
      snprintf(number, sizeof(number), "%d", fret);
      c.setCursor(columnX(column) - c.textWidth(number) / 2, stringY(0) + 9);
      c.print(number);
    }

    // the notes of the scale
    for (int s = 0; s < 6; s++) {
      for (int column = 0; column <= FRETS_SHOWN; column++) {
        int pitch = (dsp::GUITAR_MIDI[s] + firstFret_ + column) % 12;
        for (int i = 0; i < noteCount_; i++) {
          if (notes_[i].note.pitch != pitch) continue;
          drawNote(c, columnX(column), stringY(s), notes_[i], i == 0);
          break;
        }
      }
    }
  }

  void drawNote(M5Canvas &c, int x, int y, const dsp::ChordTone &note, bool root) {
    bool blueNote = strncmp(note.degree, "b5", note.degreeLength) == 0 && note.degreeLength == 2;
    c.fillCircle(x, y, DOT_RADIUS, root ? ORANGE : blueNote ? CYAN : GREEN);
  }

  dsp::SpelledNote root_ = {5, 9};
  int type_ = 0, firstFret_ = 0;
  bool accidentalAllowed_ = false;
  dsp::ChordTone notes_[7];
  int noteCount_ = 0;
};

ScalesApp instance;

}  // namespace

App *scalesApp() { return &instance; }
