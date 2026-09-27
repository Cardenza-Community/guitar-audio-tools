#include "level.h"
#include <cmath>
#include <cstdlib>

namespace dsp {

void removeDc(const int16_t *in, float *out, size_t count) {
  if (count == 0) return;
  double sum = 0;
  for (size_t i = 0; i < count; i++) sum += in[i];
  float mean = (float)(sum / count);
  for (size_t i = 0; i < count; i++) out[i] = in[i] - mean;
}

float rms(const float *x, size_t count) {
  if (count == 0) return 0;
  double sum = 0;
  for (size_t i = 0; i < count; i++) sum += (double)x[i] * x[i];
  return (float)std::sqrt(sum / count);
}

int peakAbs(const int16_t *x, size_t count) {
  int peak = 0;
  for (size_t i = 0; i < count; i++) {
    int v = std::abs((int)x[i]);
    if (v > peak) peak = v;
  }
  return peak;
}

float toDbfs(float rmsValue) {
  if (rmsValue <= 0) return SILENCE_DB;
  float db = 20.0f * std::log10(rmsValue / FULL_SCALE);
  return db < SILENCE_DB ? SILENCE_DB : db;
}

float normalizeGain(int peak, float targetPeak, float maxGainDb) {
  float maxGain = std::pow(10.0f, maxGainDb / 20);
  if (peak <= 0) return maxGain;
  float gain = targetPeak / peak;
  if (gain > maxGain) gain = maxGain;
  return gain < 1 ? 1 : gain;
}

void applyGain(int16_t *x, size_t count, float gain) {
  for (size_t i = 0; i < count; i++) {
    float v = x[i] * gain;
    x[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : std::lround(v));
  }
}

}  // namespace dsp
