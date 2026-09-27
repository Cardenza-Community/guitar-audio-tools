#include "strum_tuner.h"
#include <algorithm>
#include <cmath>
#include "notes.h"

namespace dsp {

static const double PI = 3.14159265358979323846;

StrumTuner::StrumTuner(float sampleRate, size_t length)
    : sampleRate_(sampleRate), window_(length), windowed_(length) {
  for (size_t i = 0; i < length; i++) {
    window_[i] = (float)(0.5 - 0.5 * std::cos(2 * PI * i / (length - 1)));   // Hann
    windowSum_ += window_[i];
  }
}

// Goertzel algorithm: the spectrum at one single frequency.
// The loop runs in float (the ESP32-S3 has no double-precision hardware);
// the unit tests check that this keeps the precision.
double StrumTuner::amplitudeAt(double hz) const {
  float c = (float)(2 * std::cos(2 * PI * hz / sampleRate_));
  float s1 = 0, s2 = 0;
  for (float v : windowed_) {
    float s0 = v + c * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  double power = s1 * s1 + s2 * s2 - c * s1 * s2;
  return 2 * std::sqrt(power > 0 ? power : 0) / windowSum_;
}

static const double STEP = 1.0057929410678534;   // 2^(10/1200): 10 cents

// golden-section search for the maximum between the neighbouring scan points
StrumTuner::Peak StrumTuner::refine(double hz) const {
  double a = hz / STEP, b = hz * STEP;
  const double g = 0.6180339887;
  double c = b - g * (b - a), d = a + g * (b - a);
  double fc = amplitudeAt(c), fd = amplitudeAt(d);
  for (int i = 0; i < 16; i++) {
    if (fc > fd) {
      b = d; d = c; fd = fc;
      c = b - g * (b - a); fc = amplitudeAt(c);
    } else {
      a = c; c = d; fc = fd;
      d = a + g * (b - a); fd = amplitudeAt(d);
    }
  }
  double peak = (a + b) / 2;
  return {peak, amplitudeAt(peak), false};
}

void StrumTuner::bandLimits(double target, int harmonic, double &lo, double &hi) {
  const double band = std::pow(2.0, 100 / 1200.0);     // +-100 cents
  lo = target * harmonic / band;
  hi = target * harmonic * band;
}

std::vector<float> StrumTuner::scan(double loHz, double hiHz) const {
  std::vector<float> a;
  for (double f = loHz; f <= hiHz; f *= STEP) a.push_back((float)amplitudeAt(f));
  return a;
}

void StrumTuner::setBackground(const float *ring, size_t start, float a4, const int *midi) {
  size_t n = windowed_.size();
  for (size_t i = 0; i < n; i++) windowed_[i] = ring[(start + i) % n] * window_[i];
  for (int s = 0; s < GUITAR_STRINGS; s++) {
    double target = frequencyOfMidi(midi[s], a4);
    for (int k = 1; k <= 2; k++) {
      double lo, hi;
      bandLimits(target, k, lo, hi);
      background_[s * 2 + k - 1] = scan(lo, hi);
    }
  }
  hasBackground_ = true;
}

std::vector<StrumTuner::Peak> StrumTuner::findPeaks(double loHz, double hiHz, int band) const {
  // coarse scan in 10-cent steps
  std::vector<float> amps = scan(loHz, hiHz);
  const std::vector<float> *bg = hasBackground_ ? &background_[band] : nullptr;
  // local maxima inside the band (one at the edge is a neighbour's slope)
  std::vector<Peak> peaks;
  for (size_t i = 1; i + 1 < amps.size(); i++) {
    if (!(amps[i] > amps[i - 1] && amps[i] >= amps[i + 1])) continue;
    if (bg && i + 1 < bg->size()) {
      // already there before the strum (at least a third as strong): background
      float before = std::max({(*bg)[i - 1], (*bg)[i], (*bg)[i + 1]});
      if (amps[i] < 3 * before) continue;
    }
    peaks.push_back({loHz * std::pow(STEP, (double)i), amps[i], false});
  }
  std::sort(peaks.begin(), peaks.end(), [](const Peak &x, const Peak &y) { return x.amplitude > y.amplitude; });
  if (peaks.size() > 3) peaks.resize(3);         // only the strongest few matter
  for (auto &p : peaks) p = refine(p.hz);
  return peaks;
}

StrumTuner::Peak StrumTuner::pickPeak(double loHz, double hiHz, int band, const StringReading *lower,
                                    int lowerCount) const {
  std::vector<Peak> peaks = findPeaks(loHz, hiHz, band);
  if (peaks.empty()) return {std::sqrt(loHz * hiHz), 0, false};
  // how far (in cents) the peak is from the nearest harmonic (2nd ... 8th)
  // of a lower string; a peak within 15 cents counts as that harmonic
  auto mismatch = [&](double hz) {
    double nearest = 1e9;
    for (int j = 0; j < lowerCount; j++) {
      if (!lower[j].found) continue;
      for (int m = 2; m <= 8; m++)
        nearest = std::min(nearest, std::fabs(1200 * std::log2(hz / (lower[j].hz * m))));
    }
    return nearest;
  };
  // 1) the strongest clear peak (>= 25 % of the strongest) that is not a
  //    lower string's harmonic
  // 2) if all clear peaks are such harmonics, the string's own note has merged
  //    with one of them: take the peak that matches its harmonic worst
  Peak best = peaks[0];
  double bestMismatch = -1;
  for (const Peak &p : peaks) {
    if (p.amplitude < 0.25 * peaks[0].amplitude) continue;
    double mm = mismatch(p.hz);
    if (mm >= 15) return p;
    if (mm > bestMismatch) {
      bestMismatch = mm;
      best = p;
    }
  }
  best.explained = true;
  return best;
}

void StrumTuner::analyse(const float *x, float a4, StringReading out[GUITAR_STRINGS], const int *midi) {
  for (size_t i = 0; i < windowed_.size(); i++) windowed_[i] = x[i] * window_[i];

  double strongest = 0;
  for (int s = 0; s < GUITAR_STRINGS; s++) {
    double target = frequencyOfMidi(midi[s], a4);
    // fundamental (k = 1) and 2nd harmonic (k = 2), each as cents of the string
    double cents[2], amp[2];
    bool other[2];
    for (int k = 1; k <= 2; k++) {
      double lo, hi;
      bandLimits(target, k, lo, hi);
      Peak p = pickPeak(lo, hi, s * 2 + k - 1, out, s);
      amp[k - 1] = p.amplitude;
      other[k - 1] = p.explained;
      cents[k - 1] = 1200 * std::log2(p.hz / (target * k));
    }
    // combine: weighted by power when they agree. When they disagree, trust
    // the one that is not a lower string's harmonic, then the stronger one.
    double c;
    if (std::fabs(cents[0] - cents[1]) > 15 || amp[0] == 0 || amp[1] == 0) {
      int pick = other[0] != other[1] ? (other[0] ? 1 : 0) : (amp[0] >= amp[1] ? 0 : 1);
      c = cents[pick];
    } else
      c = (cents[0] * amp[0] * amp[0] + cents[1] * amp[1] * amp[1]) / (amp[0] * amp[0] + amp[1] * amp[1]);
    out[s].cents = (float)c;
    out[s].hz = (float)(target * std::pow(2.0, c / 1200));
    out[s].amplitude = (float)std::max(amp[0], amp[1]);
    out[s].found = out[s].amplitude >= MIN_AMPLITUDE;   // provisional, for the higher strings
    if (out[s].amplitude > strongest) strongest = out[s].amplitude;
  }
  // a string counts as found when its peak is clear: above MIN_AMPLITUDE and
  // not tiny next to the loudest string (at least 3 %)
  for (int s = 0; s < GUITAR_STRINGS; s++)
    out[s].found = out[s].amplitude >= 0.03 * strongest && out[s].amplitude >= MIN_AMPLITUDE;
}

void applyCalibration(StringReading out[GUITAR_STRINGS], const float offsets[GUITAR_STRINGS]) {
  for (int s = 0; s < GUITAR_STRINGS; s++) {
    out[s].cents -= offsets[s];
    out[s].hz *= std::pow(2.0f, -offsets[s] / 1200);
  }
}

}  // namespace dsp
