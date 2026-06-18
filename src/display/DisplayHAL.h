// ============================================================================
//  DisplayHAL.h  -  thin drawing layer on top of the LGFX device
//
//  Everything UI-facing goes through this namespace so the rest of the
//  firmware never talks to LovyanGFX directly. When LVGL is added later it can
//  grab the raw device via display::gfx() for its flush callback, while custom
//  drawing keeps using these helpers.
//
//  Orientation: landscape, 480 (W) x 222 (H).
// ============================================================================
#pragma once

#include <Arduino.h>

#include "LGFX_TDisplayS3Pro.hpp"

namespace display {

// Initialise the panel + backlight. Returns false if the panel init fails.
bool begin(uint8_t brightness = 200);

// Raw LovyanGFX device (for future LVGL flush or advanced drawing).
LGFX_TDisplayS3Pro& gfx();

// Set backlight brightness (0-255).
void setBrightness(uint8_t value);

// Stage-1 self-test: title text + a colored utilization bar.
void drawTestScreen();

// --- Stage 2 screens --------------------------------------------------------
// Setup mode: tell the user which AP to join and where to browse.
void drawProvisioning(const String& apSsid, const String& apPass,
                      const String& portalIp);

// Unlock mode: prompt for the PIN via the given LAN URL.
void drawUnlock(const String& portalUrl, int failsRemaining);

// Connected/running status line.
void drawStatus(const String& ssid, const String& ip, int rssi);

// Detail/info page (toggled with IO12).
struct Detail {
    String ssid, ip;
    int    rssi;
    int    battery;
    bool   charging;
    String battEst;             // rough time-left on battery ("" while charging/untracked)
    String reset5h, reset7d;
    String uptime;
};
void drawDetail(const Detail& d);

// Usage history mini-graph (two sparklines: 5h orange, 7d lime). Arrays hold
// `count` samples oldest..newest, each 0..100. Below the graph, a stats footer
// shows now/max per series plus the limit projection: an ETA to 100% if it's
// reached before the window resets (etaMin whole minutes, else -1), otherwise
// the projected utilization at reset (peakPct 0..100+, -1 if unknown).
void drawHistory(const float* h5, const float* h7, int count,
                 int eta5min = -1, int eta7min = -1,
                 int peak5 = -1, int peak7 = -1);

// Battery page (toggled with IO12, after History). Big % + time-left header and
// a discharge graph: a solid line of the recorded battery % since the last
// unplug, plus a dotted line projecting the current drain rate down to 0%.
//   histMin[i] = whole minutes since unplug, histPct[i] = battery % (0..100),
//   oldest..newest, `histCount` entries.
//   tracking  = on battery and recording (false on USB).
//   measuring = tracking but not enough drop/time yet for a trustworthy rate.
//   ratePerHr / etaMin = drain rate and minutes-to-0% (etaMin -1 if unknown).
struct BatteryPage {
    int    pct       = 100;
    bool   charging  = true;
    bool   tracking  = false;
    bool   measuring = false;
    float  ratePerHr = 0.0f;
    int    etaMin    = -1;
    const uint16_t* histMin = nullptr;
    const uint8_t*  histPct = nullptr;
    int    histCount = 0;
};
void drawBatteryPage(const BatteryPage& b);

// --- Stage 4 dashboard ------------------------------------------------------
struct Dashboard {
    float  current      = 0;    // 5h utilization, percent 0..100
    String currentReset;        // pre-formatted, e.g. "1h 22m"
    float  weekly       = 0;    // 7d utilization, percent 0..100
    String weeklyReset;
    int    rssi         = 0;    // WiFi RSSI dBm (signal bars)
    int    battery      = 100;  // percent 0..100 (placeholder until PMU)
    bool   charging     = true; // on USB / charging
    String status;              // playful footer line
    String clock;               // top-bar wall clock, e.g. "PM 2:30" ("--:--" until NTP)
    bool   stale        = false; // last poll failed; showing previous data
    float  curPop       = 0;    // landing-pop intensity 0..1 for the Current card
    float  wkPop        = 0;    // landing-pop intensity 0..1 for the Weekly card
};

// Full themed dashboard: mascot, title, WiFi + battery, two colored bars with
// pill labels and reset countdowns, and a status line.
void drawDashboard(const Dashboard& d);

// Count-up frame: redraw + push only the cards' dynamic content (numbers, bars,
// sparks) for a fast partial update. Needs a prior full drawDashboard() to have
// painted the static chrome. curPop/wkPop drive the spark burst; curGlow/wkGlow
// drive the white-tinted bar/number flash (decoupled so the glow can lag).
void drawDashboardBands(float curPct, float wkPct, float curPop, float wkPop,
                        float curGlow, float wkGlow);

// Redraw ONLY the top-bar clock and push just its band (cheap 2Hz colon blink).
// `colonOn` toggles the ':' without shifting the digits. Needs a prior full
// drawDashboard() to have painted the rest of the chrome.
void drawClockColon(const String& clock, bool colonOn);

// One frame of the "refreshing" animation (bouncing logo + dots). Call with
// an incrementing frame counter while a poll is in flight.
void drawRefreshAnim(int frame);

// The intro splash slides down to this Y offset and rests there (a touch above
// the vertical center, to balance the loading bar below). Reuse it so a static
// splash matches the animation's final frame.
constexpr int SPLASH_REST_Y = 10;

// Boot splash: Claude Code logo + wordmark, shifted down by `yoff` (for the
// slide-in animation; pass SPLASH_REST_Y for the settled position).
void drawSplash(int yoff = 0);

// Splash with a "keep holding to reset" progress bar (frac 0..1). Shown at boot
// only while IO16 is actually held, so a normal boot just shows the splash.
void drawResetHold(float frac);

// Animated "booting" splash: the logo/wordmark sit static at the rest position
// and an indeterminate ring spinner rotates below. `frame` increments per frame.
void drawBootBusy(int frame);

// Shown when a poll fails so a photo of the screen reveals the HTTP code.
void drawApiError(int httpCode, const String& note);

// Generic title + one-line message (boot hints, etc.).
void drawMessage(const String& title, const String& line);

// --- Touch PIN keypad (Stage 4) ---------------------------------------------
// Draw the on-screen numeric keypad. `enteredLen` fills that many PIN dots;
// `note` is an optional status line (e.g. "Wrong PIN - 9 left").
void drawKeypad(int enteredLen, const String& note);

// Map a touch point to a key: '0'..'9', 'C' (clear), '<' (backspace), or 0.
char keypadHit(int x, int y);

// Diagnostic: show the last touch's raw + mapped coordinates.
void drawTouchTest(bool touching, int rawX, int rawY, int mapX, int mapY);

}  // namespace display
