#include "tempo.h"
#include <algorithm>
#include <cmath>

namespace dsp {

TempoEstimator::TempoEstimator(float frameRate, float seconds, float minBpm, float maxBpm, float minSeconds)
    : frameRate_(frameRate), minBpm_(minBpm), maxBpm_(maxBpm), history_((size_t)(seconds * frameRate), 0.0f),
      minCount_((size_t)(minSeconds * frameRate)) {}

void TempoEstimator::push(float onsetStrength) {
  history_[pos_] = onsetStrength;
  pos_ = (pos_ + 1) % history_.size();
  if (count_ < history_.size()) count_++;
}

float TempoEstimator::autocorrelation(const std::vector<float> &x, int lag) const {
  float sum = 0;
  for (size_t i = lag; i < x.size(); i++) sum += x[i] * x[i - lag];
  return sum / (x.size() - lag);            // per pair, so long lags are not penalised
}

TempoEstimator::Result TempoEstimator::estimate() const {
  Result r;
  if (!ready()) return r;
  // the collected envelope (up to history_.size() values) in time order,
  // without its average (only the ups and downs matter)
  const size_t size = history_.size();
  const size_t n = count_;
  std::vector<float> x(n);
  size_t oldest = (pos_ + size - n) % size;
  float mean = 0;
  for (size_t i = 0; i < n; i++) mean += history_[(oldest + i) % size];
  mean /= n;
  for (size_t i = 0; i < n; i++) x[i] = history_[(oldest + i) % size] - mean;
  // smooth the envelope ([1 2 1] twice, about 5 frames wide): a sharp hit is
  // only one frame wide, and a beat is rarely a whole number of frames long
  // (120 BPM = 62.5 frames), so sharp hits would not line up at 62 or 63
  for (int pass = 0; pass < 2; pass++) {
    std::vector<float> y(x);
    for (size_t i = 1; i + 1 < n; i++) y[i] = 0.25f * x[i - 1] + 0.5f * x[i] + 0.25f * x[i + 1];
    x.swap(y);
  }
  float energy = autocorrelation(x, 0);
  if (energy <= 0) return r;

  // candidate beat lengths in frames, in quarter-frame steps
  const float minLag = 60 * frameRate_ / maxBpm_;
  const float maxLag = 60 * frameRate_ / minBpm_;

  // Pulse comb: for every beat length, lay a "ruler" of equally spaced marks
  // over the envelope, slide it to the position where the marks hit the
  // strongest onsets, and take the average onset strength under the marks.
  // At the true beat the marks hit every beat; at 1.5 beats they alternate
  // between beats and weaker off-beats, so the average is lower.
  // Computed also for half and double beat lengths (the tempo "family").
  const float step = 0.25f;
  const float lowLag = minLag / 2, highLag = std::min(maxLag * 2, (float)n / 3);
  std::vector<float> comb;
  for (float lag = lowLag; lag <= highLag; lag += step) {
    float bestPhase = -1e30f;
    for (int phase = 0; phase < (int)lag; phase++) {
      float sum = 0;
      int marks = 0;
      for (float t = phase; t < n; t += lag, marks++) sum += x[(size_t)t];
      bestPhase = std::max(bestPhase, sum / marks);
    }
    comb.push_back(bestPhase);
  }
  auto combAt = [&](float lag) -> float {
    long i = std::lround((lag - lowLag) / step);
    return i >= 0 && i < (long)comb.size() ? comb[i] : 0.0f;
  };

  // Score: the tempo itself plus half of its "family" (half and double tempo:
  // bars and eighth notes line up with a true beat too), all weighted
  // towards 120 BPM. This keeps a clear beat from being read at half tempo.
  float bestLag = 0, bestScore = 0;
  for (float lag = minLag; lag <= maxLag; lag += step) {
    float score = combAt(lag) + 0.5f * std::max(0.0f, combAt(2 * lag)) + 0.5f * std::max(0.0f, combAt(lag / 2));
    float bpm = 60 * frameRate_ / lag;
    float octaves = std::log2(bpm / 120.0f);
    score *= std::exp(-0.5f * octaves * octaves);   // prefer ~120 BPM
    if (score > bestScore) {
      bestScore = score;
      bestLag = lag;
    }
  }
  if (bestLag == 0) return r;
  int best = (int)std::lround(bestLag);

  // refinement: the best lag again over k beats, parabola through 3 points
  // (the parabola's shift is limited to +-1 frame: without a real peak, e.g.
  // in near-silence, it could otherwise land anywhere, even below zero)
  auto refined = [&](int around) {
    float a = autocorrelation(x, around - 1), b = autocorrelation(x, around), c = autocorrelation(x, around + 1);
    float den = a - 2 * b + c;
    float shift = den != 0 ? (a - c) / (2 * den) : 0.0f;
    return around + std::max(-1.0f, std::min(1.0f, shift));
  };
  // the local autocorrelation peak next to the comb's beat length
  for (int d = -1; d <= 1; d += 2)
    if (autocorrelation(x, best + d) > autocorrelation(x, best)) best += d;
  float beat = refined(best);
  for (int k = 4; k >= 2; k--) {
    int around = (int)std::lround(k * beat);
    if (around + 2 >= (int)n / 2) continue;      // keep enough overlap
    // look for the actual peak next to k * beat
    int peak = around;
    for (int d = -2; d <= 2; d++)
      if (autocorrelation(x, around + d) > autocorrelation(x, peak)) peak = around + d;
    beat = refined(peak) / k;
    break;
  }
  // the refinement must stay near the comb's beat; otherwise trust the comb
  if (!(beat > 0.9f * bestLag && beat < 1.1f * bestLag)) beat = bestLag;
  r.bpm = 60 * frameRate_ / beat;
  r.confidence = std::max(0.0f, std::min(1.0f, autocorrelation(x, best) / energy));
  return r;
}

float TapTempo::tap(uint32_t ms) {
  if (!times_.empty() && ms - times_.back() > 2000) times_.clear();   // a pause: new series
  times_.push_back(ms);
  if (times_.size() > 9) times_.erase(times_.begin());
  if (times_.size() < 3) {
    bpm_ = 0;
    return bpm_;
  }
  std::vector<uint32_t> intervals;
  for (size_t i = 1; i < times_.size(); i++) intervals.push_back(times_[i] - times_[i - 1]);
  std::sort(intervals.begin(), intervals.end());
  float median = intervals.size() % 2 ? intervals[intervals.size() / 2]
                                      : 0.5f * (intervals[intervals.size() / 2 - 1] + intervals[intervals.size() / 2]);
  bpm_ = 60000.0f / median;
  return bpm_;
}

}  // namespace dsp
