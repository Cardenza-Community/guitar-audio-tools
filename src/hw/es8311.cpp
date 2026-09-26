#include "es8311.h"
#include <M5Cardputer.h>

namespace es8311 {

const uint8_t ADDRESS = 0x18;        // codec address on the internal I2C bus
const uint32_t I2C_FREQ = 100000;

static int currentGain = 0;

void setPgaGain(int db) {
  db = constrain(db, 0, 30);
  int step = db / 3;                  // 0..10, 3 dB each
  // reg 0x14: bits 5:4 = input (1 = MIC1P/MIC1N), bits 3:0 = PGA gain
  M5.In_I2C.writeRegister8(ADDRESS, 0x14, 0x10 | step, I2C_FREQ);
  currentGain = step * 3;
}

int pgaGain() { return currentGain; }

uint8_t readRegister(uint8_t reg) {
  return M5.In_I2C.readRegister8(ADDRESS, reg, I2C_FREQ);
}

void printRegisters() {
  const uint8_t regs[] = {0x00, 0x01, 0x02, 0x0A, 0x0D, 0x0E, 0x14, 0x16, 0x17, 0x1C};
  Serial.print("ES8311:");
  for (uint8_t r : regs) Serial.printf(" [%02X]=%02X", r, readRegister(r));
  Serial.println();
}

}  // namespace es8311
