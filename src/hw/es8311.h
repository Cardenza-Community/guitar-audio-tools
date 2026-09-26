// ES8311 audio codec on the Cardputer ADV.
//
// The ADV microphone is not wired to the ESP32-S3 directly: it goes through
// the ES8311 codec, which is configured over the internal I2C bus (address 0x18)
// and streams samples to the ESP32 over I2S.
//
// Signal path inside the codec:
//   mic -> PGA (analog gain 0..30 dB) -> ADC -> high-pass filter (removes DC)
//       -> digital volume -> I2S -> ESP32
//
// M5Cardputer.Mic.begin() powers the codec up with the PGA at its minimum
// (0 dB); audio_in calls setPgaGain() again after every Mic.begin().
// Register numbers and meanings come from the ES8311 User Guide Rev 1.11.
#pragma once
#include <Arduino.h>

namespace es8311 {

// Analog microphone gain (PGA): 0, 3, 6 ... 30 dB.
// Amplifying before the ADC is better than multiplying samples in software,
// which would amplify the converter noise as well.
void setPgaGain(int db);
int pgaGain();

uint8_t readRegister(uint8_t reg);
void printRegisters();   // dumps the important registers to Serial

}  // namespace es8311
