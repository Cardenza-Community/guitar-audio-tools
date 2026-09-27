#include "sound_level.h"
#include <cmath>
#include "level.h"

namespace dsp {

static float powerToDb(double power) {
  // power of a full-scale sample (32768^2) = 0 dBFS
  if (power <= 0) return SILENCE_DB;
  float db = (float)(10 * std::log10(power / ((double)FULL_SCALE * FULL_SCALE)));
  return db < SILENCE_DB ? SILENCE_DB : db;
}

SoundLevelMeter::SoundLevelMeter(float sampleRate, Weighting weighting)
    : sampleRate_(sampleRate), filter_(weighting, sampleRate) {
  // exponential averaging: each sample moves the average by alpha towards it
  fastAlpha_ = 1 - std::exp(-1 / (0.125f * sampleRate));
  slowAlpha_ = 1 - std::exp(-1 / (1.0f * sampleRate));
  reset();
}

void SoundLevelMeter::reset() {
  leqSum_ = 0;
  leqCount_ = 0;
  maxDb_ = SILENCE_DB;
  minDb_ = NO_MIN;          // the first measured value becomes the minimum
  settle_ = (uint32_t)(0.5f * sampleRate_);   // let Fast settle before Max/Min
}

void SoundLevelMeter::setWeighting(Weighting w) {
  if (w == filter_.type()) return;
  filter_ = WeightingFilter(w, sampleRate_);
  fastPower_ = slowPower_ = 0;
  reset();
}

void SoundLevelMeter::process(const int16_t *samples, size_t count, float scale) {
  const uint32_t checkEvery = (uint32_t)(sampleRate_ / 100);   // Max/Min 100x per second
  for (size_t i = 0; i < count; i++) {
    float y = filter_.process(samples[i] * scale);
    float p = y * y;
    fastPower_ += fastAlpha_ * (p - fastPower_);
    slowPower_ += slowAlpha_ * (p - slowPower_);
    leqSum_ += p;
    leqCount_++;
    if (settle_ > 0) {
      settle_--;
      continue;
    }
    if (++sinceCheck_ >= checkEvery) {
      sinceCheck_ = 0;
      float fast = fastDb();
      if (fast > maxDb_) maxDb_ = fast;
      if (fast < minDb_) minDb_ = fast;
    }
  }
}

float SoundLevelMeter::fastDb() const { return powerToDb(fastPower_); }
float SoundLevelMeter::slowDb() const { return powerToDb(slowPower_); }
float SoundLevelMeter::leqDb() const {
  return leqCount_ ? powerToDb(leqSum_ / leqCount_) : SILENCE_DB;
}

}  // namespace dsp
