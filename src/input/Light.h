// ============================================================================
//  Light.h  -  LTR-553ALS ambient light sensor (for auto-brightness)
//
//  Minimal polled driver over the shared I2C bus. Returns a raw visible-light
//  reading; callers map it to a backlight level.
// ============================================================================
#pragma once

#include <stdint.h>

namespace light {

// Enable the ALS. Returns false if the sensor does not ACK.
bool begin();

bool available();

// Raw CH0 (visible + IR) reading, 0..65535. 0 if unavailable.
uint16_t raw();

}  // namespace light
