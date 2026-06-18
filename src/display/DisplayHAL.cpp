// ============================================================================
//  DisplayHAL.cpp  -  implementation of the display drawing layer
//
//  All drawing happens on an off-screen sprite (canvas) in PSRAM and is blitted
//  to the panel in one pushSprite() per frame. This double-buffering removes the
//  clear-then-redraw flicker/tearing of drawing straight to the SPI panel.
// ============================================================================
#include "DisplayHAL.h"

#include <math.h>

#include "claudecode_bolt.h"
#include "claudecode_logo.h"
#include "claudecode_wordmark.h"
// Brand typography: Styrene B (Anthropic's UI sans), the same face Clawdmeter
// uses for the usage numbers. These generated headers are commercial-font glyph
// data, so they're git-ignored -- present on the maintainer's build, absent in
// the public repo, which falls back to the license-clean 1-bit NEXON font so a
// clone still builds. (Regenerate via tools/ttf_to_vlw.py / ttf_to_lgfx_gfxfont.py.)
//
// Big % number: anti-aliased VLW (Styrene 16pt digits + 14pt '%') when present,
// so curves and the percent sign render smooth instead of 1-bit jagged. The
// digits and '%' share a baseline; the '%' is a couple sizes down because
// Styrene's percent glyph is ~1.7x a digit's width.
#if __has_include("styrene_num_vlw.h")
  #include "styrene_num_vlw.h"        // StyreneNumVlw[] -- loadFont() data
  #include "styrene_pct_vlw.h"        // StyrenePctVlw[]
  #define CUM_NUM_VLW 1
#else
  #include "nexon_num.h"
  #define CUM_NUM_VLW 0
  #define CUM_NUM_FONT NexonNum
  #define CUM_PCT_FONT NexonNum       // fallback: '%' same face/size as the digits
#endif
// Label/countdown/clock text: anti-aliased Styrene VLW when present, 1-bit NEXON
// fallback otherwise. useTextFont() (below) selects the right one.
#if __has_include("styrene_text_vlw.h")
  #include "styrene_text_vlw.h"       // StyreneTextVlw[]
  #define CUM_TEXT_VLW 1
#else
  #include "nexon_text.h"
  #define CUM_TEXT_VLW 0
  #define CUM_TEXT_FONT NexonText
#endif

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

// Select the dashboard label font: anti-aliased Styrene VLW (loadFont) when
// available, else the 1-bit NEXON GFXfont (setFont). VLW uses one runtime-font
// slot, so each call reloads -- fine at the low rate these labels redraw.
inline void useTextFont() {
#if CUM_TEXT_VLW
    canvas.loadFont(StyreneTextVlw);
#else
    canvas.setFont(&CUM_TEXT_FONT);
#endif
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

// "label: value" row in the muted/label palette. Uses the anti-aliased Styrene
// VLW (same as the dashboard) so the detail/status screens read as cleanly.
void drawRow(const char* label, const String& value, int y) {
    useTextFont();
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextColor(rgb(COL_SUB));
    canvas.drawString(label, 16, y);
    canvas.setTextColor(rgb(COL_TITLE));
    canvas.drawString(value, 150, y);
}

// Escape a value for a WIFI: QR payload (\ ; , : " are reserved).
String qrEscape(const String& s) {
    String o; o.reserve(s.length() + 4);
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"') o += '\\';
        o += c;
    }
    return o;
}
}  // namespace

void drawProvisioning(const String& apSsid, const String& apPass,
                      const String& portalIp) {
    drawHeader("Setup mode");

    // Right: scan-to-join-WiFi QR (white margin so phones can read it).
    String payload = "WIFI:T:WPA;S:" + qrEscape(apSsid) + ";P:" + qrEscape(apPass) + ";;";
    uint8_t ver = payload.length() < 60 ? 4 : payload.length() < 100 ? 6 : 8;
    const int qw = 120, qx = canvas.width() - qw - 12, qy = 44;
    canvas.qrcode(payload.c_str(), qx, qy, qw, ver, true);
    canvas.setFont(&fonts::Font0);
    canvas.setTextDatum(textdatum_t::top_center);
    canvas.setTextColor(rgb(COL_SUB));
    canvas.drawString("scan to join WiFi", qx + qw / 2, qy + qw + 4);

    // Left: manual fallback + the page to open after joining.
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextColor(rgb(COL_LABEL));
    canvas.drawString("1. Join this WiFi", 16, 52);
    drawRow("WiFi:", apSsid, 78);
    drawRow("Pass:", apPass, 102);
    canvas.setTextColor(rgb(COL_LABEL));
    canvas.drawString("2. Open the page", 16, 134);
    drawRow("Open:", "http://" + portalIp, 160);

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
// ---- Stage 4 themed dashboard palette --------------------------------------
// Aligned to Anthropic's real brand design tokens (warm terra-cotta + cloud/
// warm-grey neutrals) instead of the earlier ad-hoc purple-leaning set. The
// coral now matches the captive portal's #d97757 exactly. Bar colors stay
// orange/lime (a deliberate functional distinction between the two windows).
constexpr uint32_t T_BG     = 0x0D0D12;  // near-black
constexpr uint32_t T_CARD   = 0x1F1F1E;  // card background (warm dark)
constexpr uint32_t T_TITLE  = 0xFAF9F5;  // primary text (warm cloud white)
constexpr uint32_t T_MUTED  = 0xB0AEA5;  // secondary text (warm grey, was lavender)
constexpr uint32_t T_TRACK  = 0x2A2A28;  // bar track (warm dark, was purple)
constexpr uint32_t T_CORAL  = 0xD97757;  // brand terra-cotta (matches portal)
constexpr uint32_t T_CUR    = 0xED7B3A;  // current bar (orange)
constexpr uint32_t T_WK     = 0xC2D74A;  // weekly bar (lime)
constexpr uint32_t T_PILLBG = 0x2A2A28;  // pill background (warm dark, was purple)
constexpr uint32_t T_PILLTX = 0xEDE8E0;  // pill text (warm off-white)
constexpr uint32_t T_GREEN  = 0x7BC86B;  // battery ok
constexpr uint32_t T_WARN   = 0xE0A24A;  // usage approaching the limit (amber, >=80%)
constexpr uint32_t T_CRIT   = 0xE5453A;  // usage at/over the limit (red, >=95%)

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

// Lucide-style battery: anti-aliased rounded outline + terminal nub, with the
// level shown as 0-3 discrete bars (charging shows a bolt instead). Square
// corners: axis-aligned fillRect edges are crisp without anti-aliasing.
void drawBattery(int x, int y, int pct, bool charging) {
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    const int bw = 29, bh = 15;
    const uint32_t line = T_TITLE;

    // Outline: outer rect in the line color, inner punched to bg (a 2px stroke),
    // plus the positive-terminal nub on the right.
    canvas.fillRect(x, y, bw, bh, rgb(line));
    canvas.fillRect(x + 2, y + 2, bw - 4, bh - 4, rgb(T_BG));
    canvas.fillRect(x + bw, y + (bh - 6) / 2, 3, 6, rgb(line));

    if (charging) {                          // shaped lightning bolt (Lucide charging)
        // A thin AA line can't read as a bolt at this size, so use the designed
        // bolt glyph; its 1-bit edges are negligible at 11x12 in green.
        drawBits(CC_BOLT, CC_BOLT_W, CC_BOLT_H,
                 x + (bw - CC_BOLT_W) / 2, y + (bh - CC_BOLT_H) / 2, 1, T_GREEN);
        return;
    }
    // Level bars: one per ~33%, coral when low. Wider cells + tighter gap so the
    // body looks fuller; left-aligned (fills from the left like a real battery).
    int bars = pct >= 67 ? 3 : pct >= 34 ? 2 : pct >= 1 ? 1 : 0;
    uint32_t bc = (pct <= 15) ? T_CORAL : T_GREEN;
    const int ix = x + 5, iy = y + 3, ih = bh - 6, barW = 5, gap = 2;
    for (int i = 0; i < bars; i++)
        canvas.fillRect(ix + i * (barW + gap), iy, barW, ih, rgb(bc));
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

// Shift a bar color toward amber (>=80%) then red (>=95%) so a near/over-limit
// card reads as a warning. Below 80% the series keeps its own hue.
uint32_t threshColor(uint32_t base, float pct) {
    if (pct < 80.0f) return base;
    if (pct < 95.0f) return lerpColor(base, T_WARN, (pct - 80.0f) / 15.0f);
    return lerpColor(T_WARN, T_CRIT, (pct - 95.0f) / 5.0f);
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

// The dynamic part of a card (big %, bar, spark) — the only thing that changes
// during a count-up. Drawn at absolute coords; the card background must already
// be present. Shared by the full render and the partial-band render.
// `pop` drives the spark burst; `glow` drives the white-tinted bar/number flash
// (they're decoupled so the glow can lag the sparks slightly).
void drawCardContent(int yc, float pct, uint32_t barColor, float pop, float glow) {
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    const int cx = 12, cw = canvas.width() - 24;
    barColor = threshColor(barColor, pct);                   // amber/red near the limit
    uint32_t pastel = lerpColor(barColor, 0xFFFFFF, 0.38f);   // flash peak: 38% toward white

    // Big percentage — glows toward the card's pastel tone as it lands. Centered
    // vertically between the card top (yc) and the bar top (yc+42) -> yc+21.
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", (int)(pct + 0.5f));   // digits only
    canvas.setTextColor(rgb(T_TITLE));            // number stays steady (only the bar flashes)
    // Digits and the '%' share a baseline (baseline_left datum) so the smaller
    // percent sign sits on the number's bottom line instead of floating in its
    // middle. baseY puts the digit row in the band between the card top and bar.
    // Styrene's '%' is ~1.7x a digit's width, so it's drawn a couple sizes down
    // (CUM_PCT_FONT) to stay proportionate.
    const int nx = cx + 18, baseY = yc + 33;
    canvas.setTextDatum(textdatum_t::baseline_left);
#if CUM_NUM_VLW
    // Anti-aliased VLW: one runtime-font slot, so load digits -> draw -> load '%'
    // -> draw. The arrays live in flash; loadFont reparses each call (cheap for
    // these tiny glyph sets). VLW blends over the card bg already painted here.
    canvas.loadFont(StyreneNumVlw);
    canvas.drawString(buf, nx, baseY);
    int dw = canvas.textWidth(buf);
    canvas.loadFont(StyrenePctVlw);
    canvas.drawString("%", nx + dw + 1, baseY);   // tight gap (Clawdmeter-style cohesion)
    canvas.unloadFont();                          // hand the font back to setFont() users
#else
    canvas.setFont(&CUM_NUM_FONT);
    canvas.drawString(buf, nx, baseY);            // big digits
    int dw = canvas.textWidth(buf);
    canvas.setFont(&CUM_PCT_FONT);
    canvas.drawString("%", nx + dw + 1, baseY);   // smaller, baseline-aligned, tight gap
#endif

    const int bx = cx + 18, bw = cw - 36, by = yc + 42, bh = 12, r = 6;
    canvas.fillRoundRect(bx, by, bw, bh, r, rgb(T_TRACK));
    int fw = (int)(bw * pct / 100.0f);
    uint32_t fill = (glow > 0) ? lerpColor(barColor, pastel, glow) : barColor;
    int filled = fw < bh ? bh : fw;
    if (fw > 0) canvas.fillRoundRect(bx, by, filled, bh, r, rgb(fill));

    if (pop > 0.0f) {                       // spark burst at the bar's leading edge
        int sx = bx + filled; if (sx > bx + bw) sx = bx + bw;
        drawSparks(sx, by + bh / 2, pop, barColor, pastel);
    }
}

void drawMetricCard(int yc, const char* label, float pct, const String& reset,
                    uint32_t barColor, float pop = 0.0f) {
    const int cx = 12, cw = canvas.width() - 24, ch = 82;
    canvas.fillRoundRect(cx, yc, cw, ch, 10, rgb(T_CARD));
    drawCardContent(yc, pct, barColor, pop, pop);   // full render: glow == pop (both ~0 here)
    drawPill(label, cx + cw - 16, yc + 8);

    // Reset countdown — vertically centered between the bar bottom (yc+54) and
    // the card bottom (yc+82), i.e. at yc+68, using a middle datum so the font's
    // real glyph height (not its top padding) is what gets centered.
    useTextFont();
    canvas.setTextDatum(textdatum_t::middle_left);
    canvas.setTextColor(rgb(T_MUTED));           // warm grey (was leftover lavender)
    canvas.drawString("Resets in " + reset, cx + 18, yc + 68);
}

// Redraw ONLY one card's dynamic content and push ONLY that region to the LCD.
// The static card (background, pill, reset text) must already be on screen from a
// prior full drawDashboard(); we never touch the pill or the rounded corners.
void drawCardBand(int yc, float pct, uint32_t barColor, float pop, float glow,
                  const char* label) {
    const int cx = 12, cw = canvas.width() - 24;
    // Clear the changing areas to card bg: number (left) and the bar+spark band.
    // The band stops at yc+59 so it never touches the reset text (~yc+61+). Up-
    // sparks (to ~yc+31) are inside; the lone down-spark (~yc+62) falls outside
    // the clip, so it's drawn but never pushed -> invisible, no clipping the text.
    canvas.fillRect(cx + 16, yc + 1,  172,     40, rgb(T_CARD));   // number (18pt -> taller)
    canvas.fillRect(cx + 16, yc + 28, cw - 20, 31, rgb(T_CARD));   // bar + sparks (right room)
    drawCardContent(yc, pct, barColor, pop, glow);
    drawPill(label, cx + cw - 16, yc + 8);   // bar clear nicks the pill bottom; restore it
    // Transfer only the bounding box of the changed region (clip the push).
    lcd.setClipRect(cx + 16, yc + 1, cw - 20, 58);
    canvas.pushSprite(0, 0);
    lcd.clearClipRect();
}

// Top-bar wall clock (center slot, NexonText 11pt). `colonOn` blinks the ':'
// WITHOUT shifting the digits: the whole string is laid out with the colon, then
// when off we overpaint just the colon glyph's column in bg. Caller positions /
// pushes; this only paints onto the canvas.
constexpr int CLOCK_Y = 2;   // raised to align the VLW clock with the mascot/wifi/battery
                             // (VLW's tall ascent otherwise drops it ~5px low)
void drawClockText(const String& clock, bool colonOn) {
    const int cx = canvas.width() / 2;
    useTextFont();
    canvas.setTextDatum(textdatum_t::top_center);
    canvas.setTextColor(rgb(T_TITLE));
    canvas.drawString(clock, cx, CLOCK_Y);
    if (!colonOn) {
        int c = clock.indexOf(':');
        if (c >= 0) {                                  // bitmap-font advances are additive
            int left   = cx - canvas.textWidth(clock) / 2;
            int colonX = left + canvas.textWidth(clock.substring(0, c).c_str());
            int colonW = canvas.textWidth(":");
            // Clear down to y33 (just above the first card at y34): the VLW colon's
            // lower dot sits near the baseline (~y30), below the old fixed 18px,
            // so a short clear left a residual dot in the off phase.
            canvas.fillRect(colonX, CLOCK_Y, colonW, 33 - CLOCK_Y, rgb(T_BG));
        }
    }
}
}  // namespace

void drawDashboard(const Dashboard& d) {
    canvas.fillScreen(rgb(T_BG));

    // ---- Top bar (icons aligned to the card box edges: left 12, right 468) -
    drawLogo(12, 3, 1);                         // 30x30 logo, left edge on box left
    // Wall clock replaces the (redundant) "Usage" title in the center slot.
    drawClockText(d.clock.length() ? d.clock : "--:--", true);

    drawBattery(canvas.width() - 44, 9, d.battery, d.charging);  // nub tip on box right (468)
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

// Count-up frame: redraw + push ONLY the two cards' dynamic content (big %, bar,
// spark). ~10x less to render and transfer than a full drawDashboard, so the
// count-up animates well above 30fps. Requires a prior full drawDashboard().
void drawDashboardBands(float curPct, float wkPct, float curPop, float wkPop,
                        float curGlow, float wkGlow) {
    drawCardBand(34,  curPct, T_CUR, curPop, curGlow, "Current");
    drawCardBand(118, wkPct,  T_WK,  wkPop,  wkGlow,  "Weekly");
}

void drawClockColon(const String& clock, bool colonOn) {
    const int cx = canvas.width() / 2;
    canvas.fillRect(cx - 60, 0, 120, 33, rgb(T_BG));   // clock band (tall enough for the VLW colon)
    drawClockText(clock, colonOn);
    lcd.setClipRect(cx - 60, 0, 120, 33);              // push just that band (full colon height)
    canvas.pushSprite(0, 0);
    lcd.clearClipRect();
}

void drawDetail(const Detail& d) {
    drawHeader("Details");
    char buf[40];

    // Start a touch higher and pack tighter (dy=23) so the taller VLW rows still
    // leave a bottom margin under the 7th row (was jammed against the edge).
    const int y0 = 48, dy = 23;
    drawRow("WiFi:",   d.ssid, y0);
    drawRow("IP:",     d.ip, y0 + dy);
    snprintf(buf, sizeof(buf), "%d dBm", d.rssi);
    drawRow("Signal:", buf, y0 + 2 * dy);
    snprintf(buf, sizeof(buf), "%d%%%s", d.battery, d.charging ? " (chg)" : "");
    String battVal(buf);
    if (d.battEst.length()) battVal += "  " + d.battEst;   // append "~Hh Mm (R%/h)"
    drawRow("Battery:", battVal, y0 + 3 * dy);
    drawRow("5h reset:", d.reset5h, y0 + 4 * dy);
    drawRow("7d reset:", d.reset7d, y0 + 5 * dy);
    drawRow("Uptime:",   d.uptime, y0 + 6 * dy);

    present();
}

namespace {
int plotY(float v, int y0, int h) {
    if (v < 0) v = 0; if (v > 100) v = 100;
    return y0 + (int)(h * (100 - v) / 100);
}

// Compact "1h 20m" / "45m" / "<1m" / "--" from whole minutes.
String etaStr(int mins) {
    if (mins < 0) return "--";
    if (mins < 1) return "<1m";
    int h = mins / 60, m = mins % 60;
    if (h >= 24) return String(h / 24) + "d " + String(h % 24) + "h";
    if (h > 0)   return String(h) + "h " + String(m) + "m";
    return String(m) + "m";
}

// One footer line: "5h  now NN%   <proj>" in `color`. <proj> is the limit
// projection -- "ETA <t>" if 100% is reached before the reset, else "end NN%"
// (projected utilization when the window resets), else "end --". (No "max": the
// window climbs monotonically then clears, so its peak is just `now`.)
void drawHistStat(const char* tag, const float* v, int count, int eta, int peak,
                  uint32_t color, int y) {
    float now = v[count - 1];
    char proj[24];
    if (eta >= 0)       snprintf(proj, sizeof(proj), "ETA %s", etaStr(eta).c_str());
    else if (peak >= 0) snprintf(proj, sizeof(proj), "end %d%%", peak);
    else                snprintf(proj, sizeof(proj), "end --");
    char buf[64];
    snprintf(buf, sizeof(buf), "%s  now %d%%   %s", tag, (int)(now + 0.5f), proj);
    canvas.setFont(&fonts::FreeSansBold12pt7b);
    canvas.setTextDatum(textdatum_t::top_center);
    canvas.setTextColor(rgb(color));
    canvas.drawString(buf, canvas.width() / 2, y);
}
}  // namespace

void drawHistory(const float* h5, const float* h7, int count,
                 int eta5min, int eta7min, int peak5, int peak7) {
    drawHeader("History");

    // Legend (top-right).
    canvas.setFont(&fonts::FreeSansBold9pt7b);
    canvas.setTextDatum(textdatum_t::top_right);
    canvas.setTextColor(rgb(T_CUR));
    canvas.drawString("5h", canvas.width() - 64, 18);
    canvas.setTextColor(rgb(T_WK));
    canvas.drawString("7d", canvas.width() - 22, 18);

    // Graph shrunk to the top ~2/3 to make room for a stats footer below.
    const int x0 = 32, y0 = 48, w = canvas.width() - 52, h = 100;

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

    // Stats footer: now/max + limit projection (ETA or end %), one 12pt line
    // per series.
    drawHistStat("5h", h5, count, eta5min, peak5, T_CUR, 162);
    drawHistStat("7d", h7, count, eta7min, peak7, T_WK,  192);
    present();
}

namespace {
// A dotted line from (x0,y0) to (x1,y1): small dots spaced ~`step` px along the
// segment. Used for the battery projection (a guess, so it reads as not-solid).
void drawDottedLine(int x0, int y0, int x1, int y1, int step, int rad,
                    uint32_t color) {
    float dx = (float)(x1 - x0), dy = (float)(y1 - y0);
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) { canvas.fillCircle(x0, y0, rad, rgb(color)); return; }
    int n = (int)(len / step);
    for (int i = 0; i <= n; i++) {
        float t = (float)i / (n > 0 ? n : 1);
        canvas.fillCircle(x0 + (int)(dx * t), y0 + (int)(dy * t), rad, rgb(color));
    }
}

// Whole-minutes -> compact "1d 13h" / "5h 20m" / "45m" / "<1m" / "--".
String battTime(int mins) {
    if (mins < 0)  return "--";
    if (mins < 1)  return "<1m";
    int h = mins / 60, m = mins % 60;
    if (h >= 24) return String(h / 24) + "d " + String(h % 24) + "h";
    if (h > 0)   return String(h) + "h " + String(m) + "m";
    return String(m) + "m";
}
}  // namespace

void drawBatteryPage(const BatteryPage& b) {
    drawHeader("Battery");

    int pct = b.pct;
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    uint32_t pctCol = (pct <= 15) ? T_CORAL : T_GREEN;

    // --- Header: big % (left) + time-left / status (right) ------------------
    char num[8];
    snprintf(num, sizeof(num), "%d", pct);
    canvas.setTextDatum(textdatum_t::baseline_left);
    canvas.setTextColor(rgb(pctCol));
    const int nx = 18, baseY = 84;
    int dw;
#if CUM_NUM_VLW
    canvas.loadFont(StyreneNumVlw);
    canvas.drawString(num, nx, baseY);
    dw = canvas.textWidth(num);
    canvas.loadFont(StyrenePctVlw);
    canvas.drawString("%", nx + dw + 1, baseY);
    canvas.unloadFont();
#else
    canvas.setFont(&CUM_NUM_FONT);
    canvas.drawString(num, nx, baseY);
    dw = canvas.textWidth(num);
    canvas.setFont(&CUM_PCT_FONT);
    canvas.drawString("%", nx + dw + 1, baseY);
#endif

    // Right side: "time left" (or charging/measuring note) over a small caption.
    String big, cap;
    if (b.charging || !b.tracking) {
        big = "On USB";
        cap = "charging";
    } else if (b.measuring) {
        big = "measuring";
        cap = "need a few % drop";
    } else {
        big = battTime(b.etaMin) + " left";
        char r[24];
        snprintf(r, sizeof(r), "%.1f%%/h", b.ratePerHr);
        cap = String(r);
    }
    canvas.setFont(&fonts::FreeSansBold12pt7b);
    canvas.setTextDatum(textdatum_t::baseline_right);
    canvas.setTextColor(rgb(T_TITLE));
    canvas.drawString(big, canvas.width() - 18, 66);
    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextColor(rgb(T_MUTED));
    canvas.drawString(cap, canvas.width() - 18, 88);

    // --- Discharge graph ----------------------------------------------------
    const int x0 = 32, y0 = 104, w = canvas.width() - 52, h = 86;

    // Gridlines + % labels at 0 / 50 / 100.
    canvas.setFont(&fonts::Font0);
    canvas.setTextDatum(textdatum_t::middle_right);
    for (int p = 0; p <= 100; p += 50) {
        int gy = y0 + h * (100 - p) / 100;
        canvas.drawFastHLine(x0, gy, w, rgb(T_TRACK));
        canvas.setTextColor(rgb(T_MUTED));
        canvas.drawString(String(p), x0 - 5, gy);
    }

    if (!b.tracking || b.histCount < 1) {
        canvas.setFont(&fonts::FreeSans9pt7b);
        canvas.setTextDatum(textdatum_t::middle_center);
        canvas.setTextColor(rgb(T_MUTED));
        const char* msg = b.tracking ? "collecting data..." : "plug out to track";
        canvas.drawString(msg, canvas.width() / 2, y0 + h / 2);
        present();
        return;
    }

    // X-axis spans 0..xMax minutes: the recorded history plus, if known, the
    // projection out to 0%. So the solid line fills the left part and the dotted
    // projection reaches the right edge exactly at the estimated empty time.
    int nowMin = b.histMin[b.histCount - 1];
    int endMin = (b.etaMin >= 0) ? nowMin + b.etaMin : nowMin;
    int xMax   = endMin > 0 ? endMin : 1;
    auto px = [&](int m) { return x0 + (int)((long)m * w / xMax); };

    // Solid recorded line (2px) through the samples.
    for (int i = 1; i < b.histCount; i++) {
        int xa = px(b.histMin[i - 1]), xb = px(b.histMin[i]);
        int ya = plotY(b.histPct[i - 1], y0, h), yb = plotY(b.histPct[i], y0, h);
        canvas.drawLine(xa, ya, xb, yb, rgb(pctCol));
        canvas.drawLine(xa, ya + 1, xb, yb + 1, rgb(pctCol));
    }

    // Dotted projection from the latest sample down to 0% at endMin.
    int cxp = px(nowMin), cyp = plotY(pct, y0, h);
    if (b.etaMin >= 0)
        drawDottedLine(cxp, cyp, px(endMin), plotY(0, y0, h), 7, 1, T_MUTED);
    canvas.fillCircle(cxp, cyp, 3, rgb(pctCol));     // "you are here" marker

    // X-axis caption: elapsed-since-unplug (left) and projected-empty (right).
    canvas.setFont(&fonts::Font0);
    canvas.setTextColor(rgb(T_MUTED));
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.drawString(battTime(nowMin) + " on battery", x0, y0 + h + 5);
    if (b.etaMin >= 0) {
        canvas.setTextDatum(textdatum_t::top_right);
        canvas.drawString("empty in " + battTime(b.etaMin), x0 + w, y0 + h + 5);
    }
    present();
}

void drawSplash(int yoff) {
    canvas.fillScreen(rgb(T_BG));
    drawBits(CC_LOGO_L, CC_LOGO_L_W, CC_LOGO_L_H,           // 90px native logo
             canvas.width() / 2 - CC_LOGO_L_W / 2, 8 + yoff, 1, T_CORAL);
    // Wordmark is an anti-aliased RGB565 image (blended over T_BG) for smooth
    // edges; 1-bit text looked jagged.
    canvas.pushImage(canvas.width() / 2 - CC_TEXT_W / 2, 104 + yoff,
                     CC_TEXT_W, CC_TEXT_H, (const lgfx::rgb565_t*)CC_TEXT);
    present();
}

void drawResetHold(float frac) {
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    const int yoff = SPLASH_REST_Y;        // same position as the settled splash
    canvas.fillScreen(rgb(T_BG));

    // Hint above, splash centered, fill bar below — the logo/wordmark sit exactly
    // where the plain splash does, so appearing/clearing the bar causes no jump.
    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextDatum(textdatum_t::top_center);
    canvas.setTextColor(rgb(T_MUTED));
    canvas.drawString("keep holding to reset", canvas.width() / 2, 10);

    drawBits(CC_LOGO_L, CC_LOGO_L_W, CC_LOGO_L_H,
             canvas.width() / 2 - CC_LOGO_L_W / 2, 8 + yoff, 1, T_CORAL);
    canvas.pushImage(canvas.width() / 2 - CC_TEXT_W / 2, 104 + yoff,
                     CC_TEXT_W, CC_TEXT_H, (const lgfx::rgb565_t*)CC_TEXT);

    const int bw = 220, bx = canvas.width() / 2 - bw / 2, by = 202, bh = 8, r = 4;
    canvas.fillRoundRect(bx, by, bw, bh, r, rgb(T_TRACK));
    int fw = (int)(bw * frac);
    if (fw > 0) canvas.fillRoundRect(bx, by, fw, bh, r, rgb(T_CORAL));
    present();
}

void drawBootBusy(int frame) {
    canvas.fillScreen(rgb(T_BG));

    // Logo + wordmark stay fixed at the rest position; only the ring below spins.
    const int yoff = SPLASH_REST_Y;
    drawBits(CC_LOGO_L, CC_LOGO_L_W, CC_LOGO_L_H,
             canvas.width() / 2 - CC_LOGO_L_W / 2, 8 + yoff, 1, T_CORAL);
    canvas.pushImage(canvas.width() / 2 - CC_TEXT_W / 2, 104 + yoff,
                     CC_TEXT_W, CC_TEXT_H, (const lgfx::rgb565_t*)CC_TEXT);

    // Indeterminate spinner: a coral arc rotates around a faint ring. Rotation is
    // continuous, so (unlike a linear marquee) there's no wrap seam or square end.
    const int cx = canvas.width() / 2, cy = 184, r0 = 9, r1 = 13;
    canvas.fillArc(cx, cy, r0, r1, 0, 360, rgb(T_TRACK));        // faint full ring
    int a = (int)((uint32_t)frame * 6u % 360u);
    canvas.fillArc(cx, cy, r0, r1, a, a + 110, rgb(T_CORAL));    // rotating arc
    present();
}

// Dim every pixel of a 16-bit sprite to `pct`% brightness in place (a flat dark
// veil = compositing black at (100-pct)% alpha over it). readPixel/drawPixel keep
// this independent of the sprite's internal byte order; it's a one-time cost.
void dimSprite(lgfx::LGFX_Sprite& s, int pct) {
    const int num = (pct * 256) / 100;     // fixed-point scale
    const int w = s.width(), h = s.height();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint16_t p = (uint16_t)s.readPixel(x, y);
            uint16_t r = (p >> 11) & 0x1F, g = (p >> 5) & 0x3F, b = p & 0x1F;
            r = (uint16_t)((r * num) >> 8);
            g = (uint16_t)((g * num) >> 8);
            b = (uint16_t)((b * num) >> 8);
            s.drawPixel(x, y, (uint16_t)((r << 11) | (g << 5) | b));
        }
    }
}

void drawRefreshAnim(int frame) {
    // Backdrop = the dashboard that's still on `canvas` when the refresh starts,
    // dimmed once and reused each frame, so the manual refresh reads as an overlay
    // on the live screen rather than a full black-out. Falls back to a black
    // background if the snapshot sprite can't be allocated.
    static lgfx::LGFX_Sprite veil(&lcd);
    static bool veilReady = false;
    if (frame == 0) {
        if (!veilReady) {
            veil.setPsram(true);
            veil.setColorDepth(16);
            veilReady = (veil.createSprite(canvas.width(), canvas.height()) != nullptr);
        }
        if (veilReady) {
            canvas.pushSprite(&veil, 0, 0);    // capture the current dashboard
            dimSprite(veil, 5);                // ~95% black veil over it
        }
    }
    if (veilReady) veil.pushSprite(&canvas, 0, 0);
    else           canvas.fillScreen(rgb(T_BG));

    // Logo bobs up and down on a short cycle, centered over the dimmed dashboard.
    static const int kBob[] = {0, 4, 8, 10, 8, 4};
    int oy = kBob[frame % 6];
    int mw = CC_LOGO_M_W;                  // 60px native logo
    drawBits(CC_LOGO_M, CC_LOGO_M_W, CC_LOGO_M_H,
             canvas.width() / 2 - mw / 2, 78 + oy, 1, T_CORAL);

    // Static "Refreshing..." — the bobbing logo conveys activity, so the text
    // stays still (no cycling dots).
    canvas.setFont(&fonts::FreeSansBold12pt7b);
    canvas.setTextDatum(textdatum_t::top_center);
    canvas.setTextColor(rgb(T_CORAL));
    canvas.drawString("Refreshing...", canvas.width() / 2, 150);
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
