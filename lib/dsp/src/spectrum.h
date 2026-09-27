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

// ISO third-octave bands with the centres 50, 63, 80, 100 ... 12.5k, 16k Hz:
// SpectrumBands(rate, fftSize, THIRD_OCTAVE_BANDS, THIRD_OCTAVE_MIN_HZ,
// THIRD_OCTAVE_MAX_HZ). The centres are 1000 * 10^(n/10), the edges half a
// step (10^(1/20)) away. The 50 Hz band is only 11 Hz wide: it needs an FFT
// of 4096 samples at 32 kHz (bins 7.8 Hz apart).
constexpr int THIRD_OCTAVE_BANDS = 26;
constexpr float THIRD_OCTAVE_MIN_HZ = 44.668f;    // 1000 * 10^(-13.5/10)
constexpr float THIRD_OCTAVE_MAX_HZ = 17782.8f;   // 1000 * 10^(12.5/10), above 16 kHz: cut off

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
