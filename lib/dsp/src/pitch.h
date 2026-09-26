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
  // window: number of samples compared (longer = steadier, slower).
  YinDetector(int sampleRate, float minHz, float maxHz, int window);

  // Samples needed by detect(): window + the longest period searched.
  size_t samplesNeeded() const { return window_ + maxLag_ + 1; }

  // Returns the pitch in Hz, or 0 when there is no clear pitch
  // (noise, silence, too few samples).
  float detect(const float *x, size_t count);

  // How periodic the last signal was: 0 = perfect tone, 1 = noise.
  float lastAperiodicity() const { return lastAperiodicity_; }

 private:
  int sampleRate_;
  int minLag_, maxLag_, window_;
  float threshold_ = 0.15f;
  std::vector<float> diff_;     // normalized difference (for the threshold)
  std::vector<float> rawDiff_;  // plain squared difference (for refinement)
  float lastAperiodicity_ = 1;
};

}  // namespace dsp
