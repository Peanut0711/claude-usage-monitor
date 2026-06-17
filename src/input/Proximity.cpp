// ============================================================================
//  Proximity.cpp
// ============================================================================
#include "Proximity.h"

#include <Arduino.h>
#include <SensorLTR553.hpp>
#include <Wire.h>

#include "../board_pins.h"

namespace {
SensorLTR553 ltr;
bool     gOk    = false;
int      gRaw   = 0;
bool     gNear  = false;
uint32_t gLast  = 0;

constexpr uint32_t POLL_MS = 120;   // throttle I2C reads while polling
constexpr int NEAR_TH = 30;         // raw >= this -> "near"  (hand at target dist ~35-68; floor 0)
constexpr int FAR_TH  = 12;         // raw <= this -> "far"   (hysteresis gap)
}  // namespace

namespace prox {

bool begin() {
    pinMode(TDS3_PIN_SENSOR_IRQ, INPUT_PULLUP);
    gOk = ltr.begin(Wire, LTR553_SLAVE_ADDRESS, TDS3_PIN_I2C_SDA, TDS3_PIN_I2C_SCL);
    if (!gOk) {
        Serial.println("[prox] LTR-553 not found");
        return false;
    }
    // Proximity channel only (no ALS): max LED drive (100mA, 8 pulses) for a
    // few-cm reach -- the bare 50mA/1-pulse default only reads ~50 at ~1cm.
    ltr.setPsLedPulsePeriod(SensorLTR553::PS_LED_PLUSE_60KHZ);
    ltr.setPsLedDutyCycle(SensorLTR553::PS_LED_DUTY_100);
    ltr.setPsLedCurrent(SensorLTR553::PS_LED_CUR_100MA);
    ltr.setProximityRate(SensorLTR553::PS_MEAS_RATE_100MS);
    ltr.setPsLedPulses(8);
    ltr.enablePsIndicator();
    ltr.enableProximity();
    Serial.println("[prox] LTR-553 proximity enabled");
    return true;
}

bool available() { return gOk; }

int raw() { return gOk ? gRaw : -1; }

bool near() {
    if (!gOk) return false;
    uint32_t now = millis();
    if (now - gLast >= POLL_MS) {
        gLast = now;
        bool sat = false;
        int p = ltr.getProximity(&sat);
        if (p >= 0) {                          // ignore I2C read errors
            gRaw = sat ? 2047 : p;             // saturation = very close
            if      (!gNear && gRaw >= NEAR_TH) gNear = true;
            else if ( gNear && gRaw <= FAR_TH)  gNear = false;
        }
    }
    return gNear;
}

}  // namespace prox
