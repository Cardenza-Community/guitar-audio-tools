#include "audio_in.h"
#include <M5Cardputer.h>
#include <freertos/stream_buffer.h>
#include <atomic>
#include "../hw/es8311.h"

namespace audio_in {

const size_t CHUNK = 256;                 // samples per Mic buffer (16 ms at 16 kHz)
const size_t STREAM_BYTES = 16384;        // 8192 samples = 0.5 s at 16 kHz

static int16_t chunks[2][CHUNK];
static StreamBufferHandle_t stream = nullptr;
static std::atomic<uint32_t> dropped{0};
static bool isRunning = false;
static uint32_t currentRate = 0;
static uint32_t startMs = 0;
static uint64_t receivedSamples = 0;

// Called by the Mic capture task whenever a buffer is full.
// Must be quick and must not wait.
static void onBufferFilled(void *, void *data, size_t length) {
  size_t bytes = length * sizeof(int16_t);
  size_t sent = xStreamBufferSend(stream, data, bytes, 0);
  if (sent < bytes) dropped += (bytes - sent) / sizeof(int16_t);
  M5Cardputer.Mic.record((int16_t *)data, length);   // queue it again
}

bool start(uint32_t sampleRate, int gainDb) {
  stop();
  if (!stream) stream = xStreamBufferCreate(STREAM_BYTES, sizeof(int16_t));
  if (!stream) return false;
  xStreamBufferReset(stream);
  dropped = 0;

  M5Cardputer.Speaker.end();

  // Unscaled samples: the library multiplies by magnification / (over_sampling * 2) = 1.
  auto cfg = M5Cardputer.Mic.config();
  cfg.sample_rate = sampleRate;
  cfg.over_sampling = 2;
  cfg.magnification = 4;
  cfg.noise_filter_level = 0;
  M5Cardputer.Mic.config(cfg);
  // The callback may only be changed while the Mic is stopped.
  M5Cardputer.Mic.setBufferReleaseCallback(nullptr, onBufferFilled);
  if (!M5Cardputer.Mic.begin()) return false;

  es8311::setPgaGain(gainDb);             // Mic.begin() reset the codec gain to 0 dB

  currentRate = sampleRate;
  receivedSamples = 0;
  startMs = millis();
  isRunning = M5Cardputer.Mic.record(chunks[0], CHUNK, sampleRate) &&
              M5Cardputer.Mic.record(chunks[1], CHUNK, sampleRate);
  return isRunning;
}

void stop() {
  if (M5Cardputer.Mic.isRunning()) M5Cardputer.Mic.end();
  isRunning = false;
}

bool running() { return isRunning; }
uint32_t sampleRate() { return currentRate; }

size_t read(int16_t *dst, size_t maxCount) {
  if (!stream) return 0;
  size_t count = xStreamBufferReceive(stream, dst, maxCount * sizeof(int16_t), 0) / sizeof(int16_t);
  receivedSamples += count;
  return count;
}

float measuredRate() {
  uint32_t elapsed = millis() - startMs;
  if (!isRunning || elapsed < 2000) return 0;
  // samples still waiting in the stream buffer were also received
  size_t waiting = xStreamBufferBytesAvailable(stream) / sizeof(int16_t);
  return (receivedSamples + waiting) * 1000.0f / elapsed;
}

uint32_t droppedSamples() { return dropped; }

}  // namespace audio_in
