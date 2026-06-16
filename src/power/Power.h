// ============================================================================
//  Power.h  -  SY6970 PMU: battery level + charge state
//
//  The SY6970 is a charger/power-path IC with no fuel gauge, so the battery
//  percentage is estimated from the measured cell voltage.
// ============================================================================
#pragma once

namespace power {

// Initialise the PMU over the shared I2C bus and start continuous ADC. Returns
// false if the chip is not found (callers then show a placeholder battery).
bool begin();

bool available();

// Estimated battery charge 0..100. Returns 100 when running on USB with no
// battery detected.
int percent();

// True while charging or whenever external (USB) power is present.
bool charging();

}  // namespace power
