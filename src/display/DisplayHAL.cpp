// ============================================================================
//  DisplayHAL.cpp  -  implementation of the display drawing layer
//
//  All drawing happens on an off-screen sprite (canvas) in PSRAM and is blitted
//  to the panel in one pushSprite() per frame. This double-buffering removes the
//  clear-then-redraw flicker/tearing of drawing straight to the SPI panel.
// ============================================================================
#include "DisplayHAL.h"

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
constexpr uint32_t T_TRACK  = 0x332E44;  // bar track (purple)
constexpr uint32_t T_CORAL  = 0xE8654F;  // mascot + status
constexpr uint32_t T_CUR    = 0xED7B3A;  // current bar (orange)
constexpr uint32_t T_WK     = 0xC2D74A;  // weekly bar (lime)
constexpr uint32_t T_PILLBG = 0x463A5E;  // pill background
constexpr uint32_t T_PILLTX = 0xE9E2F5;  // pill text
constexpr uint32_t T_GREEN  = 0x7BC86B;  // battery ok

// 11x8 pixel-art mascot (Space-Invader-ish), MSB = leftmost column.
const uint16_t MASCOT[8] = {
    0x104, 0x088, 0x1FC, 0x376, 0x7FF, 0x5FD, 0x505, 0x0D8,
};

void drawMascot(int x, int y, int s) {
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 11; c++)
            if ((MASCOT[r] >> (10 - c)) & 1)
                canvas.fillRect(x + c * s, y + r * s, s, s, rgb(T_CORAL));
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
    const int w = 26, h = 13;
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    canvas.drawRoundRect(x, y, w, h, 2, rgb(T_TITLE));
    canvas.fillRect(x + w, y + 4, 2, h - 8, rgb(T_TITLE));   // nub
    uint32_t fill = (pct <= 15 && !charging) ? T_CORAL : T_GREEN;
    int fw = (w - 4) * pct / 100;
    if (fw > 0) canvas.fillRect(x + 2, y + 2, fw, h - 4, rgb(fill));
    if (charging) {                                          // little bolt
        canvas.setFont(&fonts::Font0);
        canvas.setTextDatum(textdatum_t::middle_center);
        canvas.setTextColor(rgb(T_BG));
        canvas.drawString("+", x + w / 2, y + h / 2);
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

void drawMetricCard(int yc, const char* label, float pct, const String& reset,
                    uint32_t barColor) {
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    const int cx = 12, cw = canvas.width() - 24, ch = 80;
    canvas.fillRoundRect(cx, yc, cw, ch, 10, rgb(T_CARD));

    // Big percentage.
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", (int)(pct + 0.5f));
    canvas.setFont(&fonts::FreeSansBold18pt7b);
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextColor(rgb(T_TITLE));
    canvas.drawString(buf, cx + 18, yc + 8);

    drawPill(label, cx + cw - 16, yc + 10);

    // Bar.
    const int bx = cx + 18, bw = cw - 36, by = yc + 44, bh = 14, r = 7;
    canvas.fillRoundRect(bx, by, bw, bh, r, rgb(T_TRACK));
    int fw = (int)(bw * pct / 100.0f);
    if (fw > 0) canvas.fillRoundRect(bx, by, fw < bh ? bh : fw, bh, r, rgb(barColor));

    // Reset countdown.
    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextColor(rgb(T_MUTED));
    canvas.drawString("Resets in " + reset, bx, yc + 60);
}
}  // namespace

void drawDashboard(const Dashboard& d) {
    canvas.fillScreen(rgb(T_BG));

    // ---- Top bar ----------------------------------------------------------
    drawMascot(14, 8, 2);                       // 22x16
    canvas.setFont(&fonts::FreeSansBold12pt7b);
    canvas.setTextDatum(textdatum_t::top_center);
    canvas.setTextColor(rgb(T_TITLE));
    canvas.drawString("Usage", canvas.width() / 2, 8);

    drawBattery(canvas.width() - 42, 9, d.battery, d.charging);
    drawWifiBars(canvas.width() - 74, 9, d.rssi);

    // ---- Cards ------------------------------------------------------------
    drawMetricCard(36,  "Current", d.current, d.currentReset, T_CUR);
    drawMetricCard(120, "Weekly",  d.weekly,  d.weeklyReset,  T_WK);

    // ---- Status line ------------------------------------------------------
    canvas.setFont(&fonts::FreeSansBold9pt7b);
    canvas.setTextDatum(textdatum_t::top_center);
    canvas.setTextColor(rgb(T_CORAL));
    canvas.drawString("* " + d.status, canvas.width() / 2, 202);
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

    for (int i = 0; i < 4; i++) {
        int cx = 248 + i * 24, cy = 22;
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
    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextColor(rgb(COL_LABEL));
    canvas.drawString(line, 16, 80);
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
