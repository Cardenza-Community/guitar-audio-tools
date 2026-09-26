#include "auto_range.h"
#include <algorithm>

namespace dsp {

void AutoRange::update(float loudestDb, float dt) {
  top_ = std::max({loudestDb, top_ - release_ * dt, minTop_});
}

}  // namespace dsp
