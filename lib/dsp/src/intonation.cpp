#include "intonation.h"
#include <algorithm>
#include <cmath>
#include "notes.h"
#include "tuning.h"

namespace dsp {

void NoteMeter::reset() {
  state_ = State::Waiting;
  quietest_ = 1e9f;
  levels_ = 0;
  readings_.clear();
  result_ = 0;
}

NoteMeter::State NoteMeter::push(float timeS, float hz, float levelDb) {
  if (state_ == State::Waiting) {
    // a pluck: the level jumps 10 dB above the quietest level heard so far
    if (levels_ > 3 && levelDb - quietest_ >= 10) {
      state_ = State::Measuring;
      onset_ = timeS;
      readings_.clear();
    }
    quietest_ = std::min(quietest_, levelDb);
    levels_++;
    return state_;
  }
  if (state_ != State::Measuring) return state_;
  float since = timeS - onset_;
  if (since >= from_ && since <= to_ && hz > 0) readings_.push_back(hz);
  if (since > to_) {
    if ((int)readings_.size() < minReadings_) {
      state_ = State::Failed;                 // the tone was too short or unclear
    } else {
      std::sort(readings_.begin(), readings_.end());
      result_ = readings_[readings_.size() / 2];
      state_ = State::Done;
    }
  }
  return state_;
}

float NoteMeter::progress(float timeS) const {
  if (state_ == State::Done) return 1;
  if (state_ != State::Measuring) return 0;
  return std::min(1.0f, (timeS - onset_) / to_);
}

IntonationReference classifyReference(float hz, float a4) {
  IntonationReference r;
  for (int s = 0; s < GUITAR_STRINGS; s++) {
    float open = frequencyOfMidi(GUITAR_MIDI[s], a4);
    if (std::fabs(1200 * std::log2(hz / open)) < 50) {
      r.string = s;
      r.harmonic = false;
      r.octaveHz = 2 * hz;                    // the octave of what was played
      return r;
    }
    if (std::fabs(1200 * std::log2(hz / (2 * open))) < 50) {
      r.string = s;
      r.harmonic = true;
      r.octaveHz = hz;                        // the harmonic is the octave itself
      return r;
    }
  }
  return r;
}

float intonationCents(float frettedHz, float octaveHz) { return 1200 * std::log2(frettedHz / octaveHz); }

}  // namespace dsp
