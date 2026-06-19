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
#include "input/Proximity.h"
#include "input/Touch.h"
#include "net/Api.h"
#include "net/Net.h"
#include "net/Portal.h"
#include "power/Power.h"
#include "secure/CredentialStore.h"
#include "secure/Storage.h"

namespace {

enum class State { Setup, Unlock, Running, TouchTest };
State    gState;
String   gSsid;          // remembered for the status screen
// Saved WiFi list kept in RAM after boot so runtime roaming (home<->office) can
// rescan + re-join the strongest present network without a reboot. Populated in
// setup() from the credential store.
String   gSsids[CUM_WIFI_MAX], gPws[CUM_WIFI_MAX];
int      gWifiN = 0;
String   gToken;         // decrypted OAuth token, held in RAM while running
uint32_t gLastPoll = 0;  // millis() of the last API poll
bool     gClockValid     = false;  // wall clock (NTP) has synced at least once
bool     gPendTimeRender = false;  // re-render once NTP syncs (refresh "Resets in --")

String   gPin;           // PIN digits typed on the touch keypad
bool     gTouchOn = false;
bool     gProxOn  = false;   // LTR-553 proximity available -> wake-on-approach
bool     gWasTouched = false;

// --- Backlight / inactivity -------------------------------------------------
uint32_t gLastInput  = 0;     // millis() of last input (touch/button)
bool     gManualOff  = false; // user forced backlight off via IO16
uint8_t  gActiveBright = 200; // brightness when awake (left-edge drag sets it)
int      gCurBright  = -1;    // last applied brightness (cache; -1 = unset)
uint32_t gFadeLast   = 0;     // millis() of the last brightness fade step
uint32_t gDimMs      = 60000;   // idle -> dim (settable in Settings; 0 = never)
uint32_t gOffMs      = 120000;  // idle -> off (settable in Settings; 0 = never)
constexpr uint8_t  DIM_LEVEL = 153;      // 60% of 255
// Ease brightness toward the target instead of snapping, so dim/off/wake fade.
// Brightening (wake) is snappier than darkening; both are full-scale durations.
constexpr uint32_t FADE_IN_MS  = 180;
constexpr uint32_t FADE_OUT_MS = 400;
// On USB power the screen dims after DIM_MS but never turns off / sleeps --
// suits a desk monitor that stays plugged in, while still saving the panel a
// little. Set true to honor the full idle-off timeout on USB too.
constexpr bool     SLEEP_WHEN_CHARGING = false;
bool     gOnUsb = false;      // cached USB-power state (refreshed ~1/s in applyBacklight)
// Battery-life estimate: recorded at the moment USB is unplugged, then the drain
// since (percent dropped / time elapsed) gives a rough %/h and time-left. Only
// meaningful on battery; -1 = on USB / not tracking.
int      gDischStartPct = -1;
uint32_t gDischStartMs  = 0;

void noteInput() { gLastInput = millis(); }

// Panel power, tracked so sleep/wake stay idempotent (the wake path is reached
// from several places). Backlight 0 only kills the LED; sleeping the panel
// controller saves a few mA more. wake() must run before any drawing.
bool gPanelSlept = false;
void panelSleep() { if (!gPanelSlept) { display::sleep(); gPanelSlept = true; } }
void panelWake()  { if (gPanelSlept) { display::wake();  gPanelSlept = false; } }

// Bring the radio back after a screen-off power-down, non-blocking: target the
// last-good AP directly (no scan) so a same-place off/on re-associates in ~1 s
// without freezing the wake. A genuine location change won't find that AP -- the
// poll-fail path then escalates to a full rescan-roam.
void radioWake() {
    int i = storage::lastWifi();                       // 0xFF when unrecorded
    if (i >= 0 && i < gWifiN) net::radioOn(gSsids[i], gPws[i]);
    else                      net::radioOn(String(), String());
}

bool screenAsleep() {
    if (gManualOff) return true;
    // On USB we only dim, never sleep, so input keeps acting normally.
    if (gOnUsb && !SLEEP_WHEN_CHARGING) return false;
    return gOffMs && millis() - gLastInput > gOffMs;   // 0 = never sleep
}

// Drive the backlight from manual-off / inactivity, easing toward the target so
// dim/off/wake transitions fade. Called at the top of loop.
void applyBacklight() {
    // Refresh the cached USB-power state. charging() hits the PMU over I2C, so
    // sample it ~1/s rather than every loop. On a plug/unplug, reset the idle
    // timer: while on USB the timer keeps running (only the off is suppressed),
    // so without this an unplug after a long idle would sleep the screen
    // instantly. Resetting gives the full timeout from the unplug moment.
    {
        static uint32_t usbCheck = 0;
        static bool     prevUsb  = false;
        static bool     usbInit  = false;
        uint32_t now = millis();
        if (gCurBright < 0 || now - usbCheck > 1000) {
            usbCheck = now;
            gOnUsb   = power::charging();
            if (!usbInit)            { prevUsb = gOnUsb; usbInit = true; }
            else if (gOnUsb != prevUsb) { prevUsb = gOnUsb; noteInput(); }
        }
    }
    bool noSleep = gOnUsb && !SLEEP_WHEN_CHARGING;   // USB: dim but never off

    int target;
    if (gManualOff) {
        target = 0;
    } else if (gState == State::Setup) {
        target = gActiveBright;            // keep the setup screen readable
    } else {
        uint32_t idle = millis() - gLastInput;
        if      (gOffMs && idle > gOffMs && !noSleep) target = 0;          // sleep (battery)
        else if (gDimMs && idle > gDimMs)             target = DIM_LEVEL;  // dim (both)
        else                                          target = gActiveBright;
    }

    uint32_t now = millis();
    if (gCurBright < 0) {                  // first call: snap, no fade
        gCurBright = target;
        gFadeLast  = now;
        display::setBrightness(target);
        return;
    }
    if (gCurBright == target) { gFadeLast = now; return; }

    // Step toward target at a fixed full-scale rate (255 over FADE_*_MS) so the
    // fade speed is steady regardless of loop cadence or travel distance.
    uint32_t dt  = now - gFadeLast;
    gFadeLast    = now;
    uint32_t dur = (target > gCurBright) ? FADE_IN_MS : FADE_OUT_MS;
    int step = (int)((long)255 * (long)dt / (long)dur);
    if (step < 1) step = 1;
    int span = target - gCurBright;
    if (span > 0) gCurBright = (step >= span)  ? target : gCurBright + step;
    else          gCurBright = (step >= -span) ? target : gCurBright - step;
    display::setBrightness((uint8_t)gCurBright);
}

// --- Settings (brightness / dim / off) --------------------------------------
// Discrete presets cycled by the Home button on each settings row. Times in
// seconds; 0 = "Never". Persisted to NVS as a small blob so they survive reboot.
const uint8_t  kBrightSteps[] = {25, 64, 128, 191, 255};       // ~10/25/50/75/100%
const uint16_t kDimSteps[]    = {15, 30, 60, 120, 300, 0};
const uint16_t kOffSteps[]    = {30, 60, 120, 300, 600, 0};

uint8_t  nextBright(uint8_t cur) {
    const int n = sizeof(kBrightSteps);
    for (int i = 0; i < n; i++) if (kBrightSteps[i] == cur) return kBrightSteps[(i + 1) % n];
    for (int i = 0; i < n; i++) if (kBrightSteps[i] >  cur) return kBrightSteps[i];
    return kBrightSteps[0];
}
uint16_t nextStep(const uint16_t* a, int n, uint16_t cur) {
    for (int i = 0; i < n; i++) if (a[i] == cur) return a[(i + 1) % n];
    return a[0];
}
String fmtSecs(uint16_t s) {
    if (s == 0)       return "Never";
    if (s % 60 == 0)  return String(s / 60) + "m";
    return String(s) + "s";
}
String fmtBrightPct(uint8_t b) { return String((b * 100 + 127) / 255) + "%"; }

struct UiSettings { uint8_t bright; uint16_t dimSec; uint16_t offSec; };
void saveUiSettings() {
    UiSettings s{ gActiveBright, (uint16_t)(gDimMs / 1000), (uint16_t)(gOffMs / 1000) };
    storage::putBlob("ui", (const uint8_t*)&s, sizeof(s));
}
void loadUiSettings() {
    UiSettings s;
    if (storage::getBlob("ui", (uint8_t*)&s, sizeof(s)) == sizeof(s)) {
        if (s.bright >= 25) gActiveBright = s.bright;
        gDimMs = (uint32_t)s.dimSec * 1000;
        gOffMs = (uint32_t)s.offSec * 1000;
    }
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

// Top-bar wall clock as "AM/PM h:mm" in KST. configTime() runs in UTC (reset
// countdowns need a raw epoch), so shift +9h only for display.
String fmtClock(time_t now) {
    if (now < 1000000000L) return "--:--";        // NTP hasn't synced yet
    time_t kst = now + 9L * 3600;
    struct tm tmv;
    gmtime_r(&kst, &tmv);
    int h12 = tmv.tm_hour % 12;
    if (h12 == 0) h12 = 12;
    char buf[16];
    snprintf(buf, sizeof(buf), "%s %d:%02d",
             tmv.tm_hour < 12 ? "AM" : "PM", h12, tmv.tm_min);
    return String(buf);
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
// First boot keeps the SAME spinner from WiFi-connect through the first data
// poll (so it reads as one continuous load, not "loading... loading again");
// cleared once that first poll is consumed. Later refreshes use drawRefreshAnim.
bool          gBootLoading  = true;

bool factoryResetRequested() {
    pinMode(TDS3_PIN_BTN_IO16, INPUT_PULLUP);
    constexpr uint32_t HOLD_MS = 1500;            // hold IO16 this long to reset
    // The reset gesture is "hold IO16 while powering on", so the button is
    // already down by the time we reach here. If it isn't, skip the wait
    // entirely (saves ~2.5 s on every normal boot). delay() lets the pull-up
    // settle before the first read.
    delay(20);
    if (!io16Down()) return false;
    uint32_t held = millis();
    while (io16Down()) {
        uint32_t dt = millis() - held;
        if (dt >= HOLD_MS) return true;           // held long enough -> reset
        display::drawResetHold((float)dt / HOLD_MS);
        delay(20);
    }
    return false;                                 // released before the hold completed
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
uint32_t      gLastGoodMs  = 0;      // millis() of the last successful poll (stale age)
int           gDropStreak  = 0;      // consecutive failed polls while the link is down
                                     // (drives the cheap-reconnect -> rescan-roam escalation)

// After a failed poll, retry with exponential backoff (fast at first so a blip
// recovers quickly, capped at the normal cadence so a sustained outage doesn't
// hammer). Cleared back to 0 -> normal interval on the next success.
uint32_t      gPollBackoffMs = 0;
constexpr uint32_t POLL_RETRY_MIN_MS = 5000;
constexpr uint32_t POLL_RETRY_MAX_MS = CUM_POLL_INTERVAL_MS;

// Set by wakeShow() after a non-blocking radioWake(): poll the instant the link
// comes up instead of waiting out the backoff floor. The backoff still bounds the
// worst case (link never associates -> poll fails -> roam escalation kicks in).
bool          gWakePollPending = false;

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

// IO12 cycles pages: dashboard -> detail -> history -> battery.
enum { PAGE_DASH = 0, PAGE_DETAIL = 1, PAGE_HISTORY = 2, PAGE_BATTERY = 3, PAGE_COUNT = 4 };
int           gPage = PAGE_DASH;
bool          gMenuOpen = false;   // Home-button nav menu overlay (modal while up)
int           gMenuCursor = 0;     // scrollbar cursor row in the menu (Home selects it)
bool          gSettingsOpen = false;
int           gSetCursor = 0;      // scrollbar cursor row in the settings screen
constexpr int SET_ROWS = 4;        // Brightness, Dim after, Off after, Back

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

// Limit projection from the window position, not a recent slope. Each window
// (5h / 7d) resets at a known epoch, so the average pace this window is
// (used% / time-elapsed-in-window). Extrapolate that pace to the reset:
//   peak = projected utilization at reset (0..100+, -1 if unknown)
//   eta  = minutes until 100% IF that happens before the reset, else -1
// This is honest about the fixed window: a slow 5h climb that won't reach 100%
// before it rolls over yields eta=-1 (and a peak well under 100), instead of a
// nonsensical "17h to limit" on a 5-hour window.
struct Burn { int eta5 = -1, eta7 = -1; int peak5 = -1, peak7 = -1; };

namespace {
constexpr long WIN_5H = 5L * 3600;          // 5-hour window
constexpr long WIN_7D = 7L * 24 * 3600;     // weekly (7-day) window

void projectSeries(float util, long resetEpoch, long windowSec, time_t now,
                   int& eta, int& peak) {
    eta = -1; peak = -1;
    if (now < 1000000000L || resetEpoch <= 0) return;   // need NTP + a reset header
    long remain = resetEpoch - (long)now;
    if (remain <= 0 || remain > windowSec) return;       // expired / implausible
    long elapsed = windowSec - remain;
    if (elapsed < 300) return;                           // <5 min in: too early to trust
    if (util < 0.5f) { peak = 0; return; }               // basically idle this window
    float ratePerSec = util / (float)elapsed;            // %/sec average so far
    float proj = util + ratePerSec * remain;             // projected % at reset
    peak = (int)(proj + 0.5f);
    if (proj >= 100.0f) {                                // will hit the cap before reset
        float secsTo100 = (100.0f - util) / ratePerSec;
        eta = (int)(secsTo100 / 60.0f + 0.5f);
        if (eta < 0) eta = 0;
        if (eta > 99 * 60) eta = 99 * 60;
    }
}
}  // namespace

Burn computeBurn() {
    Burn b;
    time_t now = time(nullptr);
    projectSeries(gLastUsage.util5h, gLastUsage.reset5h, WIN_5H, now, b.eta5, b.peak5);
    projectSeries(gLastUsage.util7d, gLastUsage.reset7d, WIN_7D, now, b.eta7, b.peak7);
    return b;
}

// Compact "1h 20m" / "45m" / "<1m" from whole minutes.
String fmtEta(int mins) {
    if (mins < 0) return "--";
    if (mins < 1) return "<1m";
    int h = mins / 60, m = mins % 60;
    if (h >= 24) return String(h / 24) + "d " + String(h % 24) + "h";
    if (h > 0)   return String(h) + "h " + String(m) + "m";
    return String(m) + "m";
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

// Rough battery state derived from the drain since the last unplug. A ballpark
// only: the SY6970 has no fuel gauge, so % is estimated from cell voltage, and
// the Li-ion curve is flat in the middle -> the linear extrapolation to 0% can be
// well off. `tracking` is false on USB; `measuring` is true until there's enough
// drop + time for a trustworthy rate.
// Battery-% history for the Battery page graph. One sample per gBattSampleMs while
// on battery, reset on unplug. Stores minutes-since-unplug + percent so the graph
// x-axis is real elapsed time. On overflow, decimate (drop every other point) and
// double the interval, so an arbitrarily long discharge still fits the buffer.
// (Declared here -- ahead of battEstimate -- because the drain rate is now read
// off this same recorded history rather than a single since-unplug average.)
constexpr int BATT_HIST_N = 96;
uint16_t gBattMin[BATT_HIST_N];
uint8_t  gBattPct[BATT_HIST_N];
int      gBattHistCount = 0;
uint32_t gBattSampleMs  = 60000;
uint32_t gBattLastSample = 0;

struct BattEst {
    int   pct       = 100;
    bool  tracking  = false;
    bool  measuring = false;
    float ratePerHr = 0.0f;   // %/hour drain (valid when tracking && !measuring)
    int   etaMin    = -1;     // minutes to 0% (-1 if unknown)
};

// Trailing window for the drain rate. The SY6970 has no fuel gauge, so % is an
// open-circuit-voltage estimate (Power.cpp curve). For the first ~10-20 min after
// unplug the cell voltage relaxes down from its charge level, which reads as a
// fast % drop that is mostly NOT real energy use. A since-unplug average folds
// that transient in and over-states the drain (and under-states time-left); a
// sliding window over the recorded history slides past it and tracks the true
// steady-state rate instead.
constexpr int BATT_RATE_WINDOW_MIN = 30;   // look back this many minutes
constexpr int BATT_RATE_MIN_SPAN_MIN = 10; // need this much span before trusting it

BattEst battEstimate() {
    BattEst e;
    e.pct = power::percent();
    if (gDischStartPct < 0) return e;                        // on USB / not tracking
    e.tracking = true;
    if (gBattHistCount < 2) { e.measuring = true; return e; }

    // Slope from the newest sample back to the oldest one still inside the window.
    int last   = gBattHistCount - 1;
    int nowMin = gBattMin[last];
    int start  = last;
    while (start > 0 && nowMin - gBattMin[start - 1] <= BATT_RATE_WINDOW_MIN) start--;
    int spanMin = nowMin - gBattMin[start];
    int drop    = (int)gBattPct[start] - (int)gBattPct[last];
    if (spanMin < BATT_RATE_MIN_SPAN_MIN || drop < 2) { e.measuring = true; return e; }
    float rate = (float)drop * 60.0f / (float)spanMin;       // %/hour
    if (rate < 0.1f) { e.measuring = true; return e; }
    e.ratePerHr = rate;
    float etaHr = (float)e.pct / rate;
    if (etaHr > 99) etaHr = 99;
    e.etaMin = (int)(etaHr * 60.0f + 0.5f);
    return e;
}

// Battery time-left for the Detail row: "" while charging/untracked,
// "measuring..." until the rate settles, else "~Hh Mm (R%/h)".
String fmtBattLeft() {
    BattEst e = battEstimate();
    if (!e.tracking)  return "";
    if (e.measuring)  return "measuring...";
    char b[32];
    snprintf(b, sizeof(b), "~%dh %dm (%.1f%%/h)",
             e.etaMin / 60, e.etaMin % 60, e.ratePerHr);
    return String(b);
}

void battHistReset() {
    gBattHistCount = 0;
    gBattSampleMs  = 60000;
    gBattLastSample = 0;
}

void battHistSample() {
    if (gDischStartPct < 0) return;                          // on USB: not tracking
    uint32_t now = millis();
    if (gBattHistCount != 0 && now - gBattLastSample < gBattSampleMs) return;
    gBattLastSample = now;
    if (gBattHistCount >= BATT_HIST_N) {                     // decimate: keep even idx
        int j = 0;
        for (int i = 0; i < gBattHistCount; i += 2) {
            gBattMin[j] = gBattMin[i]; gBattPct[j] = gBattPct[i]; j++;
        }
        gBattHistCount = j;
        gBattSampleMs *= 2;
    }
    uint32_t em = (now - gDischStartMs) / 60000UL;
    gBattMin[gBattHistCount] = em > 65535U ? 65535U : (uint16_t)em;
    gBattPct[gBattHistCount] = (uint8_t)power::percent();
    gBattHistCount++;
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
    // When a limit is near, the status line turns actionable: a burn-rate ETA
    // (5h window takes priority -- it's the one users actually hit) replaces the
    // playful line. Otherwise the playful status stays.
    {
        Burn b = computeBurn();
        // The server's 5h status header is the authoritative throttle signal --
        // trust it over the utilization % (which can read <100 while limited).
        if      (gLastUsage.limited)           d.status = "5h limit reached";
        else if (gLastUsage.util5h >= 100.0f)  d.status = "5h limit reached";
        else if (b.eta5 >= 0 && b.eta5 <= 120) d.status = "~" + fmtEta(b.eta5) + " to 5h limit";
        else if (gLastUsage.util7d >= 100.0f)  d.status = "7d limit reached";
        else if (b.eta7 >= 0 && b.eta7 <= 180) d.status = "~" + fmtEta(b.eta7) + " to 7d limit";
    }
    // Stale data is the priority signal: the dot (top-right) flags it, and the
    // status line shows how old the shown numbers are so they aren't mistaken
    // for live ones during a network/API outage.
    if (gStale && gLastGoodMs) {
        d.status = "stale - " + String((millis() - gLastGoodMs) / 60000) + "m ago";
    }
    d.clock        = fmtClock(now);
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
        d.battEst  = fmtBattLeft();
        d.reset5h  = fmtCountdown(gLastUsage.reset5h, now);
        d.reset7d  = fmtCountdown(gLastUsage.reset7d, now);
        d.uptime   = fmtUptime();
        display::drawDetail(d);
        return;
    }
    if (gPage == PAGE_HISTORY) {
        Burn b = computeBurn();
        display::drawHistory(gH5, gH7, gHistCount, b.eta5, b.eta7, b.peak5, b.peak7);
        return;
    }
    if (gPage == PAGE_BATTERY) {
        BattEst e = battEstimate();
        display::BatteryPage bp;
        bp.pct       = e.pct;
        bp.charging  = power::charging();
        bp.tracking  = e.tracking;
        bp.measuring = e.measuring;
        bp.ratePerHr = e.ratePerHr;
        bp.etaMin    = e.etaMin;
        bp.histMin   = gBattMin;
        bp.histPct   = gBattPct;
        bp.histCount = gBattHistCount;
        display::drawBatteryPage(bp);
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

// Wake from sleep: show the current view, and refetch only if the data is no
// longer fresh (last poll older than one poll interval). A quick on/off within
// ~1 min skips the redundant API call + refresh animation -- the data is already
// current, so just show it and let the backlight fade it in.
bool roamReconnect();   // defined after connectWithAnim (needs the boot-anim helper)

void wakeShow() {
    noteInput();
    panelWake();          // panel was slept on screen-off -> restore it before drawing
    renderCurrentView();  // show the last data immediately -- never block the turn-on
    // The radio is powered down while asleep on battery. Re-associate non-blocking
    // (targeted, no scan), so the screen is already up; a poll a moment later
    // refreshes once the link settles. A location change is handled by the
    // poll-fail rescan-roam, not here, so wake never stalls on a 2-4 s scan.
    if (!net::isConnected()) {
        radioWake();
        gWakePollPending = true;          // poll the instant the link settles...
        gPollBackoffMs = 2000;            // ...but no later than ~2 s, which also drives
        gLastPoll = millis();             // the roam escalation if it never associates
    } else if (millis() - gLastPoll >= CUM_POLL_INTERVAL_MS) {
        requestPoll(true);
        gLastPoll = millis();
        if (gPage == PAGE_DASH) display::drawRefreshAnim(gAnimFrame++);
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
    if (!prox::available())  gProxOn  = prox::begin();   // wake-on-approach
    touch::homePressed();               // discard any press from the unlock phase
    portal::stop();
    configTime(0, 0, CUM_NTP_SERVER);   // UTC epoch for reset countdowns + TLS cert dates
    gPage = PAGE_DASH;
    gMenuOpen = false;
    gSettingsOpen = false;
    gManualOff = false;
    noteInput();
    pinMode(TDS3_PIN_BTN_IO12, INPUT_PULLUP);
    pinMode(TDS3_PIN_BTN_IO16, INPUT_PULLUP);

    // TLS cert validation (api::poll pins the CA) needs a real clock, so wait
    // briefly for the first NTP sync before polling -- otherwise the first
    // handshake fails "cert not yet valid" until the clock catches up. Keep the
    // boot spinner moving meanwhile; if NTP is slow/blocked, poll anyway and let
    // the backoff retry once time is set.
    {
        uint32_t t0 = millis();
        while (time(nullptr) < 1000000000L && millis() - t0 < 6000) {
            display::drawBootBusy(gBootFrame++);
            delay(40);
        }
        if (time(nullptr) >= 1000000000L) gClockValid = true;
    }

    Serial.println("[run] unlocked; polling Anthropic API");
    display::drawBootBusy(gBootFrame++);  // keep the boot spinner; loop continues it
    requestPoll(true);                    // first poll, animated
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
bool connectWithAnim(const String ssids[], const String passwords[], int n,
                     int preferredIdx) {
    gBootAnimDone = false;
    gBootAnimRun  = true;
    xTaskCreatePinnedToCore(bootAnimTaskFn, "bootanim", 8192, nullptr, 1, nullptr, 0);

    bool ok = net::connectMulti(ssids, passwords, n, preferredIdx);

    // Stop the task and wait for its final frame to finish before the main core
    // draws again (only one core may touch the shared canvas at a time).
    gBootAnimRun = false;
    uint32_t t0 = millis();
    while (!gBootAnimDone && millis() - t0 < 200) delay(5);
    return ok;
}

// Runtime WiFi roaming. When the link is down (typically a location change --
// office <-> home), rescan every saved network and join the strongest one that's
// actually present. preferredIdx = -1 forces the full scan (skips the fast
// last-AP probe that would just keep failing on the now-absent network). Reuses
// the boot connect+anim path so the screen shows the loading ring rather than
// freezing during the ~2-4 s scan. No-op when already connected or nothing saved.
// Only safe with no poll task in flight (it resets the STA + rescans).
bool roamReconnect() {
    if (net::isConnected()) return true;
    if (gWifiN <= 0) return false;
    Serial.println("[wifi] link down -> rescan to roam");
    bool ok = connectWithAnim(gSsids, gPws, gWifiN, -1);
    if (ok) {
        gSsid = net::ssid();
        // Remember the network that won so the next boot tries it first (no scan).
        for (int i = 0; i < gWifiN; i++)
            if (gSsids[i] == gSsid) { storage::setLastWifi((uint8_t)i); break; }
        Serial.printf("[wifi] roamed onto %s\n", gSsid.c_str());
    }
    return ok;
}

// Redraw the settings screen from the live values.
void drawSettingsNow() {
    display::drawSettings(gSetCursor, fmtBrightPct(gActiveBright).c_str(),
                          fmtSecs(gDimMs / 1000).c_str(), fmtSecs(gOffMs / 1000).c_str());
}

// Home pressed on a menu row: act on the cursor item.
void menuActivate(int cursor) {
    switch (display::menuRowItem(cursor)) {
        case display::MENU_REFRESH:  gMenuOpen = false; requestPoll(true); gLastPoll = millis(); break;
        case display::MENU_DETAIL:   gPage = PAGE_DETAIL;  gMenuOpen = false; break;
        case display::MENU_BATTERY:  gPage = PAGE_BATTERY; gMenuOpen = false; break;
        case display::MENU_HISTORY:  gPage = PAGE_HISTORY; gMenuOpen = false; break;
        case display::MENU_SETTINGS: gMenuOpen = false; gSettingsOpen = true; gSetCursor = 0; drawSettingsNow(); break;
        case display::MENU_EXIT:     gPage = PAGE_DASH;    gMenuOpen = false; break;
        default: break;
    }
    if (!gMenuOpen && !gSettingsOpen) renderCurrentView();
}

// Home pressed on a settings row: cycle that value (Back returns to the menu).
void settingsActivate(int cursor) {
    switch (cursor) {
        case 0:
            gActiveBright = nextBright(gActiveBright);
            gCurBright = gActiveBright; display::setBrightness(gActiveBright);
            break;
        case 1:
            gDimMs = (uint32_t)nextStep(kDimSteps, sizeof(kDimSteps)/sizeof(kDimSteps[0]),
                                        (uint16_t)(gDimMs / 1000)) * 1000;
            break;
        case 2:
            gOffMs = (uint32_t)nextStep(kOffSteps, sizeof(kOffSteps)/sizeof(kOffSteps[0]),
                                        (uint16_t)(gOffMs / 1000)) * 1000;
            break;
        default:  // Back
            gSettingsOpen = false; gMenuOpen = true; gMenuCursor = 4;   // back onto "Settings"
            display::drawMenu(gMenuCursor);
            return;
    }
    saveUiSettings();
    drawSettingsNow();
}

// Relative-swipe scrolling for the right band: the cursor moves by how far you
// drag, not where you tap. Grab anywhere in the band, then swipe up/down; a tap
// with no vertical travel does nothing. Returns true when the cursor changed.
bool scrubUpdate(bool touching, int tx, int ty, int rows, int& cursor) {
    static bool active = false, moved = false;
    static int  lastY = 0, accum = 0, lost = 0, p1 = 0;
    constexpr int STEP   = 7;    // px of vertical swipe per row (lower = more sensitive)
    constexpr int FIRST  = 3;    // smaller threshold for the FIRST row -> snappy start, no dead feel
    constexpr int GLITCH = 70;   // ignore a single-frame jump bigger than this (touch glitch)
    constexpr int GRACE  = 2;    // tolerate this many dropped-touch frames mid-swipe

    if (!touching) {
        if (active && ++lost <= GRACE) return false;   // brief contact dropout: keep the swipe alive
        active = false; lost = 0; p1 = 0; return false;   // release discards the last (lift-off) frame
    }
    lost = 0;
    if (!active) {
        if (!display::inScrollBand(tx, ty, rows)) return false;   // grab inside the band only
        active = true; moved = false; lastY = ty; accum = 0; p1 = 0;
        return false;                                             // anchor only -- no jump
    }
    int dy = ty - lastY;
    lastY = ty;
    if (dy > GLITCH || dy < -GLITCH) dy = 0;   // reject a glitchy coordinate spike (no sudden jump)
    // Apply movement one frame late: a frame's delta is committed only once the
    // next touch frame confirms the finger was still down. On release the held
    // frame is dropped -- which kills the lift-off coordinate shift that bumps the
    // cursor on finger-up. (Shorter 1-frame delay -> snappier start.)
    accum += p1;
    p1 = dy;
    bool changed = false;
    // At most one row per frame (if, not while), so a hard/fast push can't burst
    // several rows at once. The first row of a drag uses a smaller threshold so the
    // start feels responsive; later rows use the full STEP. Cap the leftover to one
    // STEP so a fast flick carries over at most one extra row, never a pile.
    int thresh = moved ? STEP : FIRST;
    if      (accum >=  thresh && cursor < rows - 1) { cursor++; accum -= thresh; changed = true; moved = true; }
    else if (accum <= -thresh && cursor > 0)        { cursor--; accum += thresh; changed = true; moved = true; }
    if (accum >  STEP) accum =  STEP;
    if (accum < -STEP) accum = -STEP;
    if (cursor == rows - 1 && accum > 0) accum = 0;   // don't bank over-drag at the ends
    if (cursor == 0 && accum < 0)        accum = 0;
    return changed;
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

    // Logo + wordmark sit at their rest position from the very first frame (no
    // slide-in). factoryResetRequested()/drawBootBusy keep them static and only
    // spin the loading ring.
    display::drawSplash(display::SPLASH_REST_Y);

#if CUM_TOUCH_TEST
    gTouchOn = touch::begin();
    gState = State::TouchTest;
    display::drawTouchTest(false, -1, -1, -1, -1);
    Serial.printf("[touchtest] touch=%d\n", gTouchOn);
    return;
#endif

    credentials::begin();
    loadUiSettings();        // brightness / dim / off times (after NVS is up)
    power::begin();
    if (factoryResetRequested()) {
        Serial.println("[reset] BOOT held -> wiping credentials");
        credentials::wipe();
    }
    if (!credentials::isProvisioned()) {
        enterSetup();
        return;
    }

    // Provisioned: load all remembered networks (kept in globals for runtime
    // roaming) and join the strongest.
    gWifiN = credentials::loadWifiList(gSsids, gPws, CUM_WIFI_MAX);
    int pref = storage::lastWifi();          // 0xFF when nothing recorded yet
    if (pref >= gWifiN) pref = -1;            // stale/absent index -> just scan

    // Retry the whole connect sequence before giving up: a transient empty scan
    // right after boot would otherwise drop us into setup even though a known
    // network is in range. Only the first attempt uses the fast direct-probe
    // path; later attempts force a fresh scan (pref = -1).
    bool connected = false;
    for (int attempt = 0; gWifiN > 0 && attempt < CUM_WIFI_CONNECT_RETRIES; attempt++) {
        connected = connectWithAnim(gSsids, gPws, gWifiN, attempt == 0 ? pref : -1);
        if (connected) break;
        Serial.printf("[wifi] connect attempt %d/%d failed\n",
                      attempt + 1, CUM_WIFI_CONNECT_RETRIES);
        delay(500);
    }
    if (!connected) {
        Serial.println("[wifi] connect failed -> setup mode");
        enterSetup();
        return;
    }
    gSsid = net::ssid();
    // Remember which network connected so the next boot tries it first (no scan).
    for (int i = 0; i < gWifiN; i++) {
        if (gSsids[i] == gSsid) { storage::setLastWifi((uint8_t)i); break; }
    }

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

    // Power: on the screen-off transition, drop the CPU to 80 MHz and -- in the
    // Running state -- also sleep the panel controller and power down the WiFi
    // radio. screenAsleep() is only true on battery (USB dims but never sleeps),
    // so this only fires on a battery idle-out; the awake transition restores all
    // three. We don't poll while asleep, so dropping WiFi costs nothing but the
    // ~2-4 s rescan on wake (handled by roamReconnect, idempotent if wakeShow
    // already reconnected). Skipped outside Running so Setup keeps its AP up.
    {
        static bool pmAsleep = false;
        bool a = screenAsleep();
        if (a != pmAsleep) {
            pmAsleep = a;
            setCpuFrequencyMhz(a ? 80 : 240);
            if (gState == State::Running) {
                if (a) {
                    gMenuOpen = false;              // don't resume into the menu on wake
                    gSettingsOpen = false;
                    panelSleep();
                    if (!gOnUsb) net::radioOff();   // battery only: no power gain on USB,
                                                    // and it would add wake-reconnect lag
                } else {
                    panelWake();
                    if (!net::isConnected()) radioWake();   // non-blocking re-associate
                }
            }
        }
    }
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

            // Reflect a USB plug/unplug on the battery icon promptly instead of
            // waiting for the next poll/refresh. gOnUsb is refreshed ~1/s in
            // applyBacklight; on a change, re-render the current view (which
            // re-reads power state). Defer while an animation/poll owns the
            // screen -- prevCharge isn't committed until we actually redraw, so
            // the change is picked up on the next idle iteration.
            static int prevCharge = -1;
            int chargeNow = gOnUsb ? 1 : 0;
            // Start tracking battery drain on unplug (chargeNow 0), stop on plug.
            auto trackBattery = [&]() {
                if (chargeNow == 0) {                       // unplugged -> start a fresh session
                    gDischStartPct = power::percent();
                    gDischStartMs  = millis();
                    battHistReset();
                    battHistSample();                       // seed the graph's first point
                } else {
                    gDischStartPct = -1;                    // on USB: stop tracking (keep last graph)
                }
            };
            if (prevCharge == -1) {
                prevCharge = chargeNow;                 // seed without a redraw
                trackBattery();                         // covers booting on battery
            } else if (prevCharge != chargeNow && !gAnimating && !gPollRunning) {
                prevCharge = chargeNow;
                trackBattery();
                renderCurrentView();
            }
            battHistSample();   // record a battery-% point if due (self-throttled)

            // Proximity wake: detect a far->near transition (a hand approaching).
            // Evaluated every iteration so the rising edge is never missed; only
            // used to wake from inactivity sleep, not a deliberate manual-off.
            static bool proxWas = false;
            bool proxNow  = gProxOn && prox::near();
            bool proxRise = proxNow && !proxWas;
            proxWas = proxNow;

            // IO16: wake if off. In the menu/settings it moves the cursor UP (wrap);
            // on the main screen it's the lock button (force backlight off).
            if (io16Pressed()) {
                if (asleep) {
                    gManualOff = false;
                    wakeShow();                      // refresh only if data is stale
                } else if (gSettingsOpen) {
                    noteInput();
                    gSetCursor = (gSetCursor + SET_ROWS - 1) % SET_ROWS;
                    drawSettingsNow();
                } else if (gMenuOpen) {
                    noteInput();
                    int n = display::menuRowCount();
                    gMenuCursor = (gMenuCursor + n - 1) % n;
                    display::drawMenu(gMenuCursor);
                } else {
                    gManualOff = true;               // main screen only: lock
                }
                delay(30);
                break;
            }

            if (asleep) {                            // wake on double-tap / IO12 / approach
                // IO12 wakes to the current view only; the press is consumed
                // here so it won't also advance the page. The next (released
                // then re-pressed) IO12 cycles pages as usual. Proximity wakes
                // only from inactivity sleep (not after a manual IO16 off).
                if (doubleTapDetected() || io12Pressed() || (proxRise && !gManualOff)) {
                    gManualOff = false;
                    wakeShow();                      // refresh only if data is stale
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
            // Home -> open/close the nav menu. One event per press; re-arm after a
            // >300ms release (the Home callback re-fires while the button is held).
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

            // Home drives the nav: closed -> open menu; in menu -> select cursor
            // row; in settings -> cycle that value (Back returns). Selection is
            // Home-only now -- the scrollbar just moves the cursor.
            if (homeFired) {
                if (gSettingsOpen) {
                    settingsActivate(gSetCursor);
                } else if (gMenuOpen) {
                    menuActivate(gMenuCursor);
                } else {
                    if (gAnimating) finishCountUp();
                    gMenuOpen = true; gMenuCursor = 0;
                    display::drawMenu(gMenuCursor);
                }
            }

            // Settings overlay (modal): right scrollbar moves the cursor; Home (above)
            // cycles the value. IO12 backs out to the menu.
            if (gSettingsOpen) {
                static bool sWas = false;
                bool sPrev = sWas; sWas = touching;
                if (io12) {                                   // IO12: cursor down (wrap)
                    gSetCursor = (gSetCursor + 1) % SET_ROWS;
                    drawSettingsNow();
                } else {
                    if (scrubUpdate(touching, tx, ty, SET_ROWS, gSetCursor)) drawSettingsNow();
                    if (touching && !sPrev) {                 // precise word tap
                        int wr = display::settingsWordRow(tx, ty);
                        if (wr >= 0) { gSetCursor = wr; settingsActivate(wr); }
                    }
                }
                delay(16);
                break;
            }

            // Menu overlay (modal): right scrollbar moves the cursor; Home (above)
            // selects. IO12 closes. No tap-to-select -- selection is Home-only.
            if (gMenuOpen) {
                static bool mWas = false;
                bool mPrev = mWas; mWas = touching;
                if (io12) {                                   // IO12: cursor down (wrap)
                    gMenuCursor = (gMenuCursor + 1) % display::menuRowCount();
                    display::drawMenu(gMenuCursor);
                } else {
                    if (scrubUpdate(touching, tx, ty, display::menuRowCount(), gMenuCursor))
                        display::drawMenu(gMenuCursor);
                    if (touching && !mPrev) {                 // precise word tap -> select
                        int wr = display::menuWordRow(tx, ty);
                        if (wr >= 0) { gMenuCursor = wr; menuActivate(wr); }
                    }
                }
                delay(16);
                break;
            }

            // (nothing open) IO12 cycles pages as a quick secondary path.
            if (io12) {
                gPage = (gPage + 1) % PAGE_COUNT;
                if (gAnimating) finishCountUp();     // don't carry an anim across pages
                renderCurrentView();
            }

            // Consume a finished poll. Keep the last good data on failure.
            if (gPollDone) {
                gPollDone = false;
                gBootLoading = false;   // boot-spinner phase ends at the first result
                if (gPollResult.ok) {
                    gLastUsage = gPollResult;        // fresh good data
                    gStale = false;
                    gLastGoodMs = millis();
                    gPollBackoffMs = 0;              // back to the normal cadence
                    gDropStreak = 0;                 // link healthy again
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
                if (!gPollResult.ok) {
                    // Back off the next retry (5s -> 10s -> ... -> interval).
                    gPollBackoffMs = gPollBackoffMs
                        ? min(gPollBackoffMs * 2, POLL_RETRY_MAX_MS)
                        : POLL_RETRY_MIN_MS;
                    // If the link itself dropped: a cheap same-AP reconnect first
                    // (covers a brief blip / router reboot), then after a few
                    // failures assume we've physically moved and rescan all saved
                    // networks to roam (office <-> home) -- no reboot needed. The
                    // backoff above paces the rescan so a sustained off-network
                    // stretch (e.g. a commute with the screen on) doesn't scan
                    // every cycle. The screen-off case never reaches here: the
                    // loop breaks before polling while asleep, so a battery commute
                    // stays fully idle.
                    if (!net::isConnected()) {
                        if (++gDropStreak < CUM_REROAM_AFTER) net::reconnect();
                        else { roamReconnect(); gDropStreak = 0; }
                    }
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

            // Blink the top-bar clock's colon at 2Hz. This is a cheap partial
            // redraw of just the clock band, and recomputing the time each tick
            // also keeps the minute current (polls are only 60s apart). Skipped
            // while a count-up or refresh animation owns the screen.
            static uint32_t lastColon = 0;
            static bool     colonOn   = true;
            bool clockLive = !gAnimating && !(gPollRunning && gPollAnimate)
                             && gPage == PAGE_DASH && gLastUsage.ok && gClockValid;
            if (clockLive && millis() - lastColon >= 500) {
                lastColon = millis();
                colonOn = !colonOn;
                display::drawClockColon(fmtClock(time(nullptr)), colonOn);
            }

            // Wake refresh: wakeShow() kicked a non-blocking reconnect and armed
            // this flag. Poll the moment the link is up rather than waiting out the
            // 2 s backoff floor, so the numbers/Wi-Fi bars fill in as soon as they
            // can. The backoff still fires the fallback poll (-> roam) if the link
            // never associates here.
            if (gWakePollPending && net::isConnected() && !gPollRunning) {
                gWakePollPending = false;
                gPollBackoffMs = 0;
                requestPoll(false);
                gLastPoll = millis();
            }

            // Periodic poll only. (Manual refresh is the menu's "Refresh" item now;
            // the Home button drives the menu, so it must NOT also poll here -- that
            // made selecting Exit / a page double as a refresh.)
            uint32_t pollEvery = gPollBackoffMs ? gPollBackoffMs : CUM_POLL_INTERVAL_MS;
            if (!gPollRunning && millis() - gLastPoll >= pollEvery) {
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
                if (gBootLoading) {                  // first boot: continue the spinner
                    display::drawBootBusy(gBootFrame++);
                    delay(20);
                } else {
                    display::drawRefreshAnim(gAnimFrame++);
                    delay(120);                      // slower bob
                }
            } else {
                delay(16);   // idle ~60Hz: poll the Home button often so short taps register
            }
            break;
        }
    }
}
