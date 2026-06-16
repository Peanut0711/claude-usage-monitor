// ============================================================================
//  Touch.h  -  CST226SE capacitive touch (polled), mapped to screen coords
//
//  The panel is driven in landscape (480x222). This returns touch points
//  already mapped into that coordinate space so UI code can hit-test directly.
// ============================================================================
#pragma once

namespace touch {

// Initialise the CST226SE over the shared I2C bus. Returns false if the chip
// is not found (UI then falls back to web-based unlock).
bool begin();

bool available();

// If currently touched, writes mapped screen coords (x:0..479, y:0..221) and
// returns true; otherwise returns false.
bool read(int& x, int& y);

}  // namespace touch
