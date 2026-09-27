// Tempo (BPM) of music from its onset strength envelope, and tap tempo.
// Pure C++ (no Arduino), unit-tested on the PC.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dsp {

// Tempo from the onset envelope of the last few seconds.
//
// Autocorrelation: the envelope is compared with itself shifted by `lag`
// frames; when the lag equals one beat, the drum hits line up. The same idea
// as YIN for pitch, only with rhythm instead of the sound wave. The envelope
// is smoothed first, so hits line up also when a beat is not a whole number
// of frames long.
// Choosing the beat: a pulse comb (a ruler of equally spaced marks slid over
// the envelope) finds the beat length whose marks hit the strongest onsets on
// average. At the true beat the marks hit every beat; at 1.5 beats they
// alternate between beats and weaker off-beats. A tempo also gets half the
// comb value of its half and double tempo (bars and eighth notes line up with
// a true beat), which keeps real music from being read at half tempo. Between
// double and half tempo, tempos around 120 BPM are preferred, like most BPM
// meters do (a log-normal weight, one octave wide).
// The autocorrelation then refines the chosen beat length.
// Refinement: the beat length is measured again over 2 ... 4 beats (the same
// trick as in the pitch detector), which makes it several times finer.
class TempoEstimator {
 public:
  // seconds: the longest stretch of envelope used; minSeconds: the first
  // estimate is made after this much (the window then grows up to `seconds`)
  TempoEstimator(float frameRate, float seconds = 6, float minBpm = 60, float maxBpm = 200,
                 float minSeconds = 3);

  void push(float onsetStrength);
  bool ready() const { return count_ >= minCount_; }

  struct Result {
    float bpm = 0;          // 0 = no clear tempo
    float confidence = 0;   // 0 ... 1: how strongly the rhythm repeats
  };
  Result estimate() const;

 private:
  float autocorrelation(const std::vector<float> &x, int lag) const;
  float frameRate_, minBpm_, maxBpm_;
  std::vector<float> history_;   // ring buffer
  size_t pos_ = 0, count_ = 0, minCount_;
};

// Tap tempo: BPM from the times of the last taps (key presses or claps).
// A pause longer than 2 s starts a new series; the median of the last up to
// 8 intervals is used, so one missed or double tap does not spoil it.
class TapTempo {
 public:
  // time of the tap in milliseconds; returns the current BPM (0 = need more taps)
  float tap(uint32_t ms);
  float bpm() const { return bpm_; }
  int taps() const { return (int)times_.size(); }
  void reset() { times_.clear(); bpm_ = 0; }

 private:
  std::vector<uint32_t> times_;
  float bpm_ = 0;
};

}  // namespace dsp
