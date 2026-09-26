#include "pitch.h"
#include <cmath>

namespace dsp {

YinDetector::YinDetector(int sampleRate, float minHz, float maxHz, int window, int refineLag)
    : sampleRate_(sampleRate),
      minLag_((int)(sampleRate / maxHz)),
      maxLag_((int)(sampleRate / minHz) + 1),
      window_(window),
      refineLag_(refineLag),
      diff_(maxLag_ + 2),
      rawDiff_(maxLag_ + 2) {
  if (minLag_ < 2) minLag_ = 2;
}

size_t YinDetector::samplesNeeded() const {
  int longest = maxLag_ + 1;
  if (refineLag_ + 3 > longest) longest = refineLag_ + 3;
  return window_ + longest + 1;
}

// Plain squared difference between the wave and its copy shifted by `lag`.
float YinDetector::differenceAt(const float *x, int lag) const {
  // float on purpose: the ESP32-S3 FPU is single precision only
  float d = 0;
  for (int i = 0; i < window_; i++) {
    float r = x[i] - x[i + lag];
    d += r * r;
  }
  return d;
}

float YinDetector::detect(const float *x, size_t count) {
  lastAperiodicity_ = 1;
  if (count < samplesNeeded()) return 0;

  // 1) squared difference between the wave and its shifted copy,
  //    normalized by its running average ("cumulative mean normalized difference")
  diff_[0] = 1;
  double runningSum = 0;
  for (int tau = 1; tau <= maxLag_ + 1; tau++) {
    float d = differenceAt(x, tau);
    rawDiff_[tau] = d;
    runningSum += d;
    diff_[tau] = runningSum > 0 ? (float)(d * tau / runningSum) : 1;
  }

  // 2) the first lag below the threshold, then walk down to its minimum
  int tau = minLag_;
  while (tau <= maxLag_ && diff_[tau] >= threshold_) tau++;
  if (tau > maxLag_) return 0;
  while (tau < maxLag_ && diff_[tau + 1] < diff_[tau]) tau++;
  lastAperiodicity_ = diff_[tau];

  // 3) refine between two samples: fit a parabola through three points.
  //    The plain difference is used here: the normalized one is slightly
  //    skewed and would read high pitches a fraction of a cent sharp.
  float a = rawDiff_[tau - 1], b = rawDiff_[tau], c = rawDiff_[tau + 1];
  float denom = a - 2 * b + c;
  float shift = denom != 0 ? (a - c) / (2 * denom) : 0;
  float period = tau + shift;

  // 4) refinement over many periods (see pitch.h)
  int periods = (int)(refineLag_ / period);
  if (periods >= 2) {
    int around = (int)std::lround(periods * period);
    float d[7];
    for (int k = 0; k < 7; k++) d[k] = differenceAt(x, around - 3 + k);
    int best = 1;                                // minimum among the inner five
    for (int k = 2; k <= 5; k++)
      if (d[k] < d[best]) best = k;
    float den = d[best - 1] - 2 * d[best] + d[best + 1];
    float frac = den != 0 ? (d[best - 1] - d[best + 1]) / (2 * den) : 0;
    period = (around - 3 + best + frac) / periods;
  }
  return sampleRate_ / period;
}

}  // namespace dsp
