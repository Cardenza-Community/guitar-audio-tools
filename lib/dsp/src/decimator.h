// Lowers the sample rate by an integer factor (e.g. 16 kHz -> 4 kHz).
// A low-pass filter first removes everything above the new half sample rate,
// otherwise high tones would fold down and appear as false low tones.
// Pure C++ (no Arduino), unit-tested on the PC.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dsp {

class Decimator {
 public:
  // factor: keep every factor-th sample; taps: filter length (odd, longer =
  // sharper cut-off). The filter passes up to about 0.33 of the new sample rate.
  Decimator(int factor, int taps = 63);

  // Consumes `count` input samples, writes the output samples to `out`
  // (at most count / factor + 1) and returns how many were written.
  size_t process(const int16_t *in, size_t count, float *out);
  void reset();

 private:
  int factor_;
  std::vector<float> coefficients_;
  std::vector<float> delay_;     // the last `taps` input samples (ring buffer)
  size_t pos_ = 0;
  int phase_ = 0;                // counts input samples up to the next output
};

}  // namespace dsp
