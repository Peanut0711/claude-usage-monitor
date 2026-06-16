// ============================================================================
//  Net.cpp
// ============================================================================
#include "Net.h"

#include <WiFi.h>

#include "../config.h"

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
    return WiFi.softAPIP();
}

bool connectSTA(const String& ssid, const String& pass) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start >= CUM_WIFI_CONNECT_TIMEOUT_MS) return false;
        delay(CUM_WIFI_RETRY_MS);
    }
    return true;
}

bool      isConnected() { return WiFi.status() == WL_CONNECTED; }
IPAddress localIP()     { return WiFi.localIP(); }
int       rssi()        { return WiFi.RSSI(); }

}  // namespace net
