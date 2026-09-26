// Fast Fourier Transform: splits a block of sound into frequencies.
// Radix-2, in place, size must be a power of two. Pure C++ (no Arduino),
// unit-tested on the PC.
#pragma once
#include <complex>
#include <cstddef>
#include <vector>

namespace dsp {

class Fft {
 public:
  explicit Fft(size_t size);
  size_t size() const { return size_; }

  // Transforms `data` (size() values) in place: afterwards data[k] holds
  // frequency k * sampleRate / size() (k < size() / 2 is what matters).
  void forward(std::complex<float> *data) const;

 private:
  size_t size_;
  std::vector<std::complex<float>> twiddles_;   // e^(-2 pi i k / size)
};

}  // namespace dsp
