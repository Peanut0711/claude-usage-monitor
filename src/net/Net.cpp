// ============================================================================
//  Net.cpp
// ============================================================================
#include "Net.h"

#include <WiFi.h>
#include <WiFiMulti.h>

#include "../config.h"

namespace {
// Cap WiFi TX power well below the 20 dBm default. The current spike when a
// client associates (AP) or when the radio transmits (STA) is the usual cause
// of brownout resets on boards sharing a marginal USB 5V rail with a display.
// 11 dBm keeps plenty of range for a same-room phone / nearby router.
void capTxPower() { WiFi.setTxPower(WIFI_POWER_11dBm); }
}  // namespace

namespace net {

String apSsid() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char suffix[5];
    snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);
    return String(CUM_AP_PREFIX) + suffix;
}

IPAddress startAP() {
    WiFi.mode(WIFI_AP);
    IPAddress ip(CUM_AP_IP);
    IPAddress gw(CUM_AP_IP);
    IPAddress mask(255, 255, 255, 0);
    WiFi.softAPConfig(ip, gw, mask);
    WiFi.softAP(apSsid().c_str(), CUM_AP_PASSWORD);
    capTxPower();
    return WiFi.softAPIP();
}

bool connectMulti(const String ssids[], const String passwords[], int count) {
    if (count <= 0) return false;

    WiFiMulti wm;
    for (int i = 0; i < count; i++) {
        wm.addAP(ssids[i].c_str(), passwords[i].c_str());
    }
    WiFi.mode(WIFI_STA);

    // WiFiMulti scans and connects to the strongest known network.
    bool connected = (wm.run(CUM_WIFI_CONNECT_TIMEOUT_MS) == WL_CONNECTED);
    if (connected) capTxPower();
    return connected;
}

bool      isConnected() { return WiFi.status() == WL_CONNECTED; }
IPAddress localIP()     { return WiFi.localIP(); }
int       rssi()        { return WiFi.RSSI(); }
String    ssid()        { return WiFi.SSID(); }

}  // namespace net
