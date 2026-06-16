// ============================================================================
//  Power.cpp
// ============================================================================
#include "Power.h"

#include <Arduino.h>
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

// Slow, long-window voltage sampler. One sample every SAMPLE_MS; percent() then
// uses the MEDIAN of the last SAMPLES, which rejects ADC spikes and averages
// over ~SAMPLES*SAMPLE_MS. No need for fast ADC.
constexpr int      SAMPLES   = 16;       // ~24 s window at 1.5 s
constexpr uint32_t SAMPLE_MS = 1500;
uint16_t gSamples[SAMPLES];
int      gCount = 0;
int      gIdx   = 0;
uint32_t gLastSample = 0;

void takeSample() {
    uint16_t mv = pmu.getBattVoltage();
    if (mv == 0) return;                 // ignore obviously-bad reads
    gSamples[gIdx] = mv;
    gIdx = (gIdx + 1) % SAMPLES;
    if (gCount < SAMPLES) gCount++;
}

uint16_t medianVoltage() {
    if (gCount == 0) takeSample();
    if (gCount == 0) return 0;
    uint16_t tmp[SAMPLES];
    for (int i = 0; i < gCount; i++) tmp[i] = gSamples[i];
    for (int i = 1; i < gCount; i++) {   // insertion sort (small n)
        uint16_t v = tmp[i];
        int j = i - 1;
        while (j >= 0 && tmp[j] > v) { tmp[j + 1] = tmp[j]; j--; }
        tmp[j + 1] = v;
    }
    return tmp[gCount / 2];
}
}  // namespace

namespace power {

bool begin() {
    gOk = pmu.init();
    if (gOk) pmu.enableMeasure();   // continuous ADC
    return gOk;
}

bool available() { return gOk; }

// Call frequently from the main loop; it self-throttles to one slow sample.
void update() {
    if (!gOk) return;
    uint32_t now = millis();
    if (gCount != 0 && now - gLastSample < SAMPLE_MS) return;
    gLastSample = now;
    takeSample();
}

int percent() {
    if (!gOk) return 100;
    // On USB the cell voltage is elevated/noisy (charge current, CC/CV) and is
    // not a true SoC, so show a stable full; estimate only when on battery.
    if (pmu.isVbusIn()) return 100;
    uint16_t mv = medianVoltage();
    if (mv < BATT_ABSENT_MV) return 100;
    return curvePercent(mv);
}

bool charging() {
    if (!gOk) return true;
    return pmu.isCharging() || pmu.isVbusIn();
}

}  // namespace power
