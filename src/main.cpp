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
#include "input/Light.h"
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

// Brief startup window: holding either side button (IO12 / IO16) wipes stored
// credentials so the user can re-run setup (e.g. to enter a corrected token).
// These are the buttons broken out on the enclosure; BOOT is internal-only.
bool sideButtonDown() {
    return digitalRead(TDS3_PIN_BTN_IO12) == LOW ||
           digitalRead(TDS3_PIN_BTN_IO16) == LOW;
}

// Adjust backlight to ambient light (throttled, smoothed). No-op without the
// sensor. The 600 reference scales "bright room" -> full brightness; tune to
// taste.
void autoBright() {
    if (gScreenOff || !light::available()) return;   // paused while off
    static uint32_t last = 0;
    static float    ema = -1;
    static int      curBr = 0;
    if (millis() - last < 400) return;
    last = millis();
    uint16_t r = light::raw();
    ema = (ema < 0) ? r : (ema * 0.8f + r * 0.2f);
    int b = 40 + (int)(ema * (255 - 40) / 600.0f);
    if (b < 40) b = 40;
    if (b > 255) b = 255;
    if (curBr == 0 || abs(b - curBr) >= 8) {
        curBr = b;
        display::setBrightness(b);
    }
}

// Toggle the backlight (power saving). Touch keeps working with it off, so the
// Home button also wakes the screen. autoBright is paused while off.
void toggleBacklight() {
    gScreenOff = !gScreenOff;
    display::setBrightness(gScreenOff ? 0 : 200);   // autoBright refines on wake
}

// Edge-detected side-button press (for manual refresh in Running state).
bool sideButtonPressed() {
    static bool was = false;
    bool down = sideButtonDown();
    bool edge = down && !was;
    was = down;
    return edge;
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
        d.battery      = power::percent();
        d.charging     = power::charging();
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
    display::drawMessage("Usage", "Divining your usage...");  // shown during 1st poll
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

#if CUM_TOUCH_TEST
    gTouchOn = touch::begin();
    gState = State::TouchTest;
    display::drawTouchTest(false, -1, -1, -1, -1);
    Serial.printf("[touchtest] touch=%d\n", gTouchOn);
    return;
#endif

    credentials::begin();
    power::begin();
    light::begin();
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
    autoBright();
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
            } else if (gTouchOn) {
                handleKeypad();
            }
            delay(20);
            break;
        }

        case State::Running:
            touch::poll();                          // drive Home-button event
            if (touch::homePressed()) {
                // Debounce: the controller repeats the event while held, so
                // ignore further presses for a short window.
                static uint32_t lastHome = 0;
                if (millis() - lastHome > 700) {
                    toggleBacklight();
                    lastHome = millis();
                }
            }
            if (gScreenOff) { delay(30); break; }   // screen off: skip work
            if (sideButtonPressed()) {          // manual refresh
                Serial.println("[run] manual refresh");
                display::drawMessage("Usage", "Refreshing...");
                pollAndRender();
                gLastPoll = millis();
            } else if (millis() - gLastPoll >= CUM_POLL_INTERVAL_MS) {
                pollAndRender();
                gLastPoll = millis();
            }
            delay(50);
            break;
    }
}
