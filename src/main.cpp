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
#include <time.h>

#include "board_pins.h"
#include "config.h"
#include "display/DisplayHAL.h"
#include "net/Api.h"
#include "net/Net.h"
#include "net/Portal.h"
#include "secure/CredentialStore.h"

namespace {

enum class State { Setup, Unlock, Running };
State    gState;
String   gSsid;          // remembered for the status screen
String   gToken;         // decrypted OAuth token, held in RAM while running
uint32_t gLastPoll = 0;  // millis() of the last API poll

// Format a reset timestamp (unix epoch) as a short countdown from `now`.
String fmtCountdown(long resetEpoch, time_t now) {
    if (resetEpoch <= 0 || now < 1000000000L) return "--";  // no header / no NTP
    long rem = resetEpoch - (long)now;
    if (rem < 0) rem = 0;
    if (rem > 8L * 24 * 3600) return "--";                  // implausible
    long h = rem / 3600, m = (rem % 3600) / 60;
    if (h >= 24) return String(h / 24) + "d " + String(h % 24) + "h";
    return String(h) + "h " + String(m) + "m";
}

// Brief startup window: holding either side button (IO12 / IO16) wipes stored
// credentials so the user can re-run setup (e.g. to enter a corrected token).
// These are the buttons broken out on the enclosure; BOOT is internal-only.
bool sideButtonDown() {
    return digitalRead(TDS3_PIN_BTN_IO12) == LOW ||
           digitalRead(TDS3_PIN_BTN_IO16) == LOW;
}

bool factoryResetRequested() {
    pinMode(TDS3_PIN_BTN_IO12, INPUT_PULLUP);
    pinMode(TDS3_PIN_BTN_IO16, INPUT_PULLUP);
    display::drawMessage("Starting", "Hold a side button to reset & re-setup");
    uint32_t start = millis();
    while (millis() - start < 2500) {
        if (sideButtonDown()) {
            uint32_t held = millis();           // require a deliberate hold
            while (sideButtonDown()) {
                if (millis() - held > 400) return true;
                delay(20);
            }
        }
        delay(20);
    }
    return false;
}

const char* const kStatus[] = {
    "Divining...", "Consulting the oracle", "Counting tokens",
    "Reading the runes", "Vibing", "Crunching numbers",
};
uint8_t gStatusIdx = 0;

void pollAndRender() {
    api::Usage u = api::poll(gToken);
    if (u.ok) {
        time_t now = time(nullptr);
        display::Dashboard d;
        d.current      = u.util5h;
        d.currentReset = fmtCountdown(u.reset5h, now);
        d.weekly       = u.util7d;
        d.weeklyReset  = fmtCountdown(u.reset7d, now);
        d.rssi         = net::rssi();
        d.battery      = 100;     // placeholder until SY6970 PMU bring-up
        d.charging     = true;
        d.status       = kStatus[gStatusIdx % (sizeof(kStatus) / sizeof(kStatus[0]))];
        gStatusIdx++;
        display::drawDashboard(d);
    } else {
        const char* note = u.httpCode == 401 ? "Auth rejected - check token"
                         : u.httpCode <= 0   ? "Network / TLS error"
                                             : "Unexpected response";
        display::drawApiError(u.httpCode, note);
    }
}

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
    configTime(0, 0, CUM_NTP_SERVER);   // UTC epoch for reset countdowns
    display::drawStatus(gSsid, net::localIP().toString(), net::rssi());
    Serial.println("[run] unlocked; polling Anthropic API");
    pollAndRender();                    // first poll immediately
    gLastPoll = millis();
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
    if (factoryResetRequested()) {
        Serial.println("[reset] BOOT held -> wiping credentials");
        credentials::wipe();
    }
    if (!credentials::isProvisioned()) {
        enterSetup();
        return;
    }

    // Provisioned: load all remembered networks and join the strongest.
    String ssids[CUM_WIFI_MAX], passwords[CUM_WIFI_MAX];
    int n = credentials::loadWifiList(ssids, passwords, CUM_WIFI_MAX);
    if (n == 0 || !net::connectMulti(ssids, passwords, n)) {
        Serial.println("[wifi] connect failed -> setup mode");
        enterSetup();
        return;
    }
    gSsid = net::ssid();
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
                gToken = portal::token();
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
            if (millis() - gLastPoll >= CUM_POLL_INTERVAL_MS) {
                pollAndRender();
                gLastPoll = millis();
            }
            delay(50);
            break;
    }
}
