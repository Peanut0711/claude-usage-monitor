// ============================================================================
//  Net.h  -  WiFi station/AP helpers + device identity
//
//  Thin wrappers around the Arduino WiFi stack so the boot flow reads cleanly.
// ============================================================================
#pragma once

#include <Arduino.h>
#include <IPAddress.h>

namespace net {

// SoftAP SSID for setup mode: CUM_AP_PREFIX + last 2 MAC bytes (e.g. -3F7A).
String apSsid();

// Bring up the SoftAP used by the setup captive portal. Returns the AP IP.
IPAddress startAP();

// Connect as a station. Blocks up to CUM_WIFI_CONNECT_TIMEOUT_MS. Returns
// true once an IP is obtained.
bool connectSTA(const String& ssid, const String& pass);

bool      isConnected();
IPAddress localIP();
int       rssi();           // dBm, valid when connected

}  // namespace net
