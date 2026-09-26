// Loudness measurements on blocks of samples.
// Pure C++ (no Arduino), unit-tested on the PC.
#pragma once
#include <cstddef>
#include <cstdint>

namespace dsp {

// Full scale of 16-bit samples: dBFS = 0 at this amplitude.
constexpr float FULL_SCALE = 32768.0f;
// Level reported for digital silence.
constexpr float SILENCE_DB = -120.0f;

// Converts samples to floats and subtracts their average (DC offset).
void removeDc(const int16_t *in, float *out, size_t count);

// Root mean square = the "average power" of the signal, in sample units.
float rms(const float *x, size_t count);

// Largest absolute sample value.
int peakAbs(const int16_t *x, size_t count);

// RMS in decibels relative to full scale (0 dBFS = loudest possible).
float toDbfs(float rmsValue);

}  // namespace dsp
