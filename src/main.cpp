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
bool     gClockValid     = false;  // wall clock (NTP) has synced at least once
bool     gPendTimeRender = false;  // re-render once NTP syncs (refresh "Resets in --")

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

// Boot-splash animation state, shared between the foreground reset window and
// the background task that keeps it moving while WiFi connect blocks.
int           gBootFrame    = 0;   // frame counter (continuous across both)
volatile bool gBootAnimRun  = false;
volatile bool gBootAnimDone = false;

bool factoryResetRequested() {
    pinMode(TDS3_PIN_BTN_IO16, INPUT_PULLUP);
    constexpr uint32_t HOLD_MS = 1500;            // hold IO16 this long to reset
    uint32_t start = millis();
    gBootFrame = 0;
    while (millis() - start < 2500) {
        if (io16Down()) {                         // reveal the reset bar only on press
            uint32_t held = millis();
            while (io16Down()) {
                uint32_t dt = millis() - held;
                if (dt >= HOLD_MS) return true;   // held long enough -> reset
                display::drawResetHold((float)dt / HOLD_MS);
                delay(20);
            }
            // released early -> the loop resumes the busy animation below
        }
        display::drawBootBusy(gBootFrame++);      // bob + loading bar while waiting
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
bool     gNeedFullDash = false;          // next count-up frame must do a full redraw
bool     gPopCur = false, gPopWk = false;  // pop only the card(s) that grew
uint32_t gAnimStart = 0;
uint32_t gDurCur = 0, gDurWk = 0;        // per-card climb lengths
// Both cards climb continuously and in parallel; the big % text rounds to an int
// so the NUMBER ticks 1% per frame (STEP_MS ~= the ~26ms frame so no skips) while
// the BAR glides per-pixel. Climb length = (% delta) * STEP_MS, capped at MAX_MS.
// Each card pops when it lands.
constexpr uint32_t STEP_MS = 26;         // time per 1% of climb (~1 frame; 100% = 2.6s)
constexpr uint32_t MAX_MS  = 3000;       // cap for a large jump (>= 100%*STEP_MS)
constexpr uint32_t HOLD_MS = 150;        // hold the start frame before the climb begins
constexpr uint32_t REST_MS = 150;        // pause after landing before the fanfare bursts
constexpr uint32_t FAN_MS  = 500;        // fanfare burst fade length (after the rest)
constexpr uint32_t GLOW_DELAY = 0;       // bar/number white-glow lag behind the spark burst (0 = synced with the final tick)

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
void drawDashFrame(float curPop, float wkPop,
                   float curGlow = 0, float wkGlow = 0, bool full = true) {
    // Count-up frames after the first only redraw + push the two cards' dynamic
    // content (numbers/bars/sparks) -> much faster than a full-screen redraw.
    if (!full) {
        display::drawDashboardBands(gShownCur, gShownWk, curPop, wkPop, curGlow, wkGlow);
        return;
    }
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

// Climb time for a given % delta: STEP_MS per 1%, capped, min one frame.
static uint32_t climbMs(float deltaPct) {
    uint32_t ms = (uint32_t)((int)roundf(fabsf(deltaPct))) * STEP_MS;
    if (ms > MAX_MS) ms = MAX_MS;
    if (ms == 0)     ms = 1;              // a 0% change still needs one frame to land
    return ms;
}

// Begin a count-up to the new poll values. `fromZero` (manual refresh / wake)
// replays the full growth from 0%; otherwise (periodic poll) only the delta from
// what's on screen is animated — so e.g. 25%->26% just grows 1% and pops. The
// two cards animate sequentially (Current first, then Weekly); see stepCountUp.
void startCountUp(float cur, float wk, bool fromZero) {
    gStartCur = fromZero ? 0.0f : gShownCur;
    gStartWk  = fromZero ? 0.0f : gShownWk;
    // A reset (usage drops when the 5h/7d window rolls over) must not animate a
    // backwards shrink -> snap that card straight to the new (lower) value.
    if (cur < gStartCur) gStartCur = cur;
    if (wk  < gStartWk ) gStartWk  = wk;
    gShownCur = gStartCur; gShownWk = gStartWk;   // draw the first frame at the start
    gTgtCur = cur; gTgtWk = wk;
    gPopCur = (cur > gStartCur + 0.5f);           // pop a card only if it grew >=1%
    gPopWk  = (wk  > gStartWk  + 0.5f);
    gDurCur = climbMs(cur - gStartCur);
    gDurWk  = climbMs(wk  - gStartWk);
    gAnimStart = millis();
    gAnimating = true;
    gNeedFullDash = true;     // first frame paints the whole dashboard; rest = bands
}

// Ease-out (cubic): fast start, decelerating to the target. Front-loads the
// motion so 2.6s feels snappier. Note: the number then skips at the start and
// creeps at the end (the bar stays smooth since it's continuous).
static inline float easeOut(float t) {
    float u = 1.0f - t;
    return 1.0f - u * u * u;
}

// Advance one animation frame and draw it. Both cards climb in PARALLEL from
// t=0 (each over its own gDur) with an ease-out (fast start, slow finish). No
// fanfare during the climb; only AFTER landing (100%) does the burst fire
// (popI=1 -> 0 over FAN_MS, drawSparks spreading as it fades). popCurI/popWkI
// 0..1 drives the bar glow + sparks. The climb value is continuous so the % text
// ticks 1% at a time while the bar glides per-pixel. Clears gAnimating once both
// fades finish.
void stepCountUp() {
    uint32_t el = millis() - gAnimStart;
    // Hold the start frame (bars at their start value) for HOLD_MS before the
    // climb begins, so a fresh refresh doesn't jump straight into motion.
    uint32_t elc = (el > HOLD_MS) ? el - HOLD_MS : 0;
    float popCurI = 0.0f, popWkI = 0.0f;     // spark intensity
    float glowCurI = 0.0f, glowWkI = 0.0f;   // bar/number white-glow (lags by GLOW_DELAY)
    bool full = gNeedFullDash;     // first frame full-redraws; the rest push bands
    gNeedFullDash = false;

    // Hold back the final 1% so the last number tick lands WITH the fanfare burst.
    float climbCur = (gTgtCur - gStartCur >= 1.0f) ? gTgtCur - 1.0f : gTgtCur;
    if (elc < gDurCur) {                             // Current climbing to target-1
        float p = easeOut((float)elc / gDurCur);
        gShownCur = gStartCur + (climbCur - gStartCur) * p;
    } else {                                         // landed -> rest, then burst+fade
        uint32_t since = elc - gDurCur;
        gShownCur = (since >= REST_MS) ? gTgtCur : climbCur;   // final +1 ticks at the burst
        if (gPopCur && since >= REST_MS && since < REST_MS + FAN_MS)
            popCurI = 1.0f - (float)(since - REST_MS) / FAN_MS;
        if (gPopCur && since >= REST_MS + GLOW_DELAY && since < REST_MS + GLOW_DELAY + FAN_MS)
            glowCurI = 1.0f - (float)(since - REST_MS - GLOW_DELAY) / FAN_MS;
    }

    float climbWk = (gTgtWk - gStartWk >= 1.0f) ? gTgtWk - 1.0f : gTgtWk;
    if (elc < gDurWk) {                              // Weekly climbing to target-1
        float p = easeOut((float)elc / gDurWk);
        gShownWk = gStartWk + (climbWk - gStartWk) * p;
    } else {                                         // landed -> rest, then burst+fade
        uint32_t since = elc - gDurWk;
        gShownWk = (since >= REST_MS) ? gTgtWk : climbWk;      // final +1 ticks at the burst
        if (gPopWk && since >= REST_MS && since < REST_MS + FAN_MS)
            popWkI = 1.0f - (float)(since - REST_MS) / FAN_MS;
        if (gPopWk && since >= REST_MS + GLOW_DELAY && since < REST_MS + GLOW_DELAY + FAN_MS)
            glowWkI = 1.0f - (float)(since - REST_MS - GLOW_DELAY) / FAN_MS;
    }

    // The glow ends last (REST + GLOW_DELAY + FAN); keep animating until then.
    uint32_t endCur = gDurCur + (gPopCur ? REST_MS + GLOW_DELAY + FAN_MS : 0);
    uint32_t endWk  = gDurWk  + (gPopWk  ? REST_MS + GLOW_DELAY + FAN_MS : 0);
    if (elc >= endCur && elc >= endWk) gAnimating = false;

    drawDashFrame(popCurI, popWkI, glowCurI, glowWkI, full);
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
    // The no-PIN boot path skips enterUnlock(), so touch (and the Home-button
    // callback) may not be initialised yet. Bring it up here if needed.
    if (!touch::available()) gTouchOn = touch::begin();
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

// Keep the boot splash animating (on the spare core) while a blocking call runs.
void bootAnimTaskFn(void*) {
    while (gBootAnimRun) {
        display::drawBootBusy(gBootFrame++);
        delay(20);
    }
    gBootAnimDone = true;
    vTaskDelete(nullptr);
}

// net::connectMulti() blocks on wm.run() for several seconds; run the boot
// animation on the other core meanwhile so the screen doesn't freeze.
bool connectWithAnim(const String ssids[], const String passwords[], int n) {
    gBootAnimDone = false;
    gBootAnimRun  = true;
    xTaskCreatePinnedToCore(bootAnimTaskFn, "bootanim", 8192, nullptr, 1, nullptr, 0);

    bool ok = net::connectMulti(ssids, passwords, n);

    // Stop the task and wait for its final frame to finish before the main core
    // draws again (only one core may touch the shared canvas at a time).
    gBootAnimRun = false;
    uint32_t t0 = millis();
    while (!gBootAnimDone && millis() - t0 < 200) delay(5);
    return ok;
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

    for (int f = 0; f <= display::SPLASH_REST_Y; f += 2) {  // splash slides into place
        display::drawSplash(f);
        delay(35);
    }
    // No static hold here: factoryResetRequested() animates the splash (bob +
    // loading bar) for its wait window, so the motion continues seamlessly.

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
    if (n == 0 || !connectWithAnim(ssids, passwords, n)) {
        Serial.println("[wifi] connect failed -> setup mode");
        enterSetup();
        return;
    }
    gSsid = net::ssid();

    // PIN-encrypted token -> ask for the PIN; otherwise load it and go straight
    // to Running (no unlock screen, the gift-friendly default).
    if (credentials::tokenNeedsPin()) {
        enterUnlock();
    } else {
        String tok;
        if (credentials::loadToken(tok)) {
            gToken = tok;
            enterRunning();
        } else {
            Serial.println("[token] device-key load failed -> setup mode");
            enterSetup();
        }
    }
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
                        // gPollAnimate is true for a manual (Home) refresh, false
                        // for the periodic poll -> manual replays from 0, periodic
                        // grows only the delta.
                        startCountUp(gPollResult.util5h, gPollResult.util7d, gPollAnimate);
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

            // NTP usually syncs a few seconds after boot, so the first dashboard
            // shows "Resets in --". Re-render once the wall clock becomes valid so
            // the countdown fills in without waiting for the next poll / refresh.
            if (!gClockValid && time(nullptr) >= 1000000000L) {
                gClockValid = true;
                gPendTimeRender = true;
            }
            if (gPendTimeRender && !gAnimating && gPage == PAGE_DASH && gLastUsage.ok) {
                gPendTimeRender = false;
                renderCurrentView();
            }

            // Trigger a poll: Home button (animated) or the periodic interval.
            // Ignore Home while the count-up is still playing -> no jarring instant
            // re-refresh the moment the animation ends (release + press again to refresh).
            if (homeFired && !gAnimating) {
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
                delay(2);                            // partial redraw is cheap now
#if CUM_FPS_DEBUG
                static uint32_t lastUs = 0, accUs = 0; static int nf = 0;
                uint32_t now = micros();
                if (lastUs && (now - lastUs) < 500000) {   // skip cross-animation gaps
                    accUs += now - lastUs;
                    if (++nf >= 20) {
                        Serial.printf("[fps] count-up: %.1f fps (%.1f ms/frame)\n",
                                      1000000.0f * nf / accUs, accUs / 1000.0f / nf);
                        accUs = 0; nf = 0;
                    }
                }
                lastUs = now;
#endif
            } else if (gPollRunning && gPollAnimate && gPage == PAGE_DASH) {
                display::drawRefreshAnim(gAnimFrame++);
                delay(120);                          // slower bob
            } else {
                delay(16);   // idle ~60Hz: poll the Home button often so short taps register
            }
            break;
        }
    }
}
