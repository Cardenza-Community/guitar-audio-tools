#include "spectrum.h"
#include <algorithm>
#include <cmath>
#include "level.h"

namespace dsp {

SpectrumBands::SpectrumBands(float sampleRate, size_t fftSize, int bands, float minHz, float maxHz)
    : sampleRate_(sampleRate), fft_(fftSize), window_(fftSize), buffer_(fftSize) {
  const double PI = 3.14159265358979323846;
  for (size_t i = 0; i < fftSize; i++) {
    window_[i] = (float)(0.5 - 0.5 * std::cos(2 * PI * i / fftSize));   // Hann
    windowPower_ += window_[i] * window_[i];
  }
  double ratio = std::pow(maxHz / minHz, 1.0 / bands);
  for (int b = 0; b <= bands; b++) edges_.push_back((float)(minHz * std::pow(ratio, b)));

  // FFT bins of each band; a band narrower than one bin gets the nearest bin
  double binHz = sampleRate / fftSize;
  for (int b = 0; b < bands; b++) {
    size_t first = (size_t)std::ceil(edges_[b] / binHz);
    size_t last = (size_t)std::ceil(edges_[b + 1] / binHz) - 1;
    if (last < first) first = last = (size_t)std::lround(std::sqrt(edges_[b] * edges_[b + 1]) / binHz);
    last = std::min(last, fftSize / 2 - 1);
    firstBin_.push_back(first);
    lastBin_.push_back(last);
  }
}

float SpectrumBands::centreHz(int band) const { return std::sqrt(edges_[band] * edges_[band + 1]); }

void SpectrumBands::analyse(const int16_t *samples, float *levelsDb) {
  const size_t n = fft_.size();
  float mean = 0;
  for (size_t i = 0; i < n; i++) mean += samples[i];
  mean /= n;
  for (size_t i = 0; i < n; i++) buffer_[i] = {(samples[i] - mean) * window_[i], 0.0f};
  fft_.forward(buffer_.data());

  // power scaled so that a full-scale sine inside one band reads -3 dBFS,
  // like its RMS level: |X|^2 summed over both sides / (n * window power)
  for (int b = 0; b < bands(); b++) {
    double power = 0;
    for (size_t k = firstBin_[b]; k <= lastBin_[b]; k++) power += std::norm(buffer_[k]);
    power = 2 * power / (n * windowPower_);
    double db = power > 0 ? 10 * std::log10(power / ((double)FULL_SCALE * FULL_SCALE)) : SILENCE_DB;
    levelsDb[b] = (float)std::max(db, (double)SILENCE_DB);
  }
}

}  // namespace dsp
