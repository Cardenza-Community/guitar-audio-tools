#include "es8311.h"
#include <M5Unified.h>

namespace es8311 {

const uint8_t ADDRESS = 0x18;        // codec address on the internal I2C bus
const uint32_t I2C_FREQ = 100000;

static int currentGain = 0;

void setPgaGain(int db) {
  db = constrain(db, 0, 30);
  int step = db / 3;                  // 0..10, 3 dB each
  // reg 0x14: bits 5:4 = input (1 = MIC1P/MIC1N), bits 3:0 = PGA gain
  if (M5.getBoard() != m5::board_t::board_M5Cardputer)
    M5.In_I2C.writeRegister8(ADDRESS, 0x14, 0x10 | step, I2C_FREQ);
  currentGain = step * 3;
}

int pgaGain() { return currentGain; }

void speakerOff() {
  if (M5.getBoard() == m5::board_t::board_M5Cardputer) {
    M5.Speaker.end(); // Fork callback mutes ES8156; PDM mic owns I2S.
    return;
  }
  M5.In_I2C.writeRegister8(ADDRESS, 0x32, 0x00, I2C_FREQ);   // DAC volume: -95.5 dB (silent)
  M5.In_I2C.writeRegister8(ADDRESS, 0x12, 0x02, I2C_FREQ);   // PDN_DAC: DAC powered down (default)
  M5.In_I2C.writeRegister8(ADDRESS, 0x13, 0x40, I2C_FREQ);   // output drive back to default

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

void installQuietMicCallback() {
  // Keep the fork's ES8156 Speaker callback and original PDM mic intact.
  if (M5.getBoard() != m5::board_t::board_M5Cardputer)
    MicCallbackAccess::set(M5.Mic, micCallback);
}

uint8_t readRegister(uint8_t reg) {
  if (M5.isCardenza()) return M5.Ex_I2C.readRegister8(0x08, reg, I2C_FREQ);
  return M5.getBoard() != m5::board_t::board_M5Cardputer
      ? M5.In_I2C.readRegister8(ADDRESS, reg, I2C_FREQ) : 0;
}

void printRegisters() {
  const uint8_t regs[] = {0x00, 0x01, 0x02, 0x0A, 0x0D, 0x0E, 0x14, 0x16, 0x17, 0x1C};
  if (M5.getBoard() == m5::board_t::board_M5Cardputer) {
    Serial.println("PDM microphone; digital gain (no ADC codec registers)");
    return;
  }
  Serial.print("ES8311:");
  for (uint8_t r : regs) Serial.printf(" [%02X]=%02X", r, readRegister(r));
  Serial.println();
}

}  // namespace es8311
