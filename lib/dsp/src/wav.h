// WAV file header: 16-bit PCM, mono. The recorder writes it with a data size of
// 0 first and rewrites it with the real size when the recording stops.
// Pure C++ (no Arduino), unit-tested on the PC.
#pragma once
#include <cstddef>
#include <cstdint>

namespace dsp {

constexpr size_t WAV_HEADER_BYTES = 44;

void makeWavHeader(uint8_t out[WAV_HEADER_BYTES], uint32_t sampleRate, uint32_t dataBytes);

// Reads a header written by makeWavHeader (or any plain 44-byte PCM header).
// Returns false when it is not a 16-bit mono PCM WAV file.
bool readWavHeader(const uint8_t in[WAV_HEADER_BYTES], uint32_t &sampleRate, uint32_t &dataBytes);

}  // namespace dsp
