// Guitar intonation check: the open string (or its 12th-fret harmonic) against
// the same string pressed at the 12th fret, which must sound exactly one octave
// above the open string. Pure C++ (no Arduino), unit-tested on the PC.
#pragma once
#include <vector>

namespace dsp {

// Measures the settled pitch of one plucked note from a stream of pitch
// readings: waits for a pluck (the level jumps by 10 dB or more), ignores the
// first `fromS` seconds after it (the pluck and the sharp start of the tone)
// and takes the median of the readings up to `toS` seconds.
class NoteMeter {
 public:
  explicit NoteMeter(float fromS = 0.4f, float toS = 1.6f, int minReadings = 8)
      : from_(fromS), to_(toS), minReadings_(minReadings) {}

  enum class State { Waiting, Measuring, Done, Failed };

  void reset();
  // one reading: time in seconds (any running clock), pitch (0 = none), level
  State push(float timeS, float hz, float levelDb);
  State state() const { return state_; }
  float result() const { return result_; }       // median pitch when Done
  // 0 ... 1: how far the measurement is (for a progress bar)
  float progress(float timeS) const;

 private:
  float from_, to_;
  int minReadings_;
  State state_ = State::Waiting;
  float quietest_ = 1e9f, onset_ = 0, result_ = 0;
  int levels_ = 0;
  std::vector<float> readings_;
};

struct IntonationReference {
  int string = -1;          // 0 = low E ... 5 = high E, -1 = not a guitar string
  bool harmonic = false;    // the 12th-fret harmonic (octave) was played
  float octaveHz = 0;       // what the 12th fret should sound like
};

// Which string a reference note belongs to: an open string (within 50 cents)
// or the 12th-fret harmonic of one (one octave up). The guitar must be tuned.
IntonationReference classifyReference(float hz, float a4 = 440.0f);

// Deviation of the fretted 12th-fret note from the octave, in cents
// (+ = sharp: move the saddle back, away from the neck; - = flat: forward).
float intonationCents(float frettedHz, float octaveHz);

}  // namespace dsp
