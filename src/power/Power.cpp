// ============================================================================
//  Power.cpp
// ============================================================================
#include "Power.h"

#include <XPowersLib.h>

#include "../board_pins.h"

namespace {
PowersSY6970 pmu(Wire, TDS3_PIN_I2C_SDA, TDS3_PIN_I2C_SCL, 0x6A);
bool gOk = false;

// Below this the cell is considered absent (running on USB only).
constexpr uint16_t BATT_ABSENT_MV = 2800;

// Rough Li-ion (1S) open-circuit voltage -> charge curve, mV : percent.
struct VP { uint16_t mv; uint8_t pct; };
const VP CURVE[] = {
    {4200, 100}, {4100, 95}, {4000, 87}, {3900, 77}, {3850, 70},
    {3800, 62},  {3750, 55}, {3700, 47}, {3650, 40}, {3600, 32},
    {3500, 18},  {3400, 8},  {3300, 2},  {3000, 0},
};

int curvePercent(uint16_t mv) {
    if (mv >= CURVE[0].mv) return 100;
    const int n = sizeof(CURVE) / sizeof(CURVE[0]);
    if (mv <= CURVE[n - 1].mv) return 0;
    for (int i = 0; i < n - 1; i++) {
        if (mv <= CURVE[i].mv && mv >= CURVE[i + 1].mv) {
            // Linear interpolate between the two bracketing points.
            int dv = CURVE[i].mv - CURVE[i + 1].mv;
            int dp = CURVE[i].pct - CURVE[i + 1].pct;
            return CURVE[i + 1].pct + (int)((long)(mv - CURVE[i + 1].mv) * dp / dv);
        }
    }
    return 0;
}
}  // namespace

namespace power {

bool begin() {
    gOk = pmu.init();
    if (gOk) pmu.enableMeasure();   // continuous ADC
    return gOk;
}

bool available() { return gOk; }

int percent() {
    if (!gOk) return 100;
    uint16_t mv = pmu.getBattVoltage();
    if (mv < BATT_ABSENT_MV) return 100;   // no battery -> USB powered
    return curvePercent(mv);
}

bool charging() {
    if (!gOk) return true;
    return pmu.isCharging() || pmu.isVbusIn();
}

}  // namespace power
