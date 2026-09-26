// Automatic sensitivity of the spectrum display: which levels map to the top
// and to the bottom of the bars.
//
//  - the top jumps at once to the loudest band and then comes down slowly
//    (`release` dB/s), so the bars use the whole height for loud and for
//    quiet music alike
//  - it never goes below `minTopDb`, so in silence the bars stay low
// (A version that tracked the room noise and limited the attack kept quiet
// music from a phone too dark on the real device, so it was dropped.)
// Pure C++ (no Arduino), unit-tested on the PC.
#pragma once

namespace dsp {

class AutoRange {
 public:
  AutoRange(float rangeDb, float minTopDb, float releaseDbPerS = 6)
      : range_(rangeDb), minTop_(minTopDb), release_(releaseDbPerS), top_(minTopDb) {}

  // loudest: the level of the loudest band now; dt: seconds since last update
  void update(float loudestDb, float dt);
  void setRange(float rangeDb) { range_ = rangeDb; }

  float range() const { return range_; }
  float top() const { return top_; }
  float bottom() const { return top_ - range_; }

 private:
  float range_, minTop_, release_;
  float top_;
};

}  // namespace dsp
