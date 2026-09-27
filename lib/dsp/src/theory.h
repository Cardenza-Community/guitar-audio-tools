// Music theory for the Chords (and Scales) apps: note spelling, chord types,
// parsing a typed chord name and chord shapes on the guitar neck.
// Pure C++ (no Arduino), unit-tested on the PC.
//
// Notes are spelled with a letter and a pitch, so that every chord gets the
// right names: C minor is C Eb G (not C D# G), A major is A C# E.
#pragma once
#include <cstdint>

namespace dsp {

// A spelled note: letter 0..6 = C D E F G A B, pitch class 0..11 (C = 0).
struct SpelledNote {
  int letter;
  int pitch;
};

// "C", "F#", "Bb", "Ebb"; `out` needs at least 5 characters.
void noteName(SpelledNote note, char *out);

// A degree of a chord or a scale ("1", "b3", "#5", "9") -> semitones above
// the root and letter steps (b3: 3 semitones, 2 letters: C -> E).
// Returns false for a wrong degree.
bool parseDegree(const char *degree, int &semitones, int &letterSteps);

// The note `degree` above `root`, correctly spelled.
SpelledNote noteAbove(SpelledNote root, const char *degree);

struct ChordType {
  const char *suffix;    // what is typed after the root: "", "m", "maj7"...
  const char *degrees;   // "1 b3 5 b7", separated by spaces
};
extern const ChordType CHORD_TYPES[];
extern const int CHORD_TYPE_COUNT;

struct Chord {
  SpelledNote root;
  int type;              // index into CHORD_TYPES
};

enum class ParseResult {
  Chord,       // a complete chord name
  Incomplete,  // the start of one, e.g. "cma" (on the way to "cmaj7")
  Invalid,
};

// Parses a typed chord name, case does not matter: "am7", "F#m", "bb", "Cadd9".
ParseResult parseChord(const char *text, Chord &out);

// "Am7", "F#m", "Bb"; `out` needs at least 10 characters.
void chordName(const Chord &chord, char *out);

// The notes of a chord with their degrees; returns their number (at most 6).
struct ChordTone {
  SpelledNote note;
  const char *degree;    // points into CHORD_TYPES[].degrees, not terminated:
  int degreeLength;      // use degreeLength characters
};
int chordTones(const Chord &chord, ChordTone *out, int max);

// A chord shape: for each string (0 = low E ... 5 = high E) the fret
// (0 = open, MUTED = not played) and the finger (1 index ... 4 little, 0 none).
// The same finger on several strings at one fret is a barre.
const int MUTED = -1;
struct Voicing {
  int8_t fret[6];
  int8_t finger[6];
};

// Shapes of a chord: first the open-string shapes (if the chord has one),
// then movable barre shapes by their position on the neck.
int chordVoicings(const Chord &chord, Voicing *out, int max);

}  // namespace dsp
