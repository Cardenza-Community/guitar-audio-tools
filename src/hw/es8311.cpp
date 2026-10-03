#include "es8311.h"
#include <M5Unified.h>
#ifdef CARDENZA_TARGET
#include <cmath>
#endif

namespace es8311 {

const uint8_t ADDRESS = 0x18;        // codec address on the internal I2C bus
const uint32_t I2C_FREQ = 100000;

static int currentGain = 0;

void setPgaGain(int db) {
  db = constrain(db, 0, 30);
  int step = db / 3;                  // 0..10, 3 dB each
  // reg 0x14: bits 5:4 = input (1 = MIC1P/MIC1N), bits 3:0 = PGA gain
#ifndef CARDENZA_TARGET
  M5.In_I2C.writeRegister8(ADDRESS, 0x14, 0x10 | step, I2C_FREQ);
#endif
  currentGain = step * 3;
}

int pgaGain() { return currentGain; }

void speakerOff() {
#ifdef CARDENZA_TARGET
  // ES8156 soft mute. Do not touch the ADV ADC at 0x18.
  M5.Ex_I2C.writeRegister8(0x08, 0x13, 0x06, I2C_FREQ);
#else
  M5.In_I2C.writeRegister8(ADDRESS, 0x32, 0x00, I2C_FREQ);   // DAC volume: -95.5 dB (silent)
  M5.In_I2C.writeRegister8(ADDRESS, 0x12, 0x02, I2C_FREQ);   // PDN_DAC: DAC powered down (default)
  M5.In_I2C.writeRegister8(ADDRESS, 0x13, 0x40, I2C_FREQ);   // output drive back to default

#endif
}

// the same writes as M5Unified's _microphone_enabled_cb_cardputer_adv
static bool micCallback(void *, bool enabled) {
  if (!enabled) return true;                         // stay powered: no pop
  static const uint8_t SETUP[][2] = {
      {0x00, 0x80},   // RESET: CSM power on
      {0x01, 0xBA},   // CLOCK_MANAGER: MCLK = BCLK
      {0x02, 0x18},   // CLOCK_MANAGER: MULT_PRE = 3
      {0x0D, 0x01},   // SYSTEM: power up analog circuitry
      {0x0E, 0x02},   // SYSTEM: enable analog PGA and the ADC modulator
      {0x14, 0x10},   // ADC: Mic1p-Mic1n, PGA minimum (audio_in sets the gain)
      {0x17, 0xBF},   // ADC volume 0 dB
      {0x1C, 0x6A},   // ADC equalizer bypass, digital DC offset cancel
  };
  for (auto &r : SETUP) M5.In_I2C.writeRegister8(ADDRESS, r[0], r[1], I2C_FREQ);
  return true;
}

// Mic_Class::setCallback() is protected (M5Unified sets it for the board). A
// derived class may take its address; the pointer then works on the real Mic.
struct MicCallbackAccess : m5::Mic_Class {
  static void set(m5::Mic_Class &mic, bool (*callback)(void *, bool)) {
    (mic.*(&MicCallbackAccess::setCallback))(nullptr, callback);
  }
};

#ifdef CARDENZA_TARGET
struct SpeakerCallbackAccess : m5::Speaker_Class {
  static void set(m5::Speaker_Class &speaker, bool (*callback)(void *, bool)) {
    (speaker.*(&SpeakerCallbackAccess::setCallback))(nullptr, callback);
  }
};
static bool speakerCallback(void *, bool enabled) {
  if (!enabled) return true;
  // PDM capture shares LRCK. Restore the DAC format through the owning I2C driver.
  const uint8_t setup[][2] = {{0x11,0x30},{0x01,0xE1},{0x04,0x20},{0x05,0x01},{0x13,0x00}};
  for (auto &r : setup) {
    if (!M5.Ex_I2C.writeRegister8(0x08,r[0],r[1],I2C_FREQ)
        || M5.Ex_I2C.readRegister8(0x08,r[0],I2C_FREQ) != r[1]) return false;
  }
  return true;
}
void installQuietMicCallback() {
  MicCallbackAccess::set(M5.Mic, nullptr);
  SpeakerCallbackAccess::set(M5.Speaker, speakerCallback);
}
#else
void installQuietMicCallback() { MicCallbackAccess::set(M5.Mic, micCallback); }
#endif

uint8_t readRegister(uint8_t reg) {
#ifdef CARDENZA_TARGET
  return M5.Ex_I2C.readRegister8(0x08, reg, I2C_FREQ);
#else
  return M5.In_I2C.readRegister8(ADDRESS, reg, I2C_FREQ);
#endif
}

void printRegisters() {
  const uint8_t regs[] = {0x00, 0x01, 0x02, 0x0A, 0x0D, 0x0E, 0x14, 0x16, 0x17, 0x1C};
#ifdef CARDENZA_TARGET
  Serial.print("ES8156 DAC (microphone is PDM, gain is digital):");
#else
  Serial.print("ES8311:");
#endif
  for (uint8_t r : regs) Serial.printf(" [%02X]=%02X", r, readRegister(r));
  Serial.println();
}

}  // namespace es8311
