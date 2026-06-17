// ============================================================================
//  DisplayHAL.cpp  -  implementation of the display drawing layer
//
//  All drawing happens on an off-screen sprite (canvas) in PSRAM and is blitted
//  to the panel in one pushSprite() per frame. This double-buffering removes the
//  clear-then-redraw flicker/tearing of drawing straight to the SPI panel.
// ============================================================================
#include "DisplayHAL.h"

#include "claudecode_bolt.h"
#include "claudecode_logo.h"
#include "claudecode_wordmark.h"

namespace {
LGFX_TDisplayS3Pro lcd;
lgfx::LGFX_Sprite  canvas(&lcd);   // off-screen frame buffer (PSRAM)
bool canvasReady = false;

// Anthropic-ish accent / palette (RGB888).
constexpr uint32_t COL_BG      = 0x0E1016;  // near-black background
constexpr uint32_t COL_TITLE   = 0xECECEC;  // off-white
constexpr uint32_t COL_SUB     = 0x8A9099;  // muted gray
constexpr uint32_t COL_LABEL   = 0xC8CCD0;
constexpr uint32_t COL_TRACK   = 0x242830;  // bar track
constexpr uint32_t COL_ACCENT  = 0xD97757;  // Anthropic coral
constexpr uint32_t COL_BORDER  = 0x3A404A;
constexpr uint32_t COL_FOOTER  = 0x6A7079;

// Expand a 0xRRGGBB literal into a 24-bit color (draw calls convert it to the
// target depth). Masking each channel keeps it tidy.
inline uint32_t rgb(uint32_t hex) {
    return lcd.color888((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF);
}

// Blit the finished frame to the panel.
void present() {
    if (canvasReady) canvas.pushSprite(0, 0);
}
}  // namespace

namespace display {

LGFX_TDisplayS3Pro& gfx() { return lcd; }

void setBrightness(uint8_t value) { lcd.setBrightness(value); }

bool begin(uint8_t brightness) {
    if (!lcd.init()) {
        return false;
    }
    lcd.setRotation(1);             // landscape -> 480 (W) x 222 (H)
    lcd.setBrightness(brightness);
    lcd.fillScreen(rgb(COL_BG));

    canvas.setPsram(true);
    canvas.setColorDepth(16);
    canvasReady = (canvas.createSprite(lcd.width(), lcd.height()) != nullptr);
    if (canvasReady) {
        canvas.fillScreen(rgb(COL_BG));
        present();
    }
    return true;
}

void drawTestScreen() {
    const int W = canvas.width();   // 480
    const int H = canvas.height();  // 222

    canvas.fillScreen(rgb(COL_BG));

    // ---- Title ------------------------------------------------------------
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextColor(rgb(COL_TITLE));
    canvas.setFont(&fonts::FreeSansBold12pt7b);
    canvas.drawString("Claude Usage Monitor", 16, 16);

    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextColor(rgb(COL_SUB));
    canvas.drawString("ST7796 / LovyanGFX  -  480 x 222", 16, 48);

    // ---- Self-test label --------------------------------------------------
    canvas.setTextColor(rgb(COL_LABEL));
    canvas.drawString("Display self-test", 16, 92);

    // ---- Colored utilization bar -----------------------------------------
    const int   bx = 16, by = 120, bw = W - 32, bh = 36, r = 9;
    const float pct = 0.65f;        // placeholder fill

    canvas.fillRoundRect(bx, by, bw, bh, r, rgb(COL_TRACK));
    canvas.fillRoundRect(bx, by, (int)(bw * pct), bh, r, rgb(COL_ACCENT));
    canvas.drawRoundRect(bx, by, bw, bh, r, rgb(COL_BORDER));

    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.setTextColor(TFT_WHITE);
    canvas.setFont(&fonts::FreeSansBold9pt7b);
    canvas.drawString("65%", bx + bw / 2, by + bh / 2);

    // ---- Footer -----------------------------------------------------------
    canvas.setTextDatum(textdatum_t::bottom_left);
    canvas.setFont(&fonts::Font0);
    canvas.setTextColor(rgb(COL_FOOTER));
    canvas.drawString("Stage 1 - display OK", 16, H - 10);
    present();
}

namespace {
// Shared header used by the simple status screens.
void drawHeader(const char* title) {
    canvas.fillScreen(rgb(COL_BG));
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextColor(rgb(COL_ACCENT));
    canvas.setFont(&fonts::FreeSansBold12pt7b);
    canvas.drawString(title, 16, 16);
}

// "label: value" row in the muted/label palette.
void drawRow(const char* label, const String& value, int y) {
    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextColor(rgb(COL_SUB));
    canvas.drawString(label, 16, y);
    canvas.setTextColor(rgb(COL_TITLE));
    canvas.drawString(value, 150, y);
}
}  // namespace

void drawProvisioning(const String& apSsid, const String& apPass,
                      const String& portalIp) {
    drawHeader("Setup mode");

    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextColor(rgb(COL_LABEL));
    canvas.drawString("Join this WiFi, then open the page:", 16, 56);

    drawRow("WiFi:", apSsid, 88);
    drawRow("Pass:", apPass, 116);
    drawRow("Open:", "http://" + portalIp, 144);

    canvas.setTextDatum(textdatum_t::bottom_left);
    canvas.setFont(&fonts::Font0);
    canvas.setTextColor(rgb(COL_FOOTER));
    canvas.drawString("Stage 2 - awaiting setup", 16, canvas.height() - 10);
    present();
}

void drawUnlock(const String& portalUrl, int failsRemaining) {
    drawHeader("Locked");

    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextColor(rgb(COL_LABEL));
    canvas.drawString("Open this page and enter your PIN:", 16, 56);

    drawRow("Open:", portalUrl, 92);

    canvas.setTextColor(failsRemaining <= 3 ? rgb(COL_ACCENT) : rgb(COL_SUB));
    canvas.drawString(String(failsRemaining) + " attempt(s) before wipe", 16, 124);

    canvas.setTextDatum(textdatum_t::bottom_left);
    canvas.setFont(&fonts::Font0);
    canvas.setTextColor(rgb(COL_FOOTER));
    canvas.drawString("Stage 2 - awaiting PIN", 16, canvas.height() - 10);
    present();
}

void drawStatus(const String& ssid, const String& ip, int rssi) {
    drawHeader("Claude Usage Monitor");

    drawRow("WiFi:", ssid, 64);
    drawRow("IP:", ip, 92);
    drawRow("Signal:", String(rssi) + " dBm", 120);

    canvas.setTextDatum(textdatum_t::bottom_left);
    canvas.setFont(&fonts::Font0);
    canvas.setTextColor(rgb(COL_FOOTER));
    canvas.drawString("Stage 2 - connected & unlocked", 16, canvas.height() - 10);
    present();
}

namespace {
// ---- Stage 4 themed dashboard palette -------------------------------------
constexpr uint32_t T_BG     = 0x0D0D12;  // near-black
constexpr uint32_t T_CARD   = 0x1B1B24;  // card background
constexpr uint32_t T_TITLE  = 0xF2F2F5;  // bright text
constexpr uint32_t T_MUTED  = 0x9B90B0;  // muted lavender ("resets in")
constexpr uint32_t T_TRACK  = 0x423C58;  // bar track (purple) - visible vs card
constexpr uint32_t T_CORAL  = 0xE8654F;  // mascot + status
constexpr uint32_t T_CUR    = 0xED7B3A;  // current bar (orange)
constexpr uint32_t T_WK     = 0xC2D74A;  // weekly bar (lime)
constexpr uint32_t T_PILLBG = 0x463A5E;  // pill background
constexpr uint32_t T_PILLTX = 0xE9E2F5;  // pill text
constexpr uint32_t T_GREEN  = 0x7BC86B;  // battery ok

// Draw a 1-bit bitmap (row-major, MSB first) at integer scale `s` in `color`.
void drawBits(const uint8_t* d, int w, int h, int x, int y, int s, uint32_t color) {
    const int stride = (w + 7) / 8;
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++)
            if (d[r * stride + (c >> 3)] & (0x80 >> (c & 7)))
                canvas.fillRect(x + c * s, y + r * s, s, s, rgb(color));
}

// Official Claude Code logo, drawn coral at integer scale `s`.
void drawLogo(int x, int y, int s) {
    drawBits(CC_LOGO, CC_LOGO_W, CC_LOGO_H, x, y, s, T_CORAL);
}

void drawWifiBars(int x, int y, int rssi) {
    int bars = (rssi == 0)     ? 0
             : (rssi >= -55)   ? 4
             : (rssi >= -65)   ? 3
             : (rssi >= -75)   ? 2
             : (rssi >= -85)   ? 1
                               : 0;
    for (int i = 0; i < 4; i++) {
        int h = 4 + i * 3;
        canvas.fillRect(x + i * 6, y + 13 - h, 4, h, rgb(i < bars ? T_TITLE : T_TRACK));
    }
}

void drawBattery(int x, int y, int pct, bool charging) {
    const int w = 30, h = 16;
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    canvas.drawRoundRect(x, y, w, h, 3, rgb(T_TITLE));
    canvas.fillRect(x + w, y + (h - 6) / 2, 2, 6, rgb(T_TITLE));   // nub
    uint32_t fill = (pct <= 15 && !charging) ? T_CORAL : T_GREEN;
    int fw = (w - 4) * pct / 100;
    if (fw > 0) canvas.fillRect(x + 2, y + 2, fw, h - 4, rgb(fill));
    if (charging) {                                               // lightning bolt
        drawBits(CC_BOLT, CC_BOLT_W, CC_BOLT_H,
                 x + (w - CC_BOLT_W) / 2, y + (h - CC_BOLT_H) / 2, 1, T_BG);
    }
}

// Rounded "pill" badge, right-aligned to rightX. Returns its left edge.
int drawPill(const char* text, int rightX, int y) {
    canvas.setFont(&fonts::FreeSansBold9pt7b);
    int tw = canvas.textWidth(text);
    int pw = tw + 22, ph = 24;
    int px = rightX - pw;
    canvas.fillRoundRect(px, y, pw, ph, ph / 2, rgb(T_PILLBG));
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.setTextColor(rgb(T_PILLTX));
    canvas.drawString(text, px + pw / 2, y + ph / 2 + 1);
    return px;
}

// Blend two 0xRRGGBB colors; t=0 -> a, t=1 -> b.
uint32_t lerpColor(uint32_t a, uint32_t b, float t) {
    if (t < 0) t = 0; if (t > 1) t = 1;
    int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    int r = ar + (int)((br - ar) * t);
    int g = ag + (int)((bg - ag) * t);
    int bl = ab + (int)((bb - ab) * t);
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

// A small "pop" of sparks flying out from (x,y). `pop` runs 1 -> 0 as the
// effect fades: dots start tight + bright at impact, then drift out and shrink.
// Color is a pastel tint of the card color (not harsh white): pastel at impact,
// settling back toward the bar color as it fades.
void drawSparks(int x, int y, float pop, uint32_t base, uint32_t pastel) {
    static const int8_t dx[] = { 5, 4, 0, -4, -5, 3 };
    static const int8_t dy[] = { 0, -4, -5, -4, 0, 4 };
    float out = (1.0f - pop) * 15.0f + 2.0f;     // distance grows as it fades
    int   rad = (int)(3.0f * pop) + 1;           // radius shrinks as it fades
    uint32_t c = lerpColor(base, pastel, pop);
    for (int i = 0; i < 6; i++) {
        int px = x + (int)(dx[i] * out / 5.0f);
        int py = y + (int)(dy[i] * out / 5.0f);
        canvas.fillCircle(px, py, rad, rgb(c));
    }
}

void drawMetricCard(int yc, const char* label, float pct, const String& reset,
                    uint32_t barColor, float pop = 0.0f) {
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    const int cx = 12, cw = canvas.width() - 24, ch = 82;
    canvas.fillRoundRect(cx, yc, cw, ch, 10, rgb(T_CARD));

    // Landing pop uses a pastel tint of THIS card's color (orange / lime mixed
    // with white) instead of harsh pure white.
    uint32_t pastel = lerpColor(barColor, 0xFFFFFF, 0.6f);

    // Big percentage — glows toward the card's pastel tone as it lands.
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", (int)(pct + 0.5f));
    canvas.setFont(&fonts::FreeSansBold18pt7b);
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextColor(rgb(pop > 0 ? lerpColor(T_TITLE, pastel, pop) : T_TITLE));
    canvas.drawString(buf, cx + 18, yc + 6);

    drawPill(label, cx + cw - 16, yc + 8);

    // Bar.
    const int bx = cx + 18, bw = cw - 36, by = yc + 36, bh = 17, r = 8;
    canvas.fillRoundRect(bx, by, bw, bh, r, rgb(T_TRACK));
    int fw = (int)(bw * pct / 100.0f);
    uint32_t fill = (pop > 0) ? lerpColor(barColor, pastel, pop) : barColor;
    int filled = fw < bh ? bh : fw;
    if (fw > 0) canvas.fillRoundRect(bx, by, filled, bh, r, rgb(fill));

    // Little spark burst at the bar's leading edge as it lands.
    if (pop > 0.0f) {
        int sx = bx + filled; if (sx > bx + bw) sx = bx + bw;
        drawSparks(sx, by + bh / 2, pop, barColor, pastel);
    }

    // Reset countdown — larger and brighter for readability.
    canvas.setFont(&fonts::FreeSans12pt7b);
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextColor(rgb(0xC8BEDC));
    canvas.drawString("Resets in " + reset, bx, yc + 58);
}
}  // namespace

void drawDashboard(const Dashboard& d) {
    canvas.fillScreen(rgb(T_BG));

    // ---- Top bar ----------------------------------------------------------
    drawLogo(10, 3, 1);                         // 30x30 official logo
    canvas.setFont(&fonts::FreeSansBold12pt7b);
    canvas.setTextDatum(textdatum_t::top_center);
    canvas.setTextColor(rgb(T_TITLE));
    canvas.drawString("Usage", canvas.width() / 2, 8);

    drawBattery(canvas.width() - 42, 9, d.battery, d.charging);
    drawWifiBars(canvas.width() - 74, 9, d.rssi);
    if (d.stale)                                   // last poll failed
        canvas.fillCircle(canvas.width() - 90, 16, 4, rgb(T_CUR));

    // ---- Cards ------------------------------------------------------------
    drawMetricCard(34,  "Current", d.current, d.currentReset, T_CUR, d.curPop);
    drawMetricCard(118, "Weekly",  d.weekly,  d.weeklyReset,  T_WK,  d.wkPop);

    // ---- Status line ------------------------------------------------------
    canvas.setFont(&fonts::FreeSansBold9pt7b);
    canvas.setTextDatum(textdatum_t::top_center);
    canvas.setTextColor(rgb(T_CORAL));
    canvas.drawString("* " + d.status, canvas.width() / 2, 204);
    present();
}

void drawDetail(const Detail& d) {
    drawHeader("Details");
    char buf[40];

    drawRow("WiFi:",   d.ssid, 54);
    drawRow("IP:",     d.ip, 78);
    snprintf(buf, sizeof(buf), "%d dBm", d.rssi);
    drawRow("Signal:", buf, 102);
    snprintf(buf, sizeof(buf), "%d%%%s", d.battery, d.charging ? " (chg)" : "");
    drawRow("Battery:", buf, 126);
    drawRow("5h reset:", d.reset5h, 150);
    drawRow("7d reset:", d.reset7d, 174);
    drawRow("Uptime:",   d.uptime, 198);

    present();
}

namespace {
int plotY(float v, int y0, int h) {
    if (v < 0) v = 0; if (v > 100) v = 100;
    return y0 + (int)(h * (100 - v) / 100);
}
}  // namespace

void drawHistory(const float* h5, const float* h7, int count) {
    drawHeader("History");

    // Legend (top-right).
    canvas.setFont(&fonts::FreeSansBold9pt7b);
    canvas.setTextDatum(textdatum_t::top_right);
    canvas.setTextColor(rgb(T_CUR));
    canvas.drawString("5h", canvas.width() - 64, 18);
    canvas.setTextColor(rgb(T_WK));
    canvas.drawString("7d", canvas.width() - 22, 18);

    const int x0 = 32, y0 = 58, w = canvas.width() - 52, h = 138;

    // Gridlines + % labels at 0 / 50 / 100.
    canvas.setFont(&fonts::Font0);
    canvas.setTextDatum(textdatum_t::middle_right);
    for (int p = 0; p <= 100; p += 50) {
        int gy = y0 + h * (100 - p) / 100;
        canvas.drawFastHLine(x0, gy, w, rgb(T_TRACK));
        canvas.setTextColor(rgb(T_MUTED));
        canvas.drawString(String(p), x0 - 5, gy);
    }

    if (count < 2) {
        canvas.setFont(&fonts::FreeSans9pt7b);
        canvas.setTextDatum(textdatum_t::middle_center);
        canvas.setTextColor(rgb(T_MUTED));
        canvas.drawString("collecting data...", canvas.width() / 2, y0 + h / 2);
        present();
        return;
    }

    // Two sparklines (drawn 2px thick by doubling).
    for (int i = 1; i < count; i++) {
        int xa = x0 + (long)(i - 1) * w / (count - 1);
        int xb = x0 + (long)i * w / (count - 1);
        int a5 = plotY(h5[i - 1], y0, h), b5 = plotY(h5[i], y0, h);
        int a7 = plotY(h7[i - 1], y0, h), b7 = plotY(h7[i], y0, h);
        canvas.drawLine(xa, a7, xb, b7, rgb(T_WK));
        canvas.drawLine(xa, a7 + 1, xb, b7 + 1, rgb(T_WK));
        canvas.drawLine(xa, a5, xb, b5, rgb(T_CUR));
        canvas.drawLine(xa, a5 + 1, xb, b5 + 1, rgb(T_CUR));
    }
    present();
}

void drawSplash(int yoff) {
    canvas.fillScreen(rgb(T_BG));
    drawBits(CC_LOGO_L, CC_LOGO_L_W, CC_LOGO_L_H,           // 90px native logo
             canvas.width() / 2 - CC_LOGO_L_W / 2, 8 + yoff, 1, T_CORAL);
    // Wordmark is an anti-aliased RGB565 image (blended over T_BG) for smooth
    // edges; 1-bit text looked jagged.
    canvas.pushImage(canvas.width() / 2 - CC_TEXT_W / 2, 110 + yoff,
                     CC_TEXT_W, CC_TEXT_H, (const lgfx::rgb565_t*)CC_TEXT);
    present();
}

void drawRefreshAnim(int frame) {
    canvas.fillScreen(rgb(T_BG));

    // Logo bobs up and down on a short cycle.
    static const int kBob[] = {0, 4, 8, 10, 8, 4};
    int oy = kBob[frame % 6];
    int mw = CC_LOGO_M_W;                  // 60px native logo
    drawBits(CC_LOGO_M, CC_LOGO_M_W, CC_LOGO_M_H,
             canvas.width() / 2 - mw / 2, 28 + oy, 1, T_CORAL);

    // Shadow that shrinks as it rises, for a little life.
    int sw = 48 - oy;
    canvas.fillRoundRect(canvas.width() / 2 - sw / 2, 104, sw, 6, 3, rgb(T_TRACK));

    // Static "Refreshing..." — the bobbing logo conveys activity, so the text
    // stays still (no cycling dots).
    canvas.setFont(&fonts::FreeSansBold12pt7b);
    canvas.setTextDatum(textdatum_t::top_center);
    canvas.setTextColor(rgb(T_CORAL));
    canvas.drawString("Refreshing...", canvas.width() / 2, 140);
    present();
}

namespace {
// Centered keypad: 3 cols x 4 rows of compact cells (not stretched across the
// full width). Bottom row is Clear / 0 / Backspace.
constexpr int KP_COLS = 3;
constexpr int KP_ROWS = 4;
constexpr int KP_CW   = 96;                       // cell width
constexpr int KP_CH   = 42;                       // cell height
constexpr int KP_X0   = (480 - KP_COLS * KP_CW) / 2;   // centered -> 96
constexpr int KP_Y0   = 44;
const char KP_GRID[KP_ROWS][KP_COLS] = {
    {'1', '2', '3'},
    {'4', '5', '6'},
    {'7', '8', '9'},
    {'C', '0', '<'},
};
}  // namespace

char keypadHit(int x, int y) {
    int col = (x - KP_X0) / KP_CW;
    int row = (y - KP_Y0) / KP_CH;
    if (x < KP_X0 || y < KP_Y0 || col < 0 || col >= KP_COLS ||
        row < 0 || row >= KP_ROWS)
        return 0;
    return KP_GRID[row][col];
}

void drawKeypad(int enteredLen, const String& note) {
    canvas.fillScreen(rgb(T_BG));

    // Header: title + centered PIN dots + optional note.
    canvas.setFont(&fonts::FreeSansBold12pt7b);
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextColor(rgb(T_TITLE));
    canvas.drawString("Enter PIN", 14, 12);

    // 4 dots centered on the screen (group midpoint at width/2).
    const int dotGap = 24;
    const int dotX0  = canvas.width() / 2 - (3 * dotGap) / 2;   // -> 204
    for (int i = 0; i < 4; i++) {
        int cx = dotX0 + i * dotGap, cy = 22;
        if (i < enteredLen) canvas.fillCircle(cx, cy, 7, rgb(T_CORAL));
        else                canvas.drawCircle(cx, cy, 7, rgb(T_TRACK));
    }
    if (note.length()) {
        canvas.setFont(&fonts::FreeSans9pt7b);
        canvas.setTextDatum(textdatum_t::top_right);
        canvas.setTextColor(rgb(T_CORAL));
        canvas.drawString(note, canvas.width() - 12, 16);
    }

    // Keys (centered cluster).
    for (int row = 0; row < KP_ROWS; row++) {
        for (int col = 0; col < KP_COLS; col++) {
            int kx = KP_X0 + col * KP_CW, ky = KP_Y0 + row * KP_CH;
            canvas.fillRoundRect(kx + 4, ky + 4, KP_CW - 8, KP_CH - 8, 8, rgb(T_CARD));
            char k = KP_GRID[row][col];
            const char* label = (k == 'C') ? "CLR" : (k == '<') ? "DEL" : nullptr;
            char one[2] = {k, 0};
            canvas.setTextDatum(textdatum_t::middle_center);
            if (label) {
                canvas.setFont(&fonts::FreeSansBold9pt7b);
                canvas.setTextColor(rgb(T_CORAL));
                canvas.drawString(label, kx + KP_CW / 2, ky + KP_CH / 2);
            } else {
                canvas.setFont(&fonts::FreeSansBold18pt7b);
                canvas.setTextColor(rgb(T_TITLE));
                canvas.drawString(one, kx + KP_CW / 2, ky + KP_CH / 2);
            }
        }
    }
    present();
}

void drawMessage(const String& title, const String& line) {
    drawHeader(title.c_str());
    canvas.setFont(&fonts::FreeSans12pt7b);
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextColor(rgb(COL_LABEL));
    canvas.drawString(line, 16, 78);
    present();
}

void drawTouchTest(bool touching, int rawX, int rawY, int mapX, int mapY) {
    canvas.fillScreen(rgb(T_BG));
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextColor(rgb(T_CORAL));
    canvas.setFont(&fonts::FreeSansBold12pt7b);
    canvas.drawString("Touch test", 14, 10);

    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextColor(rgb(T_MUTED));
    canvas.drawString("Press the round button; read the values.", 14, 40);

    canvas.setFont(&fonts::FreeSansBold18pt7b);
    canvas.setTextColor(rgb(T_TITLE));
    if (rawX < 0) {
        canvas.drawString("(touch to begin)", 14, 90);
    } else {
        char a[40], b[40];
        snprintf(a, sizeof(a), "raw: %d , %d", rawX, rawY);
        snprintf(b, sizeof(b), "map: %d , %d", mapX, mapY);
        canvas.drawString(a, 14, 80);
        canvas.drawString(b, 14, 120);
    }

    // Live dot at the mapped position while touching.
    if (touching && mapX >= 0)
        canvas.fillCircle(mapX, mapY, 6, rgb(T_CORAL));

    canvas.setTextDatum(textdatum_t::bottom_right);
    canvas.setFont(&fonts::Font0);
    canvas.setTextColor(rgb(touching ? T_GREEN : T_MUTED));
    canvas.drawString(touching ? "TOUCHING" : "released",
                      canvas.width() - 10, canvas.height() - 8);
    present();
}

void drawApiError(int httpCode, const String& note) {
    drawHeader("API error");

    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextColor(rgb(COL_TITLE));
    canvas.drawString("HTTP " + String(httpCode), 16, 70);

    canvas.setTextColor(rgb(COL_SUB));
    canvas.drawString(note, 16, 104);

    canvas.setTextDatum(textdatum_t::bottom_left);
    canvas.setFont(&fonts::Font0);
    canvas.setTextColor(rgb(COL_FOOTER));
    canvas.drawString("Stage 3 - poll failed", 16, canvas.height() - 10);
    present();
}

}  // namespace display
