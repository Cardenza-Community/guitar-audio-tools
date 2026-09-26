// Musical notes: frequency <-> note name, octave and deviation in cents.
// 100 cents = one semitone. Pure C++ (no Arduino), unit-tested on the PC.
#pragma once

namespace dsp {

struct Note {
  int midi;          // MIDI note number: A4 = 69, E2 = 40
  const char *name;  // "C", "C#", ... "B"
  int octave;        // scientific pitch notation: A4 = 440 Hz
  float cents;       // deviation from the exact note, -50..+50
};

// Nearest note to `hz`. `a4` is the reference pitch (usually 440 Hz).
Note noteFromFrequency(float hz, float a4 = 440.0f);

// Exact frequency of a MIDI note.
float frequencyOfMidi(int midi, float a4 = 440.0f);

}  // namespace dsp
