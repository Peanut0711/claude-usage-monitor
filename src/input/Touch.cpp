// ============================================================================
//  Touch.cpp
// ============================================================================
#include "Touch.h"

#include <TouchDrvCSTXXX.hpp>
#include <Wire.h>

#include "../board_pins.h"
#include "../config.h"

namespace {
TouchDrvCSTXXX drv;
bool gOk = false;

// CST226SE I2C slave address on the T-Display S3 Pro.
constexpr uint8_t CST226SE_ADDR = 0x5A;

// Coordinate mapping from the panel's native portrait orientation to our
// landscape (rotation 1). Flip flags are here so calibration is a one-line
// change if the axes come out swapped/mirrored on hardware.
constexpr bool TOUCH_SWAP_XY = true;
constexpr bool TOUCH_FLIP_X  = false;
constexpr bool TOUCH_FLIP_Y  = true;

// Set by the CST226SE Home-button event (fires during getPoint()).
volatile bool gHome = false;
void onHomeButton(void*) { gHome = true; }
}  // namespace

namespace touch {

bool begin() {
    drv.setPins(TDS3_PIN_TOUCH_RST, -1);   // no IRQ -> polled
    gOk = drv.begin(Wire, CST226SE_ADDR, TDS3_PIN_I2C_SDA, TDS3_PIN_I2C_SCL);
    if (gOk) drv.setHomeButtonCallback(onHomeButton, nullptr);
    return gOk;
}

bool available() { return gOk; }

void mapXY(int rx, int ry, int& sx, int& sy) {
    sx = TOUCH_SWAP_XY ? ry : rx;
    sy = TOUCH_SWAP_XY ? rx : ry;
    if (TOUCH_FLIP_X) sx = (480 - 1) - sx;
    if (TOUCH_FLIP_Y) sy = (222 - 1) - sy;
}

bool read(int& x, int& y) {
    if (!gOk) return false;
    int16_t tx[1], ty[1];
    if (drv.getPoint(tx, ty, 1) < 1) return false;
    mapXY(tx[0], ty[0], x, y);
    return true;
}

bool readDebug(int& rawX, int& rawY, int& mapX, int& mapY) {
    if (!gOk) return false;
    int16_t tx[1], ty[1];
    if (drv.getPoint(tx, ty, 1) < 1) return false;
    rawX = tx[0];
    rawY = ty[0];
    mapXY(rawX, rawY, mapX, mapY);
    return true;
}

void poll() {
    if (!gOk) return;
    int16_t tx[1], ty[1];
    drv.getPoint(tx, ty, 1);   // result ignored; drives the Home-button event
}

bool homePressed() {
    bool p = gHome;
    gHome = false;
    return p;
}

}  // namespace touch
