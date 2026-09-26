// Spectrum analyser bands: the level of the sound in logarithmically spaced
// frequency bands, like the graphic equaliser display of a hi-fi system.
//
// One block of samples -> Hann window -> FFT -> the power of all FFT bins
// inside a band is added up -> dB. Adding up the power (not averaging) makes
// pink noise, which has equal energy per octave, look flat.
// Pure C++ (no Arduino), unit-tested on the PC.
#pragma once
#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>
#include "fft.h"

namespace dsp {

class SpectrumBands {
 public:
  // bands spaced evenly on a log scale between minHz and maxHz
  SpectrumBands(float sampleRate, size_t fftSize, int bands, float minHz, float maxHz);

  int bands() const { return (int)edges_.size() - 1; }
  size_t fftSize() const { return fft_.size(); }
  // centre frequency of a band (for labels)
  float centreHz(int band) const;

  // `fftSize()` samples in, band levels in dBFS out (`bands()` values)
  void analyse(const int16_t *samples, float *levelsDb);

 private:
  float sampleRate_;
  Fft fft_;
  std::vector<float> window_;
  std::vector<std::complex<float>> buffer_;
  std::vector<float> edges_;        // band edges in Hz, bands() + 1 values
  std::vector<size_t> firstBin_;    // FFT bins belonging to each band
  std::vector<size_t> lastBin_;
  float windowPower_ = 0;
};

}  // namespace dsp
