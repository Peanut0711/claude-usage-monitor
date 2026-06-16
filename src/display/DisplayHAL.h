// ============================================================================
//  DisplayHAL.h  -  thin drawing layer on top of the LGFX device
//
//  Everything UI-facing goes through this namespace so the rest of the
//  firmware never talks to LovyanGFX directly. When LVGL is added later it can
//  grab the raw device via display::gfx() for its flush callback, while custom
//  drawing keeps using these helpers.
//
//  Orientation: landscape, 480 (W) x 222 (H).
// ============================================================================
#pragma once

#include <Arduino.h>

#include "LGFX_TDisplayS3Pro.hpp"

namespace display {

// Initialise the panel + backlight. Returns false if the panel init fails.
bool begin(uint8_t brightness = 200);

// Raw LovyanGFX device (for future LVGL flush or advanced drawing).
LGFX_TDisplayS3Pro& gfx();

// Set backlight brightness (0-255).
void setBrightness(uint8_t value);

// Stage-1 self-test: title text + a colored utilization bar.
void drawTestScreen();

// --- Stage 2 screens --------------------------------------------------------
// Setup mode: tell the user which AP to join and where to browse.
void drawProvisioning(const String& apSsid, const String& apPass,
                      const String& portalIp);

// Unlock mode: prompt for the PIN via the given LAN URL.
void drawUnlock(const String& portalUrl, int failsRemaining);

// Connected/running status line.
void drawStatus(const String& ssid, const String& ip, int rssi);

}  // namespace display
