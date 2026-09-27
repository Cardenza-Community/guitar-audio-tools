#include "wav.h"
#include <cstring>

namespace dsp {

static void put32(uint8_t *p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));   // little endian
}
static void put16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}
static uint32_t get32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

void makeWavHeader(uint8_t out[WAV_HEADER_BYTES], uint32_t sampleRate, uint32_t dataBytes) {
  std::memcpy(out, "RIFF", 4);
  put32(out + 4, 36 + dataBytes);            // size of everything after this field
  std::memcpy(out + 8, "WAVEfmt ", 8);
  put32(out + 16, 16);                       // size of the format block
  put16(out + 20, 1);                        // PCM
  put16(out + 22, 1);                        // mono
  put32(out + 24, sampleRate);
  put32(out + 28, sampleRate * 2);           // bytes per second
  put16(out + 32, 2);                        // bytes per sample frame
  put16(out + 34, 16);                       // bits per sample
  std::memcpy(out + 36, "data", 4);
  put32(out + 40, dataBytes);
}

bool readWavHeader(const uint8_t in[WAV_HEADER_BYTES], uint32_t &sampleRate, uint32_t &dataBytes) {
  if (std::memcmp(in, "RIFF", 4) || std::memcmp(in + 8, "WAVEfmt ", 8) || std::memcmp(in + 36, "data", 4))
    return false;
  if (get16(in + 20) != 1 || get16(in + 22) != 1 || get16(in + 34) != 16) return false;
  sampleRate = get32(in + 24);
  dataBytes = get32(in + 40);
  return true;
}

}  // namespace dsp
