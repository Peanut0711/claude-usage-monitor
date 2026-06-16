// ============================================================================
//  Portal.h  -  captive setup portal + PIN unlock web page
//
//  Two flows share one synchronous WebServer:
//    - Setup  : runs on the SoftAP with a catch-all DNS so any URL opens the
//               form. Collects WiFi + OAuth token + PIN, then provisions.
//    - Unlock : runs on the connected station (LAN). Serves a PIN form whose
//               submit decrypts the stored token.
//
//  Non-blocking: main calls handle() each loop and updates the display. The
//  returned Event tells main when a flow has completed.
// ============================================================================
#pragma once

#include <Arduino.h>

namespace portal {

enum class Event { None, Provisioned, Unlocked };

// Start the SoftAP captive portal with the setup form.
void beginSetup();

// Start the unlock page (assumes WiFi station is already connected).
void beginUnlock();

// Pump DNS + HTTP. Returns Provisioned / Unlocked exactly once when the
// matching flow completes; None otherwise.
Event handle();

// Valid after handle() returns Unlocked: the decrypted OAuth token.
const String& token();

void stop();

}  // namespace portal
