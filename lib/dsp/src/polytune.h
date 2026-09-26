// Polyphonic tuner: all six strings of a strummed chord at once.
//
// We know where the strings should be (standard tuning), so we do not need to
// split the chord in general. For every string we look only into a narrow band
// around its note (+-100 cents) and find the exact frequency of the strongest
// peak there, at the fundamental and at the 2nd harmonic.
//
// "Zooming" into a band: the spectrum is computed only at the frequencies we
// ask for (Goertzel algorithm) on a Hann-windowed block of about 1 s. A coarse
// scan in 10-cent steps finds the peak, a golden-section search refines it.
// Unlike an FFT this has no bin-interpolation error and needs little memory.
//
// Strings are measured from the lowest up. A peak in a higher string's band
// that is just a harmonic of an already measured lower string (e.g. the 3rd
// harmonic of the low E inside the B string's band) is skipped when the band
// holds another clear peak - the string's own note.
//
// Background: a steady tone in the room (hum, a whine of some device) can
// look like a string. setBackground() is given the sound from just before the
// strum; a peak that was already there at least a third as strong is ignored.
//
// Limits: some partials of different strings nearly coincide (3rd harmonic of
// low E = 247.2 Hz vs. B string 246.9 Hz, 2 cents apart), which cannot be
// separated in 1 s. Expect about +-2 cents per string in a chord; fine-tune
// single strings with the normal tuner.
// Pure C++ (no Arduino), unit-tested on the PC.
#pragma once
#include <cstddef>
#include <vector>
#include "tuning.h"

namespace dsp {

// Minimum peak amplitude of a string (sample units) to count as heard.
// Measured on real strums: the low E often only 13...57 (the microphone hears
// low tones weakly), microphone noise about 3.
constexpr float MIN_AMPLITUDE = 12;
// A strum whose loudest string is below this is too weak to measure.
constexpr float MIN_STRUM_AMPLITUDE = 40;

struct StringReading {
  bool found = false;    // a clear peak near the string's note
  float hz = 0;
  float cents = 0;       // deviation from the target note
  float amplitude = 0;   // in sample units (a sine of amplitude A gives about A)
};

class PolyTuner {
 public:
  // sampleRate of the blocks passed to analyse(), length = block size.
  PolyTuner(float sampleRate, size_t length);

  size_t length() const { return window_.size(); }

  // The sound before the strum (`length()` samples, read from a ring buffer
  // starting at `start`); used by the next analyse() calls.
  void setBackground(const float *ring, size_t start, float a4, const int *midi = GUITAR_MIDI);
  void clearBackground() { hasBackground_ = false; }

  // Analyses one block of `length()` samples. `midi` are the target notes
  // (default: standard tuning E2 A2 D3 G3 B3 E4).
  void analyse(const float *x, float a4, StringReading out[GUITAR_STRINGS],
               const int *midi = GUITAR_MIDI);

 private:
  // amplitude of the windowed block at frequency hz
  double amplitudeAt(double hz) const;
  struct Peak {
    double hz, amplitude;
    bool explained = false;   // matches a harmonic of a lower string
  };
  // coarse scan of a band in 10-cent steps
  std::vector<float> scan(double loHz, double hiHz) const;
  // local maxima between loHz and hiHz, strongest first, refined; peaks that
  // were in the background (band number `band`) are left out
  std::vector<Peak> findPeaks(double loHz, double hiHz, int band) const;
  Peak refine(double hz) const;
  // the string's own peak: skips peaks explained by lower strings' harmonics
  Peak pickPeak(double loHz, double hiHz, int band, const StringReading *lower, int lowerCount) const;
  static void bandLimits(double target, int harmonic, double &lo, double &hi);

  float sampleRate_;
  std::vector<float> window_;
  std::vector<float> windowed_;
  double windowSum_ = 0;
  bool hasBackground_ = false;
  std::vector<float> background_[GUITAR_STRINGS * 2];   // coarse scans, per band
};

}  // namespace dsp
