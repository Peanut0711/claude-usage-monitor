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

// --- Backlight / inactivity -------------------------------------------------
uint32_t gLastInput  = 0;     // millis() of last input (touch/button)
bool     gManualOff  = false; // user forced backlight off via IO16
uint8_t  gActiveBright = 200; // brightness when awake (left-edge drag sets it)
int      gCurBright  = -1;    // last applied brightness (cache)
constexpr uint32_t DIM_MS    = 60000;    // idle -> dim (60 s)
constexpr uint32_t OFF_MS    = 120000;   // idle -> off (120 s)
constexpr uint8_t  DIM_LEVEL = 153;      // 60% of 255

void noteInput() { gLastInput = millis(); }

bool screenAsleep() {
    return gManualOff || (millis() - gLastInput > OFF_MS);
}

// Drive the backlight from manual-off / inactivity. Called at the top of loop.
void applyBacklight() {
    int t;
    if (gManualOff) {
        t = 0;
    } else if (gState == State::Setup) {
        t = gActiveBright;                 // keep the setup screen readable
    } else {
        uint32_t idle = millis() - gLastInput;
        t = (idle > OFF_MS) ? 0 : (idle > DIM_MS) ? DIM_LEVEL : gActiveBright;
    }
    if (t != gCurBright) { gCurBright = t; display::setBrightness(t); }
}

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

// Both side buttons are assigned. IO16: boot-hold = factory reset; in Running =
// backlight toggle (and wake). IO12: in Running = cycle pages, and wake the
// screen when it's off (the wake press is consumed, so it doesn't also page).
bool io16Down() {
    return digitalRead(TDS3_PIN_BTN_IO16) == LOW;
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

// True on a double-tap (two touch-downs within 400ms). Used to wake the screen.
// Also drives the Home-button event as a side effect (caller should discard it
// while asleep).
bool doubleTapDetected() {
    int x, y;
    bool down = touch::read(x, y);
    static bool     was = false;
    static uint32_t lastTap = 0;
    bool dbl = false;
    if (down && !was) {
        uint32_t now = millis();
        if (now - lastTap < 400) { dbl = true; lastTap = 0; }
        else                       lastTap = now;
    }
    was = down;
    return dbl;
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
bool          gStale       = false;  // last poll failed; showing previous data

// --- Count-up animation (dashboard) -----------------------------------------
// On a fresh poll the bars/% ease from the previously shown values up to the
// new ones (RPG "EXP gain" feel), overshoot slightly, then a small spark pops
// at the bar's edge. Driven a frame at a time from the loop (non-blocking).
float    gShownCur = 0, gShownWk = 0;   // values currently on screen
float    gStartCur = 0, gStartWk = 0;   // where this animation began
float    gTgtCur   = 0, gTgtWk   = 0;   // where it's heading
bool     gAnimating = false;
bool     gPopCur = false, gPopWk = false;  // pop only the card(s) that grew
uint32_t gAnimStart = 0;
uint32_t gAnimDur   = 0;                 // this run's climb length; scales with delta
// Climb ticks one whole % at a time (RPG counter feel), no overshoot. Duration
// = (% change) * STEP_MS, so small changes are quick and a big jump still feels
// like real growth — capped so the first poll (0 -> N) doesn't drag.
constexpr uint32_t STEP_MS = 100;        // time per 1% tick
constexpr uint32_t MAX_MS  = 3000;       // cap for a large jump
constexpr uint32_t POP_MS  = 600;        // spark/flash fade after landing

// IO12 cycles pages: dashboard -> detail -> history.
enum { PAGE_DASH = 0, PAGE_DETAIL = 1, PAGE_HISTORY = 2, PAGE_COUNT = 3 };
int           gPage = PAGE_DASH;

// Utilization history (one sample per successful poll), oldest..newest.
constexpr int HIST_N = 64;
float         gH5[HIST_N], gH7[HIST_N];
int           gHistCount = 0;

void histPush(float a, float b) {
    if (gHistCount < HIST_N) {
        gH5[gHistCount] = a; gH7[gHistCount] = b; gHistCount++;
    } else {
        for (int i = 1; i < HIST_N; i++) { gH5[i - 1] = gH5[i]; gH7[i - 1] = gH7[i]; }
        gH5[HIST_N - 1] = a; gH7[HIST_N - 1] = b;
    }
}

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

// Draw the dashboard from the currently-shown (animated) values, with optional
// landing-pop intensity per card.
void drawDashFrame(float curPop, float wkPop) {
    time_t now = time(nullptr);
    display::Dashboard d;
    d.current      = gShownCur;
    d.currentReset = fmtCountdown(gLastUsage.reset5h, now);
    d.weekly       = gShownWk;
    d.weeklyReset  = fmtCountdown(gLastUsage.reset7d, now);
    d.rssi         = net::rssi();
    d.battery      = power::percent();
    d.charging     = power::charging();
    d.status       = kStatus[gStatusIdx % (sizeof(kStatus) / sizeof(kStatus[0]))];
    d.stale        = gStale;
    d.curPop       = curPop;
    d.wkPop        = wkPop;
    display::drawDashboard(d);
}

// Begin a count-up from the on-screen values to the new poll values.
void startCountUp(float cur, float wk) {
    gStartCur = gShownCur; gStartWk = gShownWk;
    gTgtCur = cur; gTgtWk = wk;
    gPopCur = (cur > gStartCur + 0.5f);   // only pop a card that actually grew
    gPopWk  = (wk  > gStartWk  + 0.5f);
    // Duration follows the larger of the two changes so both cards land together.
    int steps = (int)roundf(fmaxf(fabsf(cur - gStartCur), fabsf(wk - gStartWk)));
    gAnimDur = (uint32_t)steps * STEP_MS;
    if (gAnimDur > MAX_MS) gAnimDur = MAX_MS;
    if (gAnimDur == 0)     gAnimDur = 1;  // no change -> finish immediately
    gAnimStart = millis();
    gAnimating = true;
}

// Advance one animation frame and draw it. Clears gAnimating when finished.
void stepCountUp() {
    uint32_t el = millis() - gAnimStart;
    float popI = 0.0f;
    if (el < gAnimDur) {                         // climbing 1% at a time (linear)
        float t = (float)el / gAnimDur;
        gShownCur = floorf(gStartCur + (gTgtCur - gStartCur) * t + 0.5f);
        gShownWk  = floorf(gStartWk  + (gTgtWk  - gStartWk ) * t + 0.5f);
    } else if ((gPopCur || gPopWk) && el < gAnimDur + POP_MS) {  // landed: pop
        gShownCur = gTgtCur; gShownWk = gTgtWk;
        popI = 1.0f - (float)(el - gAnimDur) / POP_MS;
    } else {                                     // done
        gShownCur = gTgtCur; gShownWk = gTgtWk;
        gAnimating = false;
    }
    drawDashFrame(gPopCur ? popI : 0.0f, gPopWk ? popI : 0.0f);
}

// Snap the animation straight to its target (used when leaving the dashboard).
void finishCountUp() {
    gShownCur = gTgtCur; gShownWk = gTgtWk; gAnimating = false;
}

// Draw whichever page is active from the last poll result + live sensors.
void renderCurrentView() {
    time_t now = time(nullptr);
    if (gPage == PAGE_DETAIL) {
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
        return;
    }
    if (gPage == PAGE_HISTORY) {
        display::drawHistory(gH5, gH7, gHistCount);
        return;
    }
    if (gLastUsage.ok) {
        drawDashFrame(0.0f, 0.0f);              // steady state (no pop)
    } else {
        const char* note = gLastUsage.httpCode == 401 ? "Auth rejected - check token"
                         : gLastUsage.httpCode <= 0   ? "Network / TLS error"
                                                      : "Unexpected response";
        display::drawApiError(gLastUsage.httpCode, note);
    }
}

void enterSetup() {
    gState = State::Setup;
    portal::scanNetworks();             // cache nearby SSIDs before the AP goes up
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
    gManualOff = false;
    noteInput();
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
    if (down) noteInput();
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
    gPage = PAGE_DASH;
    gManualOff = false;
    noteInput();
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

    for (int f = 0; f <= 28; f += 2) {  // splash slides down into place
        display::drawSplash(f);
        delay(35);
    }
    delay(700);

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
    applyBacklight();         // inactivity dim/off + manual off
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
                break;
            }
            if (e == portal::Event::Provisioned) {
                delay(300);                         // lockout wiped creds
                ESP.restart();
            }
            bool asleep = screenAsleep();
            if (io16Pressed()) {                    // IO16: wake or force off
                if (asleep) { gManualOff = false; noteInput(); display::drawKeypad(gPin.length(), ""); }
                else        { gManualOff = true; }
            } else if (gTouchOn) {
                if (asleep) {                        // wake only on double-tap
                    if (doubleTapDetected()) {
                        gManualOff = false; noteInput();
                        display::drawKeypad(gPin.length(), "");
                    }
                } else {
                    handleKeypad();                  // type (notes input)
                }
            }
            delay(20);
            break;
        }

        case State::Running: {
            bool asleep = screenAsleep();

            // IO16: wake (show current view) if off, else force off.
            if (io16Pressed()) {
                if (asleep) { gManualOff = false; noteInput(); renderCurrentView(); }
                else        { gManualOff = true; }
                delay(30);
                break;
            }

            if (asleep) {                            // wake on double-tap or IO12
                // IO12 wakes to the current view only; the press is consumed
                // here so it won't also advance the page. The next (released
                // then re-pressed) IO12 cycles pages as usual.
                if (doubleTapDetected() || io12Pressed()) {
                    gManualOff = false;
                    noteInput();
                    renderCurrentView();
                }
                touch::homePressed();                // discard while asleep
                delay(30);
                break;
            }

            int tx, ty;
            bool touching = touch::read(tx, ty);     // also drives Home event
            bool homeNow  = touch::homePressed();
            bool io12     = io12Pressed();
            if (touching || homeNow || io12) noteInput();

            // --- awake ---
            // Home -> manual refresh. One per press; re-arm after release >300ms.
            static bool     armed = true;
            static uint32_t lastSeen = 0;
            uint32_t now = millis();
            bool homeFired = false;
            if (homeNow) {
                lastSeen = now;
                if (armed) { homeFired = true; armed = false; }
            } else if (now - lastSeen > 300) {
                armed = true;
            }

            if (io12) {
                gPage = (gPage + 1) % PAGE_COUNT;
                if (gAnimating) finishCountUp();     // don't carry an anim across pages
                renderCurrentView();
            }

            // Brightness: drag vertically on the LEFT edge strip (top=bright).
            if (touching && tx < 48) {
                int b = 255 - (ty - 4) * (255 - 25) / (218 - 4);
                if (b < 25)  b = 25;
                if (b > 255) b = 255;
                gActiveBright = b; gCurBright = b;
                display::setBrightness(b);
            }

            // Consume a finished poll. Keep the last good data on failure.
            if (gPollDone) {
                gPollDone = false;
                if (gPollResult.ok) {
                    gLastUsage = gPollResult;        // fresh good data
                    gStale = false;
                    gStatusIdx++;
                    histPush(gPollResult.util5h, gPollResult.util7d);
                    if (gPage == PAGE_DASH) {
                        startCountUp(gPollResult.util5h, gPollResult.util7d);
                    } else {                         // off-page: just snap, no anim
                        gShownCur = gPollResult.util5h;
                        gShownWk  = gPollResult.util7d;
                    }
                } else if (gLastUsage.ok) {
                    gStale = true;                   // keep showing prior data
                } else {
                    gLastUsage = gPollResult;        // never had data -> error screen
                }
                if (!gAnimating) renderCurrentView();   // anim frames self-draw
            }

            // Trigger a poll: Home button (animated) or the periodic interval.
            if (homeFired) {
                requestPoll(true);
                gLastPoll = millis();
            } else if (!gPollRunning && millis() - gLastPoll >= CUM_POLL_INTERVAL_MS) {
                requestPoll(false);
                gLastPoll = millis();
            }

            // Frame pacing. Count-up takes priority (it draws each frame), then
            // the refresh bob while a poll is in flight, else idle.
            if (gAnimating && gPage == PAGE_DASH) {
                stepCountUp();
                delay(8);                            // ~fast frames during count-up
            } else if (gPollRunning && gPollAnimate && gPage == PAGE_DASH) {
                display::drawRefreshAnim(gAnimFrame++);
                delay(120);                          // slower bob
            } else {
                delay(50);
            }
            break;
        }
    }
}
