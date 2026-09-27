// Tuner helpers: guitar strings and a steady pitch for the needle.
// Pure C++ (no Arduino), unit-tested on the PC.
#pragma once
#include <cstddef>

namespace dsp {

// Standard guitar tuning, from the lowest (6th) string to the highest (1st).
constexpr int GUITAR_STRINGS = 6;
extern const int GUITAR_MIDI[GUITAR_STRINGS];   // E2 A2 D3 G3 B3 E4

struct StringMatch {
  int index;       // 0 = low E (6th string) ... 5 = high E (1st string)
  int midi;        // MIDI note of that string
  float cents;     // deviation from it; may be far beyond +-50
};

// The guitar string closest to `hz` (in musical distance).
// A low E tuned almost a semitone down is still "E -90", not "D# +10".
StringMatch nearestGuitarString(float hz, float a4 = 440.0f);

// Makes the detected pitch steady enough for a needle:
//  - onset: after silence a pitch is shown only when `confirm` readings in a
//    row agree (within 30 cents) and are clearly periodic (aperiodicity below
//    `onsetMaxAperiodicity`). The noise of a pluck or fret buzz is not shown.
//  - median of the last 5 readings throws away single wrong values,
//  - an exponential average (in cents) calms the needle down; below 100 Hz
//    (the low E string) twice as strongly: the low E is weak in a small
//    microphone and single readings scatter more,
//  - a change of more than 30 cents (another string) jumps immediately,
//  - while a note rings, a reading 2x, 3x or 4x lower (a sub-harmonic, e.g. the
//    low E string resonating while the high E decays) counts as the same note;
//    this also holds for about 1 s after the note faded out, so the echo of the
//    low E at the very end does not show up as a new note,
//  - a new pluck (level rising by 6 dB or more) starts over with the onset rule,
//  - after `maxMisses` readings without pitch the result is 0 (silence).
class PitchSmoother {
 public:
  explicit PitchSmoother(int maxMisses = 6, int confirm = 3, float onsetMaxAperiodicity = 0.10f)
      : maxMisses_(maxMisses), confirm_(confirm), onsetMaxAperiodicity_(onsetMaxAperiodicity) {}

  // Feed one reading: pitch (0 = none found), how periodic it was (YIN
  // aperiodicity, 0 = perfect) and the signal level in dB.
  // Returns the steady pitch, or 0 while there is none.
  float push(float hz, float aperiodicity = 0, float levelDb = 0);
  void reset();
  bool locked() const { return smoothed_ > 0; }
  // counts every new note (onset confirmed); the tuner uses it to know when a
  // string was just plucked and its pitch is still settling
  unsigned notes() const { return notes_; }

 private:
  static const int HISTORY = 5;
  static const int LEVELS = 4;
  static const int REMEMBER = 30;   // readings (~1 s) the faded note is remembered
  bool isOnset(float levelDb);
  float median() const;

  float history_[HISTORY] = {};
  int count_ = 0, next_ = 0;
  int misses_ = 0, maxMisses_, confirm_;
  float onsetMaxAperiodicity_;
  float smoothed_ = 0;
  float levels_[LEVELS] = {};
  int levelCount_ = 0, levelNext_ = 0;
  int onsetCooldown_ = 0;       // a rising pluck is reported only once
  unsigned notes_ = 0;
  float rememberedHz_ = 0;      // the note that faded out last
  int rememberLeft_ = 0;
};

}  // namespace dsp
