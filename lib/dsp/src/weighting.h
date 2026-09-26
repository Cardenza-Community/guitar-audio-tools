// Frequency weighting filters for sound level measurement (IEC 61672).
//
// A-weighting imitates the ear: low and very high tones count less, because
// we hear them less. Z-weighting is "zero" weighting: flat, only the DC offset
// and inaudible rumble below 10 Hz are removed.
// Pure C++ (no Arduino), unit-tested on the PC.
#pragma once
#include <cstddef>

namespace dsp {

// Second-order IIR filter section ("biquad").
struct Biquad {
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
  float z1 = 0, z2 = 0;   // filter memory

  float process(float x) {
    float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
  }
  void reset() { z1 = z2 = 0; }

  // Digital version of the analog filter
  //   (num2 s^2 + num1 s + num0) / (den2 s^2 + den1 s + den0)
  // made with the bilinear transform, pre-warped at `warpHz` so that the
  // filter is exact at that frequency.
  static Biquad fromAnalog(double num2, double num1, double num0, double den2, double den1,
                           double den0, double warpHz, double sampleRate);
};

enum class Weighting { A, Z };

class WeightingFilter {
 public:
  WeightingFilter(Weighting type, float sampleRate);
  Weighting type() const { return type_; }
  float process(float x);
  void reset();

 private:
  Weighting type_;
  Biquad sections_[3];
  int count_ = 0;
  float gain_ = 1;    // makes the response exactly 0 dB at 1 kHz
};

// Response of the filter in dB at `hz`, computed from its formula
// (used by the tests and for documentation).
float aWeightingDb(float hz);

}  // namespace dsp
