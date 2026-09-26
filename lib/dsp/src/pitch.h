// Pitch detection with the YIN algorithm
// (de Cheveigné & Kawahara, 2002).
//
// Idea: compare the wave with a copy of itself shifted by `tau` samples.
// When `tau` equals one period, the two copies match best.
// Frequency = sample rate / tau.
// Pure C++ (no Arduino), unit-tested on the PC.
#pragma once
#include <cstddef>
#include <vector>

namespace dsp {

class YinDetector {
 public:
  // sampleRate: samples per second; minHz/maxHz: range of pitches to search;
  // window: number of samples compared (longer = steadier, slower);
  // refineLag: see "Refinement" below (0 = no refinement).
  YinDetector(int sampleRate, float minHz, float maxHz, int window, int refineLag = 520);

  // Samples needed by detect(): window + the longest lag used.
  size_t samplesNeeded() const;

  // Returns the pitch in Hz, or 0 when there is no clear pitch
  // (noise, silence, too few samples).
  float detect(const float *x, size_t count);

  // Refinement: YIN finds the period in whole samples and a parabola through
  // three points estimates the fraction. With real sound (harmonics) the
  // parabola is off by up to ~0.07 samples; for a 440 Hz tone at 16 kHz (period
  // 36.4 samples) that is 0.2 % = 3.4 cents. So the fraction is measured again
  // over as many whole periods as fit into `refineLag` samples (e.g. 14 periods
  // of 440 Hz), which divides the error by that number.

  // How periodic the last signal was: 0 = perfect tone, 1 = noise.
  float lastAperiodicity() const { return lastAperiodicity_; }

 private:
  int sampleRate_;
  int minLag_, maxLag_, window_, refineLag_;
  float differenceAt(const float *x, int lag) const;
  float threshold_ = 0.15f;
  std::vector<float> diff_;     // normalized difference (for the threshold)
  std::vector<float> rawDiff_;  // plain squared difference (for refinement)
  float lastAperiodicity_ = 1;
};

}  // namespace dsp
