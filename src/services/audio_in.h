// Gapless microphone input.
//
// While started, the microphone records all the time: two small buffers are
// queued in M5Cardputer.Mic; each filled buffer is copied into a FreeRTOS
// stream buffer (about 0.5 s of sound) and queued again. The main loop takes
// the samples out with read(). No sound is lost between blocks as long as the
// main loop reads faster than the microphone produces (droppedSamples() = 0).
#pragma once
#include <Arduino.h>

namespace audio_in {

// Starts recording at `sampleRate` Hz with analog gain `gainDb` (0..30 dB).
// Stops the speaker (it shares the I2S bus with the microphone).
bool start(uint32_t sampleRate, int gainDb);
void stop();
bool running();
uint32_t sampleRate();

// Copies up to `maxCount` waiting samples into `dst`, returns how many.
// Never waits: returns 0 when nothing is ready.
size_t read(int16_t *dst, size_t maxCount);

// Samples thrown away because the stream buffer was full (main loop too slow).
uint32_t droppedSamples();

// Real sample rate in samples per second, measured against the CPU clock
// (0 until about 4 s after start(); gets more precise the longer it runs).
double measuredRate();

// measuredRate() / nominal rate, or 1 while not measured yet.
// Frequencies computed with the nominal rate can be multiplied by this.
// Measured on a Cardputer ADV: -15 ppm (0.03 cents), so it hardly matters,
// but it keeps the tuner exact should another board or core differ more.
float rateCorrection();

}  // namespace audio_in
