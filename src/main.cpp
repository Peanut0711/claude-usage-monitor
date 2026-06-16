// ============================================================================
//  Claude Usage Monitor  -  LilyGo T-Display S3 Pro
//
//  Boot flow (Stage 2):
//    not provisioned ............ SoftAP captive portal -> collect creds/PIN
//    provisioned, WiFi up ....... unlock page -> PIN decrypts the token
//    WiFi fails to connect ...... fall back to setup so creds can be fixed
//    unlocked ................... RUNNING (Stage 3 will poll the API here)
// ============================================================================
#include <Arduino.h>

#include "config.h"
#include "display/DisplayHAL.h"
#include "net/Net.h"
#include "net/Portal.h"
#include "secure/CredentialStore.h"

namespace {

enum class State { Setup, Unlock, Running };
State gState;
String gSsid;   // remembered for the status screen

void enterSetup() {
    gState = State::Setup;
    IPAddress ip = net::startAP();
    portal::beginSetup();
    display::drawProvisioning(net::apSsid(), CUM_AP_PASSWORD, ip.toString());
    Serial.printf("[setup] AP '%s' up, portal at http://%s\n",
                  net::apSsid().c_str(), ip.toString().c_str());
}

void enterUnlock() {
    gState = State::Unlock;
    portal::beginUnlock();
    String url = "http://" + net::localIP().toString();
    display::drawUnlock(url, credentials::failsRemaining());
    Serial.printf("[unlock] connected; unlock page at %s\n", url.c_str());
}

void enterRunning() {
    gState = State::Running;
    portal::stop();
    display::drawStatus(gSsid, net::localIP().toString(), net::rssi());
    Serial.println("[run] unlocked; token held in RAM (Stage 3: poll API)");
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n[Claude Usage Monitor] boot");

    if (!display::begin()) {
        Serial.println("[display] init FAILED");
        return;
    }
    Serial.println("[display] init OK");

    credentials::begin();
    if (!credentials::isProvisioned()) {
        enterSetup();
        return;
    }

    // Provisioned: load WiFi (device-key sealed) and connect.
    String ssid, pass;
    if (!credentials::loadWifi(ssid, pass) || !net::connectSTA(ssid, pass)) {
        Serial.println("[wifi] connect failed -> setup mode");
        enterSetup();
        return;
    }
    gSsid = ssid;
    enterUnlock();
}

void loop() {
    switch (gState) {
        case State::Setup:
            if (portal::handle() == portal::Event::Provisioned) {
                Serial.println("[setup] provisioned -> reboot");
                delay(300);          // let the HTTP response flush
                ESP.restart();
            }
            break;

        case State::Unlock: {
            portal::Event e = portal::handle();
            if (e == portal::Event::Unlocked) {
                enterRunning();
            } else if (e == portal::Event::Provisioned) {
                // Lockout wiped creds; reboot back into setup.
                delay(300);
                ESP.restart();
            } else {
                // Keep the attempts-remaining figure fresh after a wrong PIN.
                static uint8_t lastFails = 255;
                uint8_t f = credentials::failsRemaining();
                if (f != lastFails) {
                    lastFails = f;
                    display::drawUnlock("http://" + net::localIP().toString(), f);
                }
            }
            break;
        }

        case State::Running:
            // Stage 3 will poll the Anthropic API on CUM_POLL_INTERVAL_MS here.
            delay(100);
            break;
    }
}
