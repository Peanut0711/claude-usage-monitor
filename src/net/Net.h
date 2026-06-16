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

// Connect to the strongest reachable network among the given list (home /
// office / ...). Blocks up to CUM_WIFI_CONNECT_TIMEOUT_MS. Returns true once
// an IP is obtained.
bool connectMulti(const String ssids[], const String passwords[], int count);

bool      isConnected();
IPAddress localIP();
int       rssi();           // dBm, valid when connected
String    ssid();           // SSID of the connected network

}  // namespace net
