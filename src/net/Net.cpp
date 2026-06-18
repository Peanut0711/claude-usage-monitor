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

bool connectMulti(const String ssids[], const String passwords[], int count,
                  int preferredIdx) {
    if (count <= 0) return false;
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(true);   // modem sleep: radio naps between beacons (saves power,
                           // ~DTIM latency only; the default is unreliable per-core)

    // Fast path: connect straight to the last-good network. WiFi.begin() does a
    // targeted probe (connects as soon as that SSID is found) instead of
    // WiFiMulti's full all-channel scan + RSSI sort, so a normal boot at the
    // usual location skips ~2-4 s of scanning.
    if (preferredIdx >= 0 && preferredIdx < count) {
        WiFi.begin(ssids[preferredIdx].c_str(), passwords[preferredIdx].c_str());
        uint32_t start = millis();
        while (millis() - start < 5000) {
            if (WiFi.status() == WL_CONNECTED) { capTxPower(); return true; }
            delay(150);
        }
        WiFi.disconnect(true);   // not in range here -> clear it before the scan
        delay(50);
    }

    // Fallback: scan all + connect the strongest known AP (covers a location
    // change, e.g. home <-> office, where the preferred network is absent).
    WiFiMulti wm;
    for (int i = 0; i < count; i++) {
        wm.addAP(ssids[i].c_str(), passwords[i].c_str());
    }
    bool connected = (wm.run(CUM_WIFI_CONNECT_TIMEOUT_MS) == WL_CONNECTED);
    if (connected) capTxPower();
    return connected;
}

int scanNetworks(String ssids[], int maxN) {
    WiFi.mode(WIFI_STA);
    int found = WiFi.scanNetworks();         // blocking, ~2-4 s
    if (found < 0) found = 0;
    int out = 0;
    for (int i = 0; i < found && out < maxN; i++) {
        String s = WiFi.SSID(i);
        if (s.length() == 0) continue;       // skip hidden networks
        bool dup = false;
        for (int j = 0; j < out; j++) if (ssids[j] == s) { dup = true; break; }
        if (!dup) ssids[out++] = s;
    }
    WiFi.scanDelete();
    return out;
}

bool apStaConnect(const String& ssid, const String& pass, uint32_t timeoutMs) {
    WiFi.mode(WIFI_AP_STA);                   // keep the AP up for the phone
    WiFi.begin(ssid.c_str(), pass.c_str());
    capTxPower();
    uint32_t start = millis();
    while (millis() - start < timeoutMs) {
        if (WiFi.status() == WL_CONNECTED) { capTxPower(); return true; }
        delay(200);
    }
    return false;
}

void apStaDisconnect() {
    WiFi.disconnect(false, true);             // drop STA, clear its config
    WiFi.mode(WIFI_AP);
    capTxPower();
}

bool      isConnected() { return WiFi.status() == WL_CONNECTED; }
void      reconnect()   { WiFi.reconnect(); }   // re-associate to the stored AP

// Stop the radio entirely (esp_wifi_stop via disconnect(wifioff=true) + mode
// OFF). Used when the screen sleeps on battery: associated modem-sleep still
// wakes every DTIM to hear beacons, which is pure waste when we never poll while
// asleep. A subsequent connectMulti() re-enables STA mode and re-associates.
void      radioOff()    { WiFi.disconnect(true); WiFi.mode(WIFI_OFF); }
IPAddress localIP()     { return WiFi.localIP(); }
int       rssi()        { return WiFi.RSSI(); }
String    ssid()        { return WiFi.SSID(); }

}  // namespace net
