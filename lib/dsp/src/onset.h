// Onset detection: how strongly "something new starts" in the sound, e.g. a
// drum hit, a new note or a clap.
//
// Spectral flux: every `hop` samples a short FFT is taken and split into about
// 20 logarithmic bands; for every band the (logarithmic) level is compared with
// the previous frame and only the increases are added up. Steady sound gives
// ~0, a hit gives a peak. Bands (not single FFT bins) give the kick drum as
// much say as a hi-hat, which covers hundreds of bins.
// The result is the onset strength envelope (one value per hop), used by the
// tempo estimator. isOnset() additionally picks single hits (for claps).
// Pure C++ (no Arduino), unit-tested on the PC.
#pragma once
#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>
#include "fft.h"

namespace dsp {

class OnsetDetector {
 public:
  // fftSize samples per frame, a new frame every `hop` samples
  OnsetDetector(float sampleRate, size_t fftSize = 512, size_t hop = 128);

  float frameRate() const { return sampleRate_ / hop_; }

  // Feeds samples; returns how many new envelope values are ready (read them
  // with value(i), i = 0 .. count-1, oldest first; valid until the next call).
  size_t process(const int16_t *samples, size_t count);
  float value(size_t i) const { return fresh_[i]; }
  // true when fresh value i was a single clear hit (a peak well above the
  // recent average)
  bool isOnset(size_t i) const { return freshOnset_[i]; }

  // per-band increases of fresh value i (bands() values), for analysis
  int bands() const { return (int)previous_.size(); }
  const float *bandFlux(size_t i) const { return &freshBands_[i * previous_.size()]; }
  // weight of each band in the onset strength (default: all 1)
  void setBandWeights(const std::vector<float> &weights) { weights_ = weights; }

 private:
  void frame();

  float sampleRate_;
  size_t hop_;
  Fft fft_;
  std::vector<float> window_;
  std::vector<int16_t> ring_;          // the last fftSize samples
  size_t ringPos_ = 0, sinceFrame_ = 0, filled_ = 0;
  std::vector<std::complex<float>> buffer_;
  std::vector<size_t> bandStart_;      // first FFT bin of each band (+ end)
  std::vector<float> previous_;        // log band levels of the previous frame
  std::vector<float> fresh_;
  std::vector<float> freshBands_;      // fresh count x bands
  std::vector<float> weights_;
  std::vector<bool> freshOnset_;
  float average_ = 0;                  // slow average of the envelope
  float last_[3] = {0, 0, 0};          // for peak picking
  int holdOff_ = 0;                    // frames to wait after a hit
  int warmUp_ = 0;                     // frames until the average is usable
};

}  // namespace dsp
