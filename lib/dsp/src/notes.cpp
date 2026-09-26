#include "notes.h"
#include <cmath>

namespace dsp {

static const char *NAMES[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

Note noteFromFrequency(float hz, float a4) {
  float midi = 69 + 12 * std::log2(hz / a4);
  int nearest = (int)std::lround(midi);
  Note n;
  n.midi = nearest;
  n.name = NAMES[((nearest % 12) + 12) % 12];
  n.octave = (int)std::floor(nearest / 12.0) - 1;
  n.cents = (midi - nearest) * 100;
  return n;
}

float frequencyOfMidi(int midi, float a4) {
  return a4 * std::pow(2.0f, (midi - 69) / 12.0f);
}

}  // namespace dsp
