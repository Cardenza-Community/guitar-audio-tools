#include "decimator.h"
#include <cmath>

namespace dsp {

static const double PI = 3.14159265358979323846;

Decimator::Decimator(int factor, int taps) : factor_(factor), coefficients_(taps), delay_(taps, 0) {
  // windowed-sinc low-pass (Blackman window), cut-off at 80 % of the new
  // Nyquist frequency (half the new sample rate): 1600 Hz for 16 kHz -> 4 kHz
  double cutoff = 0.8 * 0.5 / factor;           // in cycles per input sample
  int middle = taps / 2;
  double sum = 0;
  for (int i = 0; i < taps; i++) {
    int n = i - middle;
    double sinc = n == 0 ? 2 * cutoff : std::sin(2 * PI * cutoff * n) / (PI * n);
    double window = 0.42 - 0.5 * std::cos(2 * PI * i / (taps - 1)) + 0.08 * std::cos(4 * PI * i / (taps - 1));
    coefficients_[i] = (float)(sinc * window);
    sum += coefficients_[i];
  }
  for (auto &c : coefficients_) c = (float)(c / sum);   // gain 1 at 0 Hz
}

void Decimator::reset() {
  std::fill(delay_.begin(), delay_.end(), 0.0f);
  pos_ = 0;
  phase_ = 0;
}

size_t Decimator::process(const int16_t *in, size_t count, float *out) {
  size_t written = 0;
  const size_t taps = coefficients_.size();
  for (size_t i = 0; i < count; i++) {
    delay_[pos_] = in[i];
    pos_ = (pos_ + 1) % taps;
    if (++phase_ < factor_) continue;
    phase_ = 0;
    // filter only at the output samples: the others would be thrown away
    float y = 0;
    size_t k = pos_;                              // oldest sample
    for (size_t t = 0; t < taps; t++) {
      y += coefficients_[t] * delay_[k];
      if (++k == taps) k = 0;
    }
    out[written++] = y;
  }
  return written;
}

}  // namespace dsp
