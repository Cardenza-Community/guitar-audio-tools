#include "theory.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include "tuning.h"

namespace dsp {

static const char LETTERS[] = "CDEFGAB";
static const int LETTER_PITCH[] = {0, 2, 4, 5, 7, 9, 11};
static const int MAJOR_SCALE[] = {0, 2, 4, 5, 7, 9, 11};   // semitones of degrees 1..7

static int mod12(int x) { return ((x % 12) + 12) % 12; }

void noteName(SpelledNote note, char *out) {
  // how far the pitch is from the plain letter: -2 = bb ... +2 = ##
  int shift = mod12(note.pitch - LETTER_PITCH[note.letter] + 6) - 6;
  int n = 0;
  out[n++] = LETTERS[note.letter];
  for (int i = 0; i < shift && i < 2; i++) out[n++] = '#';
  for (int i = 0; i < -shift && i < 2; i++) out[n++] = 'b';
  out[n] = 0;
}

bool parseDegree(const char *degree, int &semitones, int &letterSteps) {
  int shift = 0;
  while (*degree == 'b' || *degree == '#') shift += *degree++ == '#' ? 1 : -1;
  int number = 0;
  while (*degree >= '0' && *degree <= '9') number = number * 10 + (*degree++ - '0');
  if (number < 1 || number > 13) return false;
  letterSteps = (number - 1) % 7;
  semitones = MAJOR_SCALE[letterSteps] + shift;
  return true;
}

SpelledNote noteAbove(SpelledNote root, const char *degree) {
  int semitones = 0, steps = 0;
  parseDegree(degree, semitones, steps);
  return {(root.letter + steps) % 7, mod12(root.pitch + semitones)};
}

const ChordType CHORD_TYPES[] = {
    {"", "1 3 5"},
    {"m", "1 b3 5"},
    {"7", "1 3 5 b7"},
    {"maj7", "1 3 5 7"},
    {"m7", "1 b3 5 b7"},
    {"sus2", "1 2 5"},
    {"sus4", "1 4 5"},
    {"dim", "1 b3 b5"},
    {"aug", "1 3 #5"},
    {"6", "1 3 5 6"},
    {"9", "1 3 5 b7 9"},
    {"add9", "1 3 5 9"},
    {"5", "1 5"},
};
const int CHORD_TYPE_COUNT = sizeof(CHORD_TYPES) / sizeof(CHORD_TYPES[0]);

ParseResult parseChord(const char *text, Chord &out) {
  if (!text[0]) return ParseResult::Incomplete;
  const char *letter = strchr(LETTERS, toupper((unsigned char)text[0]));
  if (!letter || !*letter) return ParseResult::Invalid;
  SpelledNote root = {(int)(letter - LETTERS), LETTER_PITCH[letter - LETTERS]};
  const char *rest = text + 1;
  // one sharp or flat right after the letter (no chord type starts with b)
  if (*rest == '#' || *rest == 'b' || *rest == 'B') {
    root.pitch = mod12(root.pitch + (*rest == '#' ? 1 : -1));
    rest++;
  }
  char suffix[8];
  size_t length = strlen(rest);
  if (length >= sizeof(suffix)) return ParseResult::Invalid;
  for (size_t i = 0; i <= length; i++) suffix[i] = (char)tolower((unsigned char)rest[i]);

  bool prefix = false;
  for (int t = 0; t < CHORD_TYPE_COUNT; t++) {
    if (strcmp(suffix, CHORD_TYPES[t].suffix) == 0) {
      out = {root, t};
      return ParseResult::Chord;
    }
    if (strncmp(suffix, CHORD_TYPES[t].suffix, length) == 0) prefix = true;
  }
  return prefix ? ParseResult::Incomplete : ParseResult::Invalid;
}

void chordName(const Chord &chord, char *out) {
  noteName(chord.root, out);
  strcat(out, CHORD_TYPES[chord.type].suffix);
}

int chordTones(const Chord &chord, ChordTone *out, int max) {
  int count = 0;
  const char *p = CHORD_TYPES[chord.type].degrees;
  while (*p && count < max) {
    const char *end = strchr(p, ' ');
    if (!end) end = p + strlen(p);
    char degree[4] = {};
    memcpy(degree, p, std::min<size_t>(end - p, 3));
    out[count++] = {noteAbove(chord.root, degree), p, (int)(end - p)};
    p = *end ? end + 1 : end;
  }
  return count;
}

// ---------- chord shapes ----------

static const int8_t X = -99;       // muted string in the tables below

// Open-string shapes (the ones everybody learns first), fret and finger per string.
struct OpenShape {
  int pitch;
  const char *suffix;
  int8_t fret[6], finger[6];
};
static const OpenShape OPEN_SHAPES[] = {
    {0, "", {X, 3, 2, 0, 1, 0}, {0, 3, 2, 0, 1, 0}},      // C
    {2, "", {X, X, 0, 2, 3, 2}, {0, 0, 0, 1, 3, 2}},      // D
    {4, "", {0, 2, 2, 1, 0, 0}, {0, 2, 3, 1, 0, 0}},      // E
    {7, "", {3, 2, 0, 0, 0, 3}, {2, 1, 0, 0, 0, 3}},      // G
    {9, "", {X, 0, 2, 2, 2, 0}, {0, 0, 1, 2, 3, 0}},      // A
    {2, "m", {X, X, 0, 2, 3, 1}, {0, 0, 0, 2, 3, 1}},     // Dm
    {4, "m", {0, 2, 2, 0, 0, 0}, {0, 2, 3, 0, 0, 0}},     // Em
    {9, "m", {X, 0, 2, 2, 1, 0}, {0, 0, 2, 3, 1, 0}},     // Am
    {0, "7", {X, 3, 2, 3, 1, 0}, {0, 3, 2, 4, 1, 0}},     // C7
    {2, "7", {X, X, 0, 2, 1, 2}, {0, 0, 0, 2, 1, 3}},     // D7
    {4, "7", {0, 2, 0, 1, 0, 0}, {0, 2, 0, 1, 0, 0}},     // E7
    {7, "7", {3, 2, 0, 0, 0, 1}, {3, 2, 0, 0, 0, 1}},     // G7
    {9, "7", {X, 0, 2, 0, 2, 0}, {0, 0, 2, 0, 3, 0}},     // A7
    {11, "7", {X, 2, 1, 2, 0, 2}, {0, 2, 1, 3, 0, 4}},    // B7
    {0, "maj7", {X, 3, 2, 0, 0, 0}, {0, 3, 2, 0, 0, 0}},  // Cmaj7
    {2, "maj7", {X, X, 0, 2, 2, 2}, {0, 0, 0, 1, 2, 3}},  // Dmaj7
    {4, "maj7", {0, 2, 1, 1, 0, 0}, {0, 3, 1, 2, 0, 0}},  // Emaj7
    {5, "maj7", {X, X, 3, 2, 1, 0}, {0, 0, 3, 2, 1, 0}},  // Fmaj7
    {7, "maj7", {3, 2, 0, 0, 0, 2}, {3, 2, 0, 0, 0, 1}},  // Gmaj7
    {9, "maj7", {X, 0, 2, 1, 2, 0}, {0, 0, 2, 1, 3, 0}},  // Amaj7
    {2, "m7", {X, X, 0, 2, 1, 1}, {0, 0, 0, 2, 1, 1}},    // Dm7
    {4, "m7", {0, 2, 0, 0, 0, 0}, {0, 2, 0, 0, 0, 0}},    // Em7
    {9, "m7", {X, 0, 2, 0, 1, 0}, {0, 0, 2, 0, 1, 0}},    // Am7
    {2, "sus2", {X, X, 0, 2, 3, 0}, {0, 0, 0, 1, 3, 0}},  // Dsus2
    {9, "sus2", {X, 0, 2, 2, 0, 0}, {0, 0, 1, 2, 0, 0}},  // Asus2
    {2, "sus4", {X, X, 0, 2, 3, 3}, {0, 0, 0, 1, 2, 3}},  // Dsus4
    {4, "sus4", {0, 2, 2, 2, 0, 0}, {0, 2, 3, 4, 0, 0}},  // Esus4
    {9, "sus4", {X, 0, 2, 2, 3, 0}, {0, 0, 1, 2, 3, 0}},  // Asus4
    {0, "6", {X, 3, 2, 2, 1, 0}, {0, 4, 2, 3, 1, 0}},     // C6
    {2, "6", {X, X, 0, 2, 0, 2}, {0, 0, 0, 1, 0, 2}},     // D6
    {4, "6", {0, 2, 2, 1, 2, 0}, {0, 2, 3, 1, 4, 0}},     // E6
    {7, "6", {3, 2, 0, 0, 0, 0}, {2, 1, 0, 0, 0, 0}},     // G6
    {9, "6", {X, 0, 2, 2, 2, 2}, {0, 0, 1, 1, 1, 1}},     // A6
    {4, "9", {0, 2, 0, 1, 0, 2}, {0, 2, 0, 1, 0, 3}},     // E9
    {0, "add9", {X, 3, 2, 0, 3, 0}, {0, 2, 1, 0, 3, 0}},  // Cadd9
    {4, "add9", {0, 2, 2, 1, 0, 2}, {0, 2, 3, 1, 0, 4}},  // Eadd9
    {9, "add9", {X, 0, 2, 4, 2, 0}, {0, 0, 1, 4, 2, 0}},  // Aadd9
    {2, "5", {X, X, 0, 2, 3, X}, {0, 0, 0, 1, 3, 0}},     // D5
    {4, "5", {0, 2, 2, X, X, X}, {0, 1, 2, 0, 0, 0}},     // E5
    {9, "5", {X, 0, 2, 2, X, X}, {0, 0, 1, 2, 0, 0}},     // A5
};

// Movable shapes: frets relative to the root on string `rootString`
// (0 = low E: "E shape", 1 = A string: "A shape"); moved along the neck.
struct MovableShape {
  const char *suffix;
  int rootString;
  int8_t fret[6], finger[6];
};
static const MovableShape MOVABLE_SHAPES[] = {
    {"", 0, {0, 2, 2, 1, 0, 0}, {1, 3, 4, 2, 1, 1}},
    {"", 1, {X, 0, 2, 2, 2, 0}, {0, 1, 2, 3, 4, 1}},
    {"m", 0, {0, 2, 2, 0, 0, 0}, {1, 3, 4, 1, 1, 1}},
    {"m", 1, {X, 0, 2, 2, 1, 0}, {0, 1, 3, 4, 2, 1}},
    {"7", 0, {0, 2, 0, 1, 0, 0}, {1, 3, 1, 2, 1, 1}},
    {"7", 1, {X, 0, 2, 0, 2, 0}, {0, 1, 3, 1, 4, 1}},
    {"maj7", 0, {0, X, 1, 1, 0, X}, {1, 0, 3, 4, 2, 0}},
    {"maj7", 1, {X, 0, 2, 1, 2, 0}, {0, 1, 3, 2, 4, 1}},
    {"m7", 0, {0, 2, 0, 0, 0, 0}, {1, 3, 1, 1, 1, 1}},
    {"m7", 1, {X, 0, 2, 0, 1, 0}, {0, 1, 3, 1, 2, 1}},
    {"sus2", 1, {X, 0, 2, 2, 0, 0}, {0, 1, 3, 4, 1, 1}},
    {"sus4", 0, {0, 2, 2, 2, 0, 0}, {1, 2, 3, 4, 1, 1}},
    {"sus4", 1, {X, 0, 2, 2, 3, 0}, {0, 1, 2, 3, 4, 1}},
    {"dim", 0, {0, 1, 2, 0, X, X}, {1, 2, 3, 1, 0, 0}},
    {"dim", 1, {X, 0, 1, 2, 1, X}, {0, 1, 2, 4, 3, 0}},
    {"aug", 0, {0, X, 2, 1, 1, 0}, {1, 0, 4, 2, 3, 1}},
    {"aug", 1, {X, 0, -1, -2, -2, X}, {0, 4, 3, 1, 2, 0}},
    {"6", 0, {0, X, -1, 1, 0, X}, {2, 0, 1, 4, 3, 0}},
    {"6", 1, {X, 0, 2, 2, 2, 2}, {0, 1, 3, 3, 3, 3}},
    {"9", 0, {0, 2, 0, 1, 0, 2}, {1, 3, 1, 2, 1, 4}},
    {"9", 1, {X, 0, -1, 0, 0, 0}, {0, 2, 1, 3, 3, 3}},
    {"add9", 1, {X, 0, 2, 4, 2, 0}, {0, 1, 2, 4, 3, 1}},
    {"5", 0, {0, 2, 2, X, X, X}, {1, 3, 4, 0, 0, 0}},
    {"5", 1, {X, 0, 2, 2, X, X}, {0, 1, 3, 4, 0, 0}},
};

static int lowestFret(const Voicing &v) {
  int lowest = 99;
  for (int s = 0; s < 6; s++)
    if (v.fret[s] > 0) lowest = std::min(lowest, (int)v.fret[s]);
  return lowest;
}

int chordVoicings(const Chord &chord, Voicing *out, int max) {
  const char *suffix = CHORD_TYPES[chord.type].suffix;
  int count = 0;
  for (const OpenShape &shape : OPEN_SHAPES) {
    if (count >= max) break;
    if (shape.pitch != chord.root.pitch || strcmp(shape.suffix, suffix) != 0) continue;
    for (int s = 0; s < 6; s++) {
      out[count].fret[s] = shape.fret[s] == X ? MUTED : shape.fret[s];
      out[count].finger[s] = shape.finger[s];
    }
    count++;
  }
  int firstMovable = count;
  for (const MovableShape &shape : MOVABLE_SHAPES) {
    if (count >= max) break;
    if (strcmp(shape.suffix, suffix) != 0) continue;
    int lowestOffset = 0;
    for (int s = 0; s < 6; s++)
      if (shape.fret[s] != X) lowestOffset = std::min(lowestOffset, (int)shape.fret[s]);
    // the root's fret on the root string; every finger must be at fret 1 or higher
    int base = mod12(chord.root.pitch - GUITAR_MIDI[shape.rootString]);
    while (base + lowestOffset < 1) base += 12;
    for (int s = 0; s < 6; s++) {
      out[count].fret[s] = shape.fret[s] == X ? MUTED : base + shape.fret[s];
      out[count].finger[s] = shape.finger[s];
    }
    count++;
  }
  std::sort(out + firstMovable, out + count,
            [](const Voicing &a, const Voicing &b) { return lowestFret(a) < lowestFret(b); });
  return count;
}

}  // namespace dsp
