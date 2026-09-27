#include "tuning.h"
#include <algorithm>
#include <cmath>
#include "notes.h"

namespace dsp {

const int GUITAR_MIDI[GUITAR_STRINGS] = {40, 45, 50, 55, 59, 64};

static float centsBetween(float hz, float reference) { return 1200 * std::log2(hz / reference); }

// If hz is 1/2, 1/3 or 1/4 of the reference note, or 2, 3 or 4 times it
// (within 25 cents), returns the reference note's frequency range reading
// (hz scaled back); otherwise returns hz unchanged.
static float undoHarmonics(float hz, float reference) {
  if (hz <= 0 || reference <= 0) return hz;
  for (int k = 2; k <= 4; k++) {
    if (std::fabs(centsBetween(hz * k, reference)) < 25) return hz * k;   // sub-harmonic
    if (std::fabs(centsBetween(hz / k, reference)) < 25) return hz / k;   // harmonic
  }
  return hz;
}

StringMatch nearestGuitarString(float hz, float a4) {
  StringMatch best{0, GUITAR_MIDI[0], 0};
  float bestDistance = 1e9f;
  for (int i = 0; i < GUITAR_STRINGS; i++) {
    float cents = centsBetween(hz, frequencyOfMidi(GUITAR_MIDI[i], a4));
    if (std::fabs(cents) < bestDistance) {
      bestDistance = std::fabs(cents);
      best = {i, GUITAR_MIDI[i], cents};
    }
  }
  return best;
}

void PitchSmoother::reset() {
  count_ = next_ = misses_ = 0;
  smoothed_ = 0;
}

// True when the level jumps 6 dB above the quietest of the last readings.
bool PitchSmoother::isOnset(float levelDb) {
  float quietest = levelDb;
  for (int i = 0; i < levelCount_; i++) quietest = std::min(quietest, levels_[i]);
  bool onset = levelCount_ == LEVELS && levelDb - quietest >= 6 && onsetCooldown_ == 0;
  if (onset) onsetCooldown_ = LEVELS;           // the attack rises over several readings
  else if (onsetCooldown_ > 0) onsetCooldown_--;
  levels_[levelNext_] = levelDb;
  levelNext_ = (levelNext_ + 1) % LEVELS;
  if (levelCount_ < LEVELS) levelCount_++;
  return onset;
}

float PitchSmoother::median() const {
  float sorted[HISTORY];
  std::copy(history_, history_ + count_, sorted);
  std::sort(sorted, sorted + count_);
  return sorted[count_ / 2];
}

float PitchSmoother::push(float hz, float aperiodicity, float levelDb) {
  if (isOnset(levelDb)) {                        // a new pluck: confirm again
    reset();
    rememberedHz_ = 0;
  }

  if (!locked()) {
    // shortly after a note faded out, its sub-harmonics still belong to it
    if (rememberLeft_ > 0) {
      rememberLeft_--;
      hz = undoHarmonics(hz, rememberedHz_);
    }
    // onset: collect agreeing, clearly periodic readings
    bool good = hz > 0 && aperiodicity < onsetMaxAperiodicity_;
    if (!good || (count_ > 0 && std::fabs(centsBetween(hz, history_[(next_ + HISTORY - 1) % HISTORY])) > 30)) {
      count_ = next_ = 0;                        // start collecting again
      if (!good) return 0;
    }
    history_[next_] = hz;
    next_ = (next_ + 1) % HISTORY;
    count_++;
    if (count_ < confirm_) return 0;
    smoothed_ = median();
    notes_++;
    return smoothed_;
  }

  if (hz <= 0) {
    if (++misses_ >= maxMisses_) {             // the note faded out
      rememberedHz_ = smoothed_;
      rememberLeft_ = REMEMBER;
      reset();
    }
    return smoothed_;
  }
  misses_ = 0;

  // a sub-harmonic or harmonic of the ringing note counts as the note itself
  hz = undoHarmonics(hz, smoothed_);

  history_[next_] = hz;
  next_ = (next_ + 1) % HISTORY;
  if (count_ < HISTORY) count_++;
  float m = median();

  if (std::fabs(centsBetween(m, smoothed_)) > 30) {
    smoothed_ = m;                               // another note: jump there
  } else {
    // move 30 % of the way (15 % below 100 Hz), in cents (musical steps)
    float step = smoothed_ < 100 ? 0.15f : 0.3f;
    smoothed_ *= std::pow(2.0f, step * centsBetween(m, smoothed_) / 1200);
  }
  return smoothed_;
}

}  // namespace dsp
