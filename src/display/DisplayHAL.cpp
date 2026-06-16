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
// One labeled utilization bar with a percentage and a reset countdown.
void drawBar(const char* label, float pct, const String& reset, int y) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;

    const int bx = 16, bw = lcd.width() - 32, bh = 30, r = 8;

    // Label (left) + countdown (right) above the bar.
    lcd.setFont(&fonts::FreeSans9pt7b);
    lcd.setTextDatum(textdatum_t::top_left);
    lcd.setTextColor(rgb(COL_LABEL));
    lcd.drawString(label, bx, y);
    lcd.setTextDatum(textdatum_t::top_right);
    lcd.setTextColor(rgb(COL_SUB));
    lcd.drawString("resets " + reset, bx + bw, y);

    const int by = y + 24;
    lcd.fillRoundRect(bx, by, bw, bh, r, rgb(COL_TRACK));
    lcd.fillRoundRect(bx, by, (int)(bw * pct / 100.0f), bh, r, rgb(COL_ACCENT));
    lcd.drawRoundRect(bx, by, bw, bh, r, rgb(COL_BORDER));

    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", (int)(pct + 0.5f));
    lcd.setTextDatum(textdatum_t::middle_center);
    lcd.setTextColor(TFT_WHITE);
    lcd.setFont(&fonts::FreeSansBold9pt7b);
    lcd.drawString(buf, bx + bw / 2, by + bh / 2);
}
}  // namespace

void drawDashboard(float pct5h, const String& reset5h,
                   float pct7d, const String& reset7d) {
    drawHeader("Claude Usage Monitor");
    drawBar("Current (5h)", pct5h, reset5h, 58);
    drawBar("Weekly (7d)",  pct7d, reset7d, 130);

    lcd.setTextDatum(textdatum_t::bottom_left);
    lcd.setFont(&fonts::Font0);
    lcd.setTextColor(rgb(COL_FOOTER));
    lcd.drawString("Stage 3 - live", 16, lcd.height() - 10);
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
