// ============================================================================
//  DisplayHAL.cpp  -  implementation of the display drawing layer
// ============================================================================
#include "DisplayHAL.h"

namespace {
LGFX_TDisplayS3Pro lcd;

// Anthropic-ish accent / palette (RGB888).
constexpr uint32_t COL_BG      = 0x0E1016;  // near-black background
constexpr uint32_t COL_TITLE   = 0xECECEC;  // off-white
constexpr uint32_t COL_SUB     = 0x8A9099;  // muted gray
constexpr uint32_t COL_LABEL   = 0xC8CCD0;
constexpr uint32_t COL_TRACK   = 0x242830;  // bar track
constexpr uint32_t COL_ACCENT  = 0xD97757;  // Anthropic coral
constexpr uint32_t COL_BORDER  = 0x3A404A;
constexpr uint32_t COL_FOOTER  = 0x6A7079;

// Expand a 0xRRGGBB literal into the panel's native color. Masking each
// channel keeps color888() from emitting -Woverflow on the truncation.
inline uint32_t rgb(uint32_t hex) {
    return lcd.color888((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF);
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
    return true;
}

void drawTestScreen() {
    const int W = lcd.width();      // 480
    const int H = lcd.height();     // 222

    lcd.fillScreen(rgb(COL_BG));

    // ---- Title ------------------------------------------------------------
    lcd.setTextDatum(textdatum_t::top_left);
    lcd.setTextColor(rgb(COL_TITLE));
    lcd.setFont(&fonts::FreeSansBold12pt7b);
    lcd.drawString("Claude Usage Monitor", 16, 16);

    lcd.setFont(&fonts::FreeSans9pt7b);
    lcd.setTextColor(rgb(COL_SUB));
    lcd.drawString("ST7796 / LovyanGFX  -  480 x 222", 16, 48);

    // ---- Self-test label --------------------------------------------------
    lcd.setTextColor(rgb(COL_LABEL));
    lcd.drawString("Display self-test", 16, 92);

    // ---- Colored utilization bar -----------------------------------------
    const int   bx = 16, by = 120, bw = W - 32, bh = 36, r = 9;
    const float pct = 0.65f;        // placeholder fill

    lcd.fillRoundRect(bx, by, bw, bh, r, rgb(COL_TRACK));
    lcd.fillRoundRect(bx, by, (int)(bw * pct), bh, r, rgb(COL_ACCENT));
    lcd.drawRoundRect(bx, by, bw, bh, r, rgb(COL_BORDER));

    lcd.setTextDatum(textdatum_t::middle_center);
    lcd.setTextColor(TFT_WHITE);
    lcd.setFont(&fonts::FreeSansBold9pt7b);
    lcd.drawString("65%", bx + bw / 2, by + bh / 2);

    // ---- Footer -----------------------------------------------------------
    lcd.setTextDatum(textdatum_t::bottom_left);
    lcd.setFont(&fonts::Font0);
    lcd.setTextColor(rgb(COL_FOOTER));
    lcd.drawString("Stage 1 - display OK", 16, H - 10);
}

namespace {
// Shared header used by the Stage 2 screens.
void drawHeader(const char* title) {
    lcd.fillScreen(rgb(COL_BG));
    lcd.setTextDatum(textdatum_t::top_left);
    lcd.setTextColor(rgb(COL_ACCENT));
    lcd.setFont(&fonts::FreeSansBold12pt7b);
    lcd.drawString(title, 16, 16);
}

// "label: value" row in the muted/label palette.
void drawRow(const char* label, const String& value, int y) {
    lcd.setFont(&fonts::FreeSans9pt7b);
    lcd.setTextColor(rgb(COL_SUB));
    lcd.drawString(label, 16, y);
    lcd.setTextColor(rgb(COL_TITLE));
    lcd.drawString(value, 150, y);
}
}  // namespace

void drawProvisioning(const String& apSsid, const String& apPass,
                      const String& portalIp) {
    drawHeader("Setup mode");

    lcd.setFont(&fonts::FreeSans9pt7b);
    lcd.setTextColor(rgb(COL_LABEL));
    lcd.drawString("Join this WiFi, then open the page:", 16, 56);

    drawRow("WiFi:", apSsid, 88);
    drawRow("Pass:", apPass, 116);
    drawRow("Open:", "http://" + portalIp, 144);

    lcd.setTextDatum(textdatum_t::bottom_left);
    lcd.setFont(&fonts::Font0);
    lcd.setTextColor(rgb(COL_FOOTER));
    lcd.drawString("Stage 2 - awaiting setup", 16, lcd.height() - 10);
}

void drawUnlock(const String& portalUrl, int failsRemaining) {
    drawHeader("Locked");

    lcd.setFont(&fonts::FreeSans9pt7b);
    lcd.setTextColor(rgb(COL_LABEL));
    lcd.drawString("Open this page and enter your PIN:", 16, 56);

    drawRow("Open:", portalUrl, 92);

    lcd.setTextColor(failsRemaining <= 3 ? rgb(COL_ACCENT) : rgb(COL_SUB));
    lcd.drawString(String(failsRemaining) + " attempt(s) before wipe", 16, 124);

    lcd.setTextDatum(textdatum_t::bottom_left);
    lcd.setFont(&fonts::Font0);
    lcd.setTextColor(rgb(COL_FOOTER));
    lcd.drawString("Stage 2 - awaiting PIN", 16, lcd.height() - 10);
}

void drawStatus(const String& ssid, const String& ip, int rssi) {
    drawHeader("Claude Usage Monitor");

    drawRow("WiFi:", ssid, 64);
    drawRow("IP:", ip, 92);
    drawRow("Signal:", String(rssi) + " dBm", 120);

    lcd.setTextDatum(textdatum_t::bottom_left);
    lcd.setFont(&fonts::Font0);
    lcd.setTextColor(rgb(COL_FOOTER));
    lcd.drawString("Stage 2 - connected & unlocked", 16, lcd.height() - 10);
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
                lcd.fillRect(x + c * s, y + r * s, s, s, rgb(T_CORAL));
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
        lcd.fillRect(x + i * 6, y + 13 - h, 4, h, rgb(i < bars ? T_TITLE : T_TRACK));
    }
}

void drawBattery(int x, int y, int pct, bool charging) {
    const int w = 26, h = 13;
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    lcd.drawRoundRect(x, y, w, h, 2, rgb(T_TITLE));
    lcd.fillRect(x + w, y + 4, 2, h - 8, rgb(T_TITLE));   // nub
    uint32_t fill = (pct <= 15 && !charging) ? T_CORAL : T_GREEN;
    int fw = (w - 4) * pct / 100;
    if (fw > 0) lcd.fillRect(x + 2, y + 2, fw, h - 4, rgb(fill));
    if (charging) {                                       // little bolt
        lcd.setFont(&fonts::Font0);
        lcd.setTextDatum(textdatum_t::middle_center);
        lcd.setTextColor(rgb(T_BG));
        lcd.drawString("+", x + w / 2, y + h / 2);
    }
}

// Rounded "pill" badge, right-aligned to rightX. Returns its left edge.
int drawPill(const char* text, int rightX, int y) {
    lcd.setFont(&fonts::FreeSansBold9pt7b);
    int tw = lcd.textWidth(text);
    int pw = tw + 22, ph = 24;
    int px = rightX - pw;
    lcd.fillRoundRect(px, y, pw, ph, ph / 2, rgb(T_PILLBG));
    lcd.setTextDatum(textdatum_t::middle_center);
    lcd.setTextColor(rgb(T_PILLTX));
    lcd.drawString(text, px + pw / 2, y + ph / 2 + 1);
    return px;
}

void drawMetricCard(int yc, const char* label, float pct, const String& reset,
                    uint32_t barColor) {
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    const int cx = 12, cw = lcd.width() - 24, ch = 80;
    lcd.fillRoundRect(cx, yc, cw, ch, 10, rgb(T_CARD));

    // Big percentage.
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", (int)(pct + 0.5f));
    lcd.setFont(&fonts::FreeSansBold18pt7b);
    lcd.setTextDatum(textdatum_t::top_left);
    lcd.setTextColor(rgb(T_TITLE));
    lcd.drawString(buf, cx + 18, yc + 8);

    drawPill(label, cx + cw - 16, yc + 10);

    // Bar.
    const int bx = cx + 18, bw = cw - 36, by = yc + 44, bh = 14, r = 7;
    lcd.fillRoundRect(bx, by, bw, bh, r, rgb(T_TRACK));
    int fw = (int)(bw * pct / 100.0f);
    if (fw > 0) lcd.fillRoundRect(bx, by, fw < bh ? bh : fw, bh, r, rgb(barColor));

    // Reset countdown.
    lcd.setFont(&fonts::FreeSans9pt7b);
    lcd.setTextDatum(textdatum_t::top_left);
    lcd.setTextColor(rgb(T_MUTED));
    lcd.drawString("Resets in " + reset, bx, yc + 60);
}
}  // namespace

void drawDashboard(const Dashboard& d) {
    lcd.fillScreen(rgb(T_BG));

    // ---- Top bar ----------------------------------------------------------
    drawMascot(14, 8, 2);                       // 22x16
    lcd.setFont(&fonts::FreeSansBold12pt7b);
    lcd.setTextDatum(textdatum_t::top_center);
    lcd.setTextColor(rgb(T_TITLE));
    lcd.drawString("Usage", lcd.width() / 2, 8);

    drawBattery(lcd.width() - 42, 9, d.battery, d.charging);
    drawWifiBars(lcd.width() - 74, 9, d.rssi);

    // ---- Cards ------------------------------------------------------------
    drawMetricCard(36,  "Current", d.current, d.currentReset, T_CUR);
    drawMetricCard(120, "Weekly",  d.weekly,  d.weeklyReset,  T_WK);

    // ---- Status line ------------------------------------------------------
    lcd.setFont(&fonts::FreeSansBold9pt7b);
    lcd.setTextDatum(textdatum_t::top_center);
    lcd.setTextColor(rgb(T_CORAL));
    lcd.drawString("* " + d.status, lcd.width() / 2, 202);
}

void drawMessage(const String& title, const String& line) {
    drawHeader(title.c_str());
    lcd.setFont(&fonts::FreeSans9pt7b);
    lcd.setTextDatum(textdatum_t::top_left);
    lcd.setTextColor(rgb(COL_LABEL));
    lcd.drawString(line, 16, 80);
}

void drawApiError(int httpCode, const String& note) {
    drawHeader("API error");

    lcd.setFont(&fonts::FreeSans9pt7b);
    lcd.setTextColor(rgb(COL_TITLE));
    lcd.drawString("HTTP " + String(httpCode), 16, 70);

    lcd.setTextColor(rgb(COL_SUB));
    lcd.drawString(note, 16, 104);

    lcd.setTextDatum(textdatum_t::bottom_left);
    lcd.setFont(&fonts::Font0);
    lcd.setTextColor(rgb(COL_FOOTER));
    lcd.drawString("Stage 3 - poll failed", 16, lcd.height() - 10);
}

}  // namespace display
