#include "weighting.h"
#include <cmath>
#include <complex>

namespace dsp {

static const double PI = 3.14159265358979323846;

// Pole frequencies of the A-weighting curve (IEC 61672-1)
static const double F1 = 20.598997, F2 = 107.65265, F3 = 737.86223, F4 = 12194.217;

Biquad Biquad::fromAnalog(double num2, double num1, double num0, double den2, double den1,
                          double den0, double warpHz, double sampleRate) {
  double w = 2 * PI * warpHz;
  double k = w / std::tan(w / (2 * sampleRate));   // s = k (1 - 1/z) / (1 + 1/z)
  double k2 = k * k;
  double B0 = num2 * k2 + num1 * k + num0;
  double B1 = 2 * (num0 - num2 * k2);
  double B2 = num2 * k2 - num1 * k + num0;
  double A0 = den2 * k2 + den1 * k + den0;
  double A1 = 2 * (den0 - den2 * k2);
  double A2 = den2 * k2 - den1 * k + den0;
  Biquad q;
  q.b0 = (float)(B0 / A0);
  q.b1 = (float)(B1 / A0);
  q.b2 = (float)(B2 / A0);
  q.a1 = (A1 / A0);
  q.a2 = (A2 / A0);
  return q;
}

// Gain of one section at frequency hz (digital response).
static std::complex<double> sectionResponse(const Biquad &q, double hz, double sampleRate) {
  std::complex<double> z = std::polar(1.0, -2 * PI * hz / sampleRate);   // z^-1
  std::complex<double> num = (double)q.b0 + (double)q.b1 * z + (double)q.b2 * z * z;
  std::complex<double> den = 1.0 + (double)q.a1 * z + (double)q.a2 * z * z;
  return num / den;
}

WeightingFilter::WeightingFilter(Weighting type, float sampleRate) : type_(type) {
  double w1 = 2 * PI * F1, w2 = 2 * PI * F2, w3 = 2 * PI * F3, w4 = 2 * PI * F4;
  if (type == Weighting::A) {
    // A(s) = s^4 / ((s + w1)^2 (s + w2) (s + w3) (s + w4)^2), split into 3 sections.
    // The low sections are pre-warped at their corner frequencies. The high one
    // (12.2 kHz poles, close to half the sample rate) is pre-warped at 7.5 kHz:
    // at 32 kHz this keeps the error below 0.25 dB up to 8 kHz (-1.7 dB at 10 kHz).
    sections_[0] = Biquad::fromAnalog(1, 0, 0, 1, 2 * w1, w1 * w1, F1, sampleRate);
    sections_[1] = Biquad::fromAnalog(1, 0, 0, 1, w2 + w3, w2 * w3, std::sqrt(F2 * F3), sampleRate);
    sections_[2] = Biquad::fromAnalog(0, 0, 1, 1, 2 * w4, w4 * w4, 7500, sampleRate);
    count_ = 3;
  } else {
    // Z: only a gentle high-pass at 3 Hz (removes DC offset and rumble)
    double w = 2 * PI * 3.0;
    sections_[0] = Biquad::fromAnalog(0, 1, 0, 0, 1, w, 3.0, sampleRate);
    count_ = 1;
  }
  // scale so that 1 kHz passes with exactly 0 dB
  std::complex<double> h = 1;
  for (int i = 0; i < count_; i++) h *= sectionResponse(sections_[i], 1000, sampleRate);
  gain_ = (float)(1.0 / std::abs(h));
}

float WeightingFilter::process(float x) {
  x *= gain_;
  for (int i = 0; i < count_; i++) x = sections_[i].process(x);
  return x;
}

void WeightingFilter::reset() {
  for (int i = 0; i < count_; i++) sections_[i].reset();
}

float aWeightingDb(float hz) {
  double f2 = (double)hz * hz;
  double ra = (F4 * F4 * f2 * f2) /
              ((f2 + F1 * F1) * std::sqrt((f2 + F2 * F2) * (f2 + F3 * F3)) * (f2 + F4 * F4));
  return (float)(20 * std::log10(ra) + 2.0);   // +2.0 dB = 0 dB at 1 kHz
}

}  // namespace dsp
