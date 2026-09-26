#include "fft.h"
#include <cmath>
#include <utility>

namespace dsp {

Fft::Fft(size_t size) : size_(size), twiddles_(size / 2) {
  const double PI = 3.14159265358979323846;
  for (size_t k = 0; k < size / 2; k++)
    twiddles_[k] = std::polar(1.0f, (float)(-2 * PI * k / size));
}

void Fft::forward(std::complex<float> *data) const {
  const size_t n = size_;
  // 1) reorder the values: index -> index with its bits reversed
  for (size_t i = 1, j = 0; i < n; i++) {
    size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(data[i], data[j]);
  }
  // 2) combine pairs, then groups of 4, 8 ... ("butterflies")
  for (size_t len = 2; len <= n; len <<= 1) {
    size_t step = n / len;
    for (size_t start = 0; start < n; start += len) {
      for (size_t k = 0; k < len / 2; k++) {
        std::complex<float> t = twiddles_[k * step] * data[start + k + len / 2];
        std::complex<float> u = data[start + k];
        data[start + k] = u + t;
        data[start + k + len / 2] = u - t;
      }
    }
  }
}

}  // namespace dsp
