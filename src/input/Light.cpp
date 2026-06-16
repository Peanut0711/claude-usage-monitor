// ============================================================================
//  Light.cpp  -  minimal LTR-553ALS driver (ALS only, polled)
// ============================================================================
#include "Light.h"

#include <Wire.h>

#include "../board_pins.h"

namespace {
constexpr uint8_t LTR553_ADDR     = 0x23;
constexpr uint8_t REG_ALS_CONTR   = 0x80;  // bit0 = ALS active
constexpr uint8_t REG_ALS_CH0_0   = 0x8A;  // CH0 (visible+IR) low byte
bool gOk = false;

bool writeReg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(LTR553_ADDR);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}
}  // namespace

namespace light {

bool begin() {
    Wire.begin(TDS3_PIN_I2C_SDA, TDS3_PIN_I2C_SCL);   // idempotent
    gOk = writeReg(REG_ALS_CONTR, 0x01);              // ALS active, gain 1x
    delay(10);                                         // wake-up time
    return gOk;
}

bool available() { return gOk; }

uint16_t raw() {
    if (!gOk) return 0;
    Wire.beginTransmission(LTR553_ADDR);
    Wire.write(REG_ALS_CH0_0);
    if (Wire.endTransmission(false) != 0) return 0;
    if (Wire.requestFrom((int)LTR553_ADDR, 2) < 2) return 0;
    uint8_t lo = Wire.read();
    uint8_t hi = Wire.read();
    return (uint16_t)((hi << 8) | lo);
}

}  // namespace light
