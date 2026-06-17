// ============================================================================
//  Proximity.h  -  LTR-553ALS-01 proximity sensor (wake-on-approach)
//
//  The LTR-553 is a combined ambient-light + IR proximity sensor on the shared
//  I2C bus. We use only the proximity channel: bring a hand close and the panel
//  wakes, no button press needed.
// ============================================================================
#pragma once

namespace prox {

// Initialise the sensor over the shared I2C bus and enable the proximity
// channel. Returns false if the chip isn't found (callers then skip prox-wake).
bool begin();

bool available();

// True while an object is close. Hysteresis debounces the edge; the sensor is
// polled at a throttled cadence internally, so this is safe to call every loop.
bool near();

// Raw proximity reading (0..2047, higher = closer), -1 if unavailable. For tuning.
int raw();

}  // namespace prox
