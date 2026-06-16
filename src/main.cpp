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
#include "input/Touch.h"
#include "net/Api.h"
#include "net/Net.h"
#include "net/Portal.h"
#include "power/Power.h"
#include "secure/CredentialStore.h"

namespace {

enum class State { Setup, Unlock, Running, TouchTest };
State    gState;
String   gSsid;          // remembered for the status screen
String   gToken;         // decrypted OAuth token, held in RAM while running
uint32_t gLastPoll = 0;  // millis() of the last API poll

String   gPin;           // PIN digits typed on the touch keypad
bool     gTouchOn = false;
bool     gWasTouched = false;
bool     gScreenOff = false;  // backlight toggled off (double-tap)

void enterRunning();     // forward decl (tryPin -> enterRunning)

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

// IO16 is the only assigned side button (boot-hold = factory reset; in Running
// = backlight toggle). IO12 is intentionally unassigned.
bool io16Down() {
    return digitalRead(TDS3_PIN_BTN_IO16) == LOW;
}

// Toggle the backlight (power saving). Touch keeps working with it off.
void toggleBacklight() {
    gScreenOff = !gScreenOff;
    display::setBrightness(gScreenOff ? 0 : 200);   // restore default on wake
}

// Edge-detected IO16 press.
bool io16Pressed() {
    static bool was = false;
    bool down = io16Down();
    bool edge = down && !was;
    was = down;
    return edge;
}

// Edge-detected IO12 press (toggles the detail page).
bool io12Pressed() {
    static bool was = false;
    bool down = digitalRead(TDS3_PIN_BTN_IO12) == LOW;
    bool edge = down && !was;
    was = down;
    return edge;
}

bool factoryResetRequested() {
    pinMode(TDS3_PIN_BTN_IO16, INPUT_PULLUP);
    display::drawMessage("Starting", "Hold IO16 to reset & re-setup");
    uint32_t start = millis();
    while (millis() - start < 2500) {
        if (io16Down()) {
            uint32_t held = millis();           // require a deliberate hold
            while (io16Down()) {
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

// --- Background poll (runs off the UI core so the screen can animate) -------
volatile bool gPollRunning = false;
volatile bool gPollDone    = false;
api::Usage    gPollResult;
api::Usage    gLastUsage;            // last result (drives dashboard + detail)
bool          gPollAnimate = false;
int           gAnimFrame   = 0;
bool          gShowDetail  = false;  // IO12 toggles dashboard <-> detail

void pollTaskFn(void*) {
    gPollResult = api::poll(gToken);   // ~1s blocking, off the UI core
    gPollDone = true;
    gPollRunning = false;
    vTaskDelete(nullptr);
}

void requestPoll(bool animate) {
    if (gPollRunning || gPollDone) return;   // don't overlap / lose a result
    gPollAnimate = animate;
    gAnimFrame = 0;
    gPollRunning = true;
    xTaskCreatePinnedToCore(pollTaskFn, "poll", 12288, nullptr, 1, nullptr, 0);
}

String fmtUptime() {
    uint32_t s = millis() / 1000, h = s / 3600, m = (s % 3600) / 60;
    if (h > 0) return String(h) + "h " + String(m) + "m";
    return String(m) + "m " + String(s % 60) + "s";
}

// Draw whichever page is active from the last poll result + live sensors.
void renderCurrentView() {
    if (gScreenOff) return;
    time_t now = time(nullptr);
    if (gShowDetail) {
        display::Detail d;
        d.ssid     = gSsid;
        d.ip       = net::localIP().toString();
        d.rssi     = net::rssi();
        d.battery  = power::percent();
        d.charging = power::charging();
        d.reset5h  = fmtCountdown(gLastUsage.reset5h, now);
        d.reset7d  = fmtCountdown(gLastUsage.reset7d, now);
        d.uptime   = fmtUptime();
        display::drawDetail(d);
    } else if (gLastUsage.ok) {
        display::Dashboard d;
        d.current      = gLastUsage.util5h;
        d.currentReset = fmtCountdown(gLastUsage.reset5h, now);
        d.weekly       = gLastUsage.util7d;
        d.weeklyReset  = fmtCountdown(gLastUsage.reset7d, now);
        d.rssi         = net::rssi();
        d.battery      = power::percent();
        d.charging     = power::charging();
        d.status       = kStatus[gStatusIdx % (sizeof(kStatus) / sizeof(kStatus[0]))];
        display::drawDashboard(d);
    } else {
        const char* note = gLastUsage.httpCode == 401 ? "Auth rejected - check token"
                         : gLastUsage.httpCode <= 0   ? "Network / TLS error"
                                                      : "Unexpected response";
        display::drawApiError(gLastUsage.httpCode, note);
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
    gPin = "";
    gWasTouched = false;
    portal::beginUnlock();                  // web unlock stays as a fallback
    gTouchOn = touch::begin();
    String url = "http://" + net::localIP().toString();
    if (gTouchOn) display::drawKeypad(0, "");
    else          display::drawUnlock(url, credentials::failsRemaining());
    Serial.printf("[unlock] touch=%d; web unlock at %s\n", gTouchOn, url.c_str());
}

// Try the entered PIN. On success enter Running; otherwise refresh the keypad.
void tryPin() {
    String tok;
    credentials::UnlockResult r = credentials::unlock(gPin, tok);
    gPin = "";
    if (r == credentials::UnlockResult::Ok) {
        gToken = tok;
        enterRunning();
    } else if (r == credentials::UnlockResult::LockedOut) {
        display::drawMessage("Locked out", "Credentials wiped - rebooting");
        delay(1500);
        ESP.restart();
    } else {
        display::drawKeypad(0, String(credentials::failsRemaining()) + " left");
    }
}

// Edge-triggered keypad handling (one key per finger press).
void handleKeypad() {
    int x, y;
    bool down = touch::read(x, y);
    if (down && !gWasTouched) {
        char k = display::keypadHit(x, y);
        if (k >= '0' && k <= '9') {
            if (gPin.length() < CUM_PIN_LEN) gPin += k;
        } else if (k == 'C') {
            gPin = "";
        } else if (k == '<') {
            if (gPin.length()) gPin.remove(gPin.length() - 1);
        }
        if (k) {
            display::drawKeypad(gPin.length(), "");
            if (gPin.length() == CUM_PIN_LEN) tryPin();
        }
    }
    gWasTouched = down;
}

void enterRunning() {
    gState = State::Running;
    touch::homePressed();               // discard any press from the unlock phase
    portal::stop();
    configTime(0, 0, CUM_NTP_SERVER);   // UTC epoch for reset countdowns
    gShowDetail = false;
    pinMode(TDS3_PIN_BTN_IO12, INPUT_PULLUP);
    pinMode(TDS3_PIN_BTN_IO16, INPUT_PULLUP);
    Serial.println("[run] unlocked; polling Anthropic API");
    display::drawRefreshAnim(0);        // intro frame; loop animates until result
    requestPoll(true);                  // first poll, animated
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

    display::drawSplash();              // branded boot splash
    delay(1300);

#if CUM_TOUCH_TEST
    gTouchOn = touch::begin();
    gState = State::TouchTest;
    display::drawTouchTest(false, -1, -1, -1, -1);
    Serial.printf("[touchtest] touch=%d\n", gTouchOn);
    return;
#endif

    credentials::begin();
    power::begin();
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
#if CUM_TOUCH_TEST
    if (gState == State::TouchTest) {
        static int rx = -1, ry = -1, mx = -1, my = -1;
        int a, b, c, d;
        bool down = touch::readDebug(a, b, c, d);
        if (down) { rx = a; ry = b; mx = c; my = d; }
        display::drawTouchTest(down, rx, ry, mx, my);
        delay(60);
        return;
    }
#endif
    power::update();          // slow battery sampling (self-throttled)
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
            if (e == portal::Event::Unlocked) {     // web fallback succeeded
                gToken = portal::token();
                enterRunning();
            } else if (e == portal::Event::Provisioned) {
                delay(300);                         // lockout wiped creds
                ESP.restart();
            } else {
                if (io16Pressed()) {                // IO16 toggles backlight
                    toggleBacklight();
                    if (!gScreenOff) display::drawKeypad(gPin.length(), "");
                }
                // Keypad only when the screen is on (can't type blind).
                if (!gScreenOff && gTouchOn) handleKeypad();
            }
            delay(20);
            break;
        }

        case State::Running: {
            int tx, ty;
            bool touching = touch::read(tx, ty);    // also drives Home event

            // Home button -> manual refresh. One action per press; re-arm only
            // after the repeated event stops for >300ms (finger lifted).
            static bool     armed = true;
            static uint32_t lastSeen = 0;
            uint32_t now = millis();
            bool homeFired = false;
            if (touch::homePressed()) {
                lastSeen = now;
                if (armed) { homeFired = true; armed = false; }
            } else if (now - lastSeen > 300) {
                armed = true;
            }

            // IO16 -> backlight on/off (also wakes from off).
            if (io16Pressed()) {
                toggleBacklight();
                if (!gScreenOff) renderCurrentView();   // repaint on wake
            }

            // IO12 -> toggle detail page.
            if (io12Pressed()) {
                gShowDetail = !gShowDetail;
                renderCurrentView();
            }

            // Brightness: drag vertically on the LEFT edge strip (top=bright).
            if (!gScreenOff && touching && tx < 48) {
                int b = 255 - (ty - 4) * (255 - 25) / (218 - 4);
                if (b < 25)  b = 25;
                if (b > 255) b = 255;
                display::setBrightness(b);
            }

            if (gScreenOff) { delay(30); break; }   // screen off: skip work

            // Consume a finished poll.
            if (gPollDone) {
                gPollDone = false;
                gLastUsage = gPollResult;
                gStatusIdx++;
                renderCurrentView();
            }

            // Trigger a poll: Home button (animated) or the periodic interval.
            if (homeFired) {
                requestPoll(true);
                gLastPoll = millis();
            } else if (!gPollRunning && millis() - gLastPoll >= CUM_POLL_INTERVAL_MS) {
                requestPoll(false);
                gLastPoll = millis();
            }

            // Animate while an animated poll is running (skip on the detail page).
            if (gPollRunning && gPollAnimate && !gShowDetail) {
                display::drawRefreshAnim(gAnimFrame++);
                delay(70);
            } else {
                delay(50);
            }
            break;
        }
    }
}
