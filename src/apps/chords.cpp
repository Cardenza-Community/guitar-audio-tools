// Chords: a chord dictionary. Type a chord name (am7, f#m, bb, c9) and its
// shape is drawn on a horizontal fretboard (high E string at the top, as the
// guitar is seen when playing), with the fingers and the notes of the chord.
//
// Typing: letters are added to the name while they can continue it (slowly
// too: "c" "#" "a" "d" "d" "9" = C#add9). A note letter (a...g) that cannot
// continue it starts a new chord: after "am7", "d" shows D. Enter clears the
// name - needed only when a letter could continue it: C, then A ("ca" can be
// the start of "cadd9"), or A, then B ("ab" is A flat).
// Keys: , / other shape of the chord, Del deletes the last letter.
// No timing rule: looking for shift + 3 (#) easily takes a few seconds.
// The microphone is not used.
#include <cstring>
#include "apps.h"
#include "theory.h"
#include "tuning.h"
#include "../services/settings.h"
#include "../services/ui.h"

namespace {

const int MAX_TEXT = 8;
const int MAX_SHAPES = 6;

// the fretboard
const int NUT_X = 24;
const int FRET_WIDTH = 42;
const int FRETS_SHOWN = 5;
const int STRING_TOP = 48;         // the high E string
const int STRING_GAP = 13;
const char *STRING_LABELS = "EADGBe";

bool isNoteLetter(char ch) { return ch >= 'a' && ch <= 'g'; }

class ChordsApp : public App {
 public:
  const char *name() const override { return "Chords"; }
  uint32_t sampleRate() const override { return 0; }   // no microphone

  void enter() override {
    // the last chord: letter * 10000 + pitch * 100 + type
    int saved = settings::getInt("ch_last", 5 * 10000 + 9 * 100 + 4);   // Am7
    dsp::Chord chord = {{saved / 10000 % 7, saved / 100 % 100 % 12}, saved % 100 % dsp::CHORD_TYPE_COUNT};
    dsp::chordName(chord, text_);
    update();
    fresh_ = true;                   // the first letter starts a new chord
  }

  void exit() override {
    if (valid_) settings::putInt("ch_last", chord_.root.letter * 10000 + chord_.root.pitch * 100 + chord_.type);
  }

  void process(const int16_t *, size_t) override {}

  int help(const ui::HelpItem *&items) const override {
    static const ui::HelpItem HELP[] = {
        {nullptr, "type a chord: am7  f#m  bb  c9"},
        {nullptr, "types: m 7 maj7 m7 sus2 sus4"},
        {nullptr, "       dim aug 6 9 add9 5"},
        {", /", "other shape of the chord"},
        {"Del", "delete the last letter"},
        {"Enter", "clear, type a new chord"},
        {nullptr, "orange = root, number = finger"},
    };
    items = HELP;
    return sizeof(HELP) / sizeof(HELP[0]);
  }

  void onKey(const Key &key) override {
    if (key.enter) {
      text_[0] = 0;
      update();
    }
    if (key.del) {
      size_t length = strlen(text_);
      if (length > 0) text_[length - 1] = 0;
      update();
    }
    if (key.ch == ',' && shapeCount_ > 0) shape_ = (shape_ + shapeCount_ - 1) % shapeCount_;
    if (key.ch == '/' && shapeCount_ > 0) shape_ = (shape_ + 1) % shapeCount_;
    if (key.ch && key.ch != ',' && key.ch != '/') type(key.ch);
  }

  void draw(M5Canvas &c) override {
    char position[8] = "";
    if (valid_ && shapeCount_ > 1) snprintf(position, sizeof(position), "%d/%d", shape_ + 1, shapeCount_);
    ui::header("Chords", position);

    // the name: as typed while it is not complete
    c.setTextSize(3);
    c.setCursor(4, 16);
    if (valid_) {
      char name[12];
      dsp::chordName(chord_, name);
      c.setTextColor(WHITE);
      c.print(name);
    } else {
      c.setTextColor(invalid_ ? RED : YELLOW);
      c.print(pretty_);
      c.print("_");
      c.setTextSize(1);
      c.setTextColor(WHITE);
      c.setCursor(4, 44);
      c.print(invalid_ ? "unknown chord - Del to fix" : text_[0] ? "Enter: start again" : "type the chord name...");
    }
    if (!valid_) {
      ui::footerHelp();
      return;
    }

    // its notes, next to the name
    char name[12];
    dsp::chordName(chord_, name);
    int x = 4 + c.textWidth(name) + 10;
    c.setTextSize(2);
    dsp::ChordTone tones[6];
    int count = dsp::chordTones(chord_, tones, 6);
    for (int i = 0; i < count; i++) {
      char note[6];
      dsp::noteName(tones[i].note, note);
      c.setTextColor(i == 0 ? ORANGE : GREEN);
      c.setCursor(x, 20);
      c.print(note);
      x += c.textWidth(note) + 6;   // C#add9: C# E# G# D# just fits
    }

    if (shapeCount_ > 0) drawShape(c, shapes_[shape_]);
    ui::footerHelp();
  }

 private:
  // adds a typed character to the name, or starts a new name with it
  void type(char ch) {
    bool fresh = fresh_;
    fresh_ = false;
    size_t length = strlen(text_);
    char longer[MAX_TEXT + 2];
    snprintf(longer, sizeof(longer), "%s%c", text_, ch);
    dsp::Chord unused;
    bool continues = length < (size_t)MAX_TEXT &&
                     dsp::parseChord(longer, unused) != dsp::ParseResult::Invalid;
    if (isNoteLetter(ch) && (fresh || !continues)) {
      text_[0] = ch;                 // a new chord
      text_[1] = 0;
    } else if (continues) {
      strcpy(text_, longer);
    } else {
      return;                        // a character that does not fit: ignored
    }
    update();
  }

  void update() {
    dsp::Chord chord;
    dsp::ParseResult result = dsp::parseChord(text_, chord);
    valid_ = result == dsp::ParseResult::Chord;
    invalid_ = result == dsp::ParseResult::Invalid;
    strcpy(pretty_, text_);
    if (pretty_[0]) pretty_[0] = (char)toupper(pretty_[0]);
    if (!valid_) return;
    bool changed = chord.root.pitch != chord_.root.pitch || chord.type != chord_.type;
    chord_ = chord;
    shapeCount_ = dsp::chordVoicings(chord_, shapes_, MAX_SHAPES);
    if (changed || shape_ >= shapeCount_) shape_ = 0;
  }

  static int stringY(int s) { return STRING_TOP + (5 - s) * STRING_GAP; }

  bool isRoot(int s, int fret) const {
    return (dsp::GUITAR_MIDI[s] + fret) % 12 == chord_.root.pitch;
  }

  void drawShape(M5Canvas &c, const dsp::Voicing &v) {
    // which frets to show: from the nut, or from the lowest fret used
    int lowest = 99, highest = 0;
    for (int s = 0; s < 6; s++) {
      if (v.fret[s] > 0) {
        lowest = std::min(lowest, (int)v.fret[s]);
        highest = std::max(highest, (int)v.fret[s]);
      }
    }
    int first = highest <= FRETS_SHOWN ? 1 : lowest;
    int right = NUT_X + FRETS_SHOWN * FRET_WIDTH;

    // strings, frets, the nut, fret numbers
    c.setTextSize(1);
    for (int s = 0; s < 6; s++) {
      int y = stringY(s);
      c.drawFastHLine(NUT_X, y, right - NUT_X, LIGHTGREY);
      c.setTextColor(YELLOW);
      c.setCursor(1, y - 3);
      c.print(STRING_LABELS[s]);
    }
    for (int f = 0; f <= FRETS_SHOWN; f++)
      c.drawFastVLine(NUT_X + f * FRET_WIDTH, stringY(5), stringY(0) - stringY(5) + 1, LIGHTGREY);
    if (first == 1) c.fillRect(NUT_X - 2, stringY(5), 3, stringY(0) - stringY(5) + 1, WHITE);
    c.setTextColor(WHITE);
    for (int f = 0; f < FRETS_SHOWN; f++) {
      char number[4];
      snprintf(number, sizeof(number), "%d", first + f);
      c.setCursor(NUT_X + f * FRET_WIDTH + FRET_WIDTH / 2 - c.textWidth(number) / 2, stringY(0) + 6);
      c.print(number);
    }

    // barre: the index finger over several strings at one fret
    int barreFret = 0, barreLow = 6, barreHigh = -1;
    for (int s = 0; s < 6; s++) {
      if (v.finger[s] == 1 && v.fret[s] > 0) {
        barreFret = v.fret[s];
        barreLow = std::min(barreLow, s);
        barreHigh = std::max(barreHigh, s);
      }
    }
    if (barreHigh > barreLow) {
      int x = fretX(barreFret, first);
      c.fillRoundRect(x - 5, stringY(barreHigh) - 5, 11, stringY(barreLow) - stringY(barreHigh) + 11, 5, GREEN);
    }

    // muted / open strings and the fingers
    for (int s = 0; s < 6; s++) {
      int y = stringY(s);
      int fret = v.fret[s];
      if (fret == dsp::MUTED) {
        c.setTextColor(RED);
        c.setCursor(12, y - 3);
        c.print("x");
      } else if (fret == 0) {
        c.drawCircle(15, y, 4, isRoot(s, 0) ? ORANGE : GREEN);
      } else {
        int x = fretX(fret, first);
        c.fillCircle(x, y, 6, isRoot(s, fret) ? ORANGE : GREEN);
        c.setTextColor(BLACK);
        c.setCursor(x - 2, y - 3);
        c.print((int)v.finger[s]);
      }
    }
  }

  static int fretX(int fret, int first) { return NUT_X + (fret - first) * FRET_WIDTH + FRET_WIDTH / 2; }

  char text_[MAX_TEXT + 1] = "";
  char pretty_[MAX_TEXT + 1] = "";
  bool valid_ = false, invalid_ = false;
  dsp::Chord chord_ = {{0, 0}, 0};
  dsp::Voicing shapes_[MAX_SHAPES];
  int shapeCount_ = 0, shape_ = 0;
  bool fresh_ = false;
};

ChordsApp instance;

}  // namespace

App *chordsApp() { return &instance; }
