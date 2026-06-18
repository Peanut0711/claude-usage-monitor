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

// Connect among the given list (home / office / ...). If preferredIdx is valid,
// try that network directly first (no scan) for a fast reconnect at the usual
// location, then fall back to a strongest-of-all scan. Blocks up to ~5 s on the
// fast path plus CUM_WIFI_CONNECT_TIMEOUT_MS on the scan. Returns true once an
// IP is obtained. Pass preferredIdx < 0 to scan straight away.
bool connectMulti(const String ssids[], const String passwords[], int count,
                  int preferredIdx = -1);

// Scan nearby networks for the setup dropdown. This chip is 2.4GHz-only, so
// every result is a band the device can actually join. Fills `ssids` with up
// to maxN unique, non-hidden names; returns the count. Call before startAP()
// (it switches to STA briefly).
int scanNetworks(String ssids[], int maxN);

// Bring up STA alongside the running SoftAP to verify credentials during setup,
// so the portal client (the phone) stays associated to the AP. Returns true
// once the STA link obtains an IP within `timeoutMs`.
bool apStaConnect(const String& ssid, const String& pass, uint32_t timeoutMs);

// Tear down the verify STA link, returning to AP-only.
void apStaDisconnect();

bool      isConnected();
// Nudge the STA back onto its last AP after a drop (non-blocking; reuses the
// stored config). Cheap to call repeatedly while disconnected.
void      reconnect();
IPAddress localIP();
int       rssi();           // dBm, valid when connected
String    ssid();           // SSID of the connected network

}  // namespace net
