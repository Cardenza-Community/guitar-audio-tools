// Sound level meter core: weighting + time averaging (IEC 61672 style).
//
//  - Fast (125 ms) and Slow (1 s): exponential averages of the squared signal,
//    what a hand-held sound level meter shows.
//  - Leq: "equivalent continuous level" = the average energy since reset.
//    Two sounds at 60 and 80 dB give Leq of 77 dB, not 70: energy counts.
//  - Max / Min: highest / lowest Fast level since reset.
//
// All levels are in dBFS of the weighted signal. The app adds the microphone
// calibration to get dB SPL. Pure C++ (no Arduino), unit-tested on the PC.
#pragma once
#include <cstddef>
#include "level.h"
#include <cstdint>
#include "weighting.h"

namespace dsp {

class SoundLevelMeter {
 public:
  SoundLevelMeter(float sampleRate, Weighting weighting);

  // `scale` multiplies the samples first. The decibel meter uses it to undo a
  // change of the microphone gain, so the levels continue seamlessly.
  void process(const int16_t *samples, size_t count, float scale = 1.0f);
  void reset();                      // restart Leq, Max and Min
  void setWeighting(Weighting w);    // also resets
  Weighting weighting() const { return filter_.type(); }

  float fastDb() const;
  float slowDb() const;
  float leqDb() const;
  float maxDb() const { return maxDb_; }
  // SILENCE_DB until something was measured
  float minDb() const { return minDb_ == NO_MIN ? SILENCE_DB : minDb_; }
  float seconds() const { return (float)(leqCount_ / sampleRate_); }

 private:
  float sampleRate_;
  WeightingFilter filter_;
  float fastAlpha_, slowAlpha_;
  float fastPower_ = 0, slowPower_ = 0;
  double leqSum_ = 0;
  uint64_t leqCount_ = 0;
  static constexpr float NO_MIN = 1e9f;
  float maxDb_, minDb_;
  uint32_t settle_ = 0;              // samples to skip in Max/Min after a reset
  uint32_t sinceCheck_ = 0;
};

}  // namespace dsp
