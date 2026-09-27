#include "onset.h"
#include <algorithm>
#include <cmath>

namespace dsp {

OnsetDetector::OnsetDetector(float sampleRate, size_t fftSize, size_t hop)
    : sampleRate_(sampleRate), hop_(hop), fft_(fftSize), window_(fftSize), ring_(fftSize, 0),
      buffer_(fftSize) {
  warmUp_ = (int)(0.3f * frameRate());   // the slow average needs a moment first
  // band edges: logarithmic from 40 Hz to 8 kHz (or half the sample rate),
  // each band at least one FFT bin
  double binHz = sampleRate / fftSize;
  double top = std::min(8000.0, sampleRate / 2.0);
  const int BANDS = 20;
  size_t last = 0;
  for (int b = 0; b <= BANDS; b++) {
    size_t bin = (size_t)std::lround(40 * std::pow(top / 40, (double)b / BANDS) / binHz);
    if (bin < 1) bin = 1;
    if (bandStart_.empty() || bin > last) {
      bandStart_.push_back(bin);
      last = bin;
    }
  }
  previous_.assign(bandStart_.size() - 1, 0.0f);
  weights_.assign(previous_.size(), 1.0f);
  const double PI = 3.14159265358979323846;
  for (size_t i = 0; i < fftSize; i++) window_[i] = (float)(0.5 - 0.5 * std::cos(2 * PI * i / fftSize));
}

size_t OnsetDetector::process(const int16_t *samples, size_t count) {
  fresh_.clear();
  freshOnset_.clear();
  freshBands_.clear();
  for (size_t i = 0; i < count; i++) {
    ring_[ringPos_] = samples[i];
    ringPos_ = (ringPos_ + 1) % ring_.size();
    if (filled_ < ring_.size()) filled_++;
    if (++sinceFrame_ >= hop_ && filled_ == ring_.size()) {
      sinceFrame_ = 0;
      frame();
    }
  }
  return fresh_.size();
}

void OnsetDetector::frame() {
  const size_t n = ring_.size();
  for (size_t i = 0; i < n; i++) buffer_[i] = {ring_[(ringPos_ + i) % n] * window_[i], 0.0f};
  fft_.forward(buffer_.data());

  // spectral flux per band on compressed levels: log(1 + level) makes quiet
  // and loud parts count alike; only increases count (a new sound, not a
  // fading one)
  float flux = 0;
  for (size_t b = 0; b + 1 < bandStart_.size(); b++) {
    float sum = 0;
    for (size_t k = bandStart_[b]; k < bandStart_[b + 1]; k++) sum += std::abs(buffer_[k]);
    float m = std::log1p(sum / (bandStart_[b + 1] - bandStart_[b]) / 64.0f);
    float d = std::max(0.0f, m - previous_[b]);
    flux += weights_[b] * d;
    freshBands_.push_back(d);
    previous_[b] = m;
  }
  fresh_.push_back(flux);

  // peak picking for single hits: the middle of the last three values is a
  // local maximum, clearly above the slow average, and not right after a hit
  last_[0] = last_[1];
  last_[1] = last_[2];
  last_[2] = flux;
  bool peak = last_[1] > last_[0] && last_[1] >= last_[2] && last_[1] > 2.0f * average_ + 1.0f;
  if (holdOff_ > 0) holdOff_--;
  if (warmUp_ > 0) warmUp_--;
  bool onset = peak && holdOff_ == 0 && warmUp_ == 0;
  if (onset) holdOff_ = (int)(0.1f * frameRate());   // at most 10 hits per second
  freshOnset_.push_back(onset);
  average_ += 0.02f * (flux - average_);
}

}  // namespace dsp
