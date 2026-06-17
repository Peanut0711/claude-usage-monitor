// ============================================================================
//  Portal.cpp
// ============================================================================
#include "Portal.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

#include "../config.h"
#include "../secure/CredentialStore.h"
#include "Api.h"
#include "Net.h"

namespace {

enum class Mode { Idle, Setup, Unlock };

Mode       gMode = Mode::Idle;
WebServer  gServer(CUM_PORTAL_PORT);
DNSServer  gDns;
bool       gDnsUp = false;

// Latched flow result, consumed by handle().
portal::Event gPending = portal::Event::None;
String        gToken;

// Networks found by scanNetworks(), shown in the setup dropdown.
String        gScan[CUM_SCAN_MAX];
int           gScanN = 0;

// --- Pages ------------------------------------------------------------------
// Escape user-supplied text (scanned SSIDs, echoed input) for safe HTML.
String htmlEscape(const String& s) {
    String o;
    o.reserve(s.length() + 8);
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        switch (c) {
            case '&': o += "&amp;";  break;
            case '<': o += "&lt;";   break;
            case '>': o += "&gt;";   break;
            case '"': o += "&quot;"; break;
            default:  o += c;
        }
    }
    return o;
}

// The setup form, built fresh so the scanned-SSID dropdown can be injected.
// Picking from the dropdown copies the SSID into the text input (which stays
// the submitted source of truth, so hidden/manual SSIDs still work).
String setupHtml() {
    String h = R"HTML(<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Claude Usage Monitor</title><style>
body{font-family:system-ui,sans-serif;background:#0e1016;color:#ececec;margin:0;padding:24px}
h1{color:#d97757;font-size:20px}label{display:block;margin:14px 0 4px;color:#c8ccd0}
input,textarea,select{width:100%;box-sizing:border-box;padding:10px;border-radius:8px;
border:1px solid #3a404a;background:#1a1d24;color:#ececec;font-size:16px}
textarea{height:90px}button{margin-top:20px;width:100%;padding:14px;border:0;
border-radius:8px;background:#d97757;color:#fff;font-size:16px;font-weight:600}
small{color:#8a9099}</style></head><body>
<h1>Claude Usage Monitor</h1>
<form method="POST" action="/save">
<label>WiFi network (2.4GHz only)</label>
<select onchange="document.getElementById('ssid').value=this.value">)HTML";

    if (gScanN == 0) {
        h += "<option value=\"\">(no networks found - type below)</option>";
    } else {
        h += "<option value=\"\">Pick a network...</option>";
        for (int i = 0; i < gScanN; i++) {
            String e = htmlEscape(gScan[i]);
            h += "<option value=\"" + e + "\">" + e + "</option>";
        }
        h += "<option value=\"\">Other / hidden...</option>";
    }

    h += R"HTML(</select>
<input id="ssid" name="ssid" maxlength="32" placeholder="or type SSID" required>
<label>WiFi password</label><input name="pass" type="password" maxlength="64">
<label>OAuth token</label>
<textarea name="token" placeholder="run: claude setup-token"></textarea>
<label>PIN (4 digits)</label>
<input name="pin" inputmode="numeric" pattern="[0-9]{4}" maxlength="4">
<small>The PIN encrypts your token and is never stored.<br>
Adding/changing WiFi? Leave token &amp; PIN blank: your token is kept and the
network is remembered (up to 3, e.g. home + office).</small>
<button type="submit">Save &amp; verify</button></form></body></html>)HTML";
    return h;
}

// A styled result page for verify failures, with a button back to the form.
// `title` is escaped here; `body` may contain markup we built ourselves.
String resultHtml(const String& title, const String& body) {
    String h = R"HTML(<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Setup</title><style>body{font-family:system-ui,sans-serif;background:#0e1016;
color:#ececec;margin:0;padding:32px 24px}h1{color:#d97757;font-size:20px}
p{color:#c8ccd0;line-height:1.5}code{background:#1a1d24;padding:1px 5px;
border-radius:4px;color:#d97757}a{display:block;margin-top:24px;text-align:center;
padding:14px;border-radius:8px;background:#d97757;color:#fff;text-decoration:none;
font-weight:600}</style></head><body><h1>)HTML";
    h += htmlEscape(title);
    h += "</h1><p>";
    h += body;
    h += "</p><a href=\"/\">Go back</a></body></html>";
    return h;
}

const char SAVED_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Saved</title><style>body{font-family:system-ui,sans-serif;background:#0e1016;
color:#ececec;text-align:center;padding:60px 24px}h1{color:#d97757}</style></head>
<body><h1>Saved</h1><p>Rebooting and connecting to WiFi&hellip;</p></body></html>)HTML";

// Build the unlock page with the attempts-remaining notice injected.
String unlockHtml(const String& note) {
    String h = R"HTML(<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Unlock</title><style>body{font-family:system-ui,sans-serif;background:#0e1016;
color:#ececec;margin:0;padding:24px}h1{color:#d97757;font-size:20px}
label{display:block;margin:14px 0 4px;color:#c8ccd0}
input{width:100%;box-sizing:border-box;padding:10px;border-radius:8px;
border:1px solid #3a404a;background:#1a1d24;color:#ececec;font-size:16px}
button{margin-top:20px;width:100%;padding:14px;border:0;border-radius:8px;
background:#d97757;color:#fff;font-size:16px;font-weight:600}
.note{color:#e0a;margin-top:12px}</style></head><body>
<h1>Unlock token</h1><form method="POST" action="/unlock">
<label>PIN (4 digits)</label>
<input name="pin" inputmode="numeric" pattern="[0-9]{4}" maxlength="4" required autofocus>
<button type="submit">Unlock</button></form>)HTML";
    if (note.length()) { h += "<p class=\"note\">"; h += note; h += "</p>"; }
    h += "</body></html>";
    return h;
}

void sendProgmem(const char* page) {
    gServer.send_P(200, "text/html", page);
}

// --- Handlers ---------------------------------------------------------------
void handleSetupRoot() { gServer.send(200, "text/html", setupHtml()); }

// Redirect any off-portal probe to the form. Returning a 302 (instead of the
// expected 204 / "Success" body) is what makes Android/iOS/Windows pop up the
// captive-portal sign-in window automatically when joining the AP.
void handleCaptive() {
    String loc = "http://" + IPAddress(CUM_AP_IP).toString() + "/";
    gServer.sendHeader("Location", loc, true);
    gServer.send(302, "text/plain", "");
}

void handleSave() {
    String ssid  = gServer.arg("ssid");
    String pass  = gServer.arg("pass");
    String token = gServer.arg("token");
    String pin   = gServer.arg("pin");
    ssid.trim();
    token.trim();
    pin.trim();

    // Blank token + already provisioned = change WiFi only, keep the token.
    bool wifiOnly = (token.length() == 0 && credentials::isProvisioned());

    String err;
    if (ssid.length() == 0)
        err = "WiFi name (SSID) is empty.";
    else if (ssid.length() > CUM_SSID_MAX_LEN)
        err = "WiFi name too long: " + String(ssid.length()) + " > 32.";
    else if (pass.length() > CUM_PASS_MAX_LEN)
        err = "WiFi password too long: " + String(pass.length()) + " > 64.";
    else if (!wifiOnly && token.length() == 0)
        err = "Token is empty.";
    else if (token.length() > CUM_TOKEN_MAX_LEN)
        err = "Token too long: " + String(token.length()) + " > 1024.";
    else if (!wifiOnly && pin.length() != CUM_PIN_LEN)
        err = "PIN must be exactly 4 digits (you entered " + String(pin.length()) + ").";

    if (err.length()) {
        gServer.send(400, "text/html", resultHtml("Check your entries", htmlEscape(err)));
        return;
    }

    // --- Verify before committing -------------------------------------------
    // Bring up STA next to the AP and actually connect, so a wrong WiFi
    // password or a dead token is caught here instead of turning into a
    // reboot loop the user can't diagnose. The phone stays on the AP.
    String esc = htmlEscape(ssid);
    if (!net::apStaConnect(ssid, pass, CUM_VERIFY_WIFI_TIMEOUT_MS)) {
        net::apStaDisconnect();
        gServer.send(400, "text/html", resultHtml("Couldn't join \"" + esc + "\"",
            "Wrong password, or the network is out of range / not 2.4GHz. "
            "Double-check and try again."));
        return;
    }

    if (!wifiOnly) {
        api::Usage u = api::poll(token);
        // The probe authenticates as long as the server responds with the
        // rate-limit headers; 200 (or even a 400 bad-request) means the token
        // was accepted. 401 is the token being rejected.
        bool tokenOk = (u.httpCode == 200 || u.httpCode == 400);
        if (!tokenOk) {
            net::apStaDisconnect();
            String why = (u.httpCode == 401)
                ? "The token was rejected (401). Re-run <code>claude setup-token</code> "
                  "and paste the new sk-ant-oat... value."
                : (u.httpCode <= 0)
                ? "Joined the WiFi, but couldn't reach the API. Check internet "
                  "access on this network and try again."
                : "Unexpected response from the API (HTTP " + String(u.httpCode) + ").";
            gServer.send(400, "text/html", resultHtml("Token check failed", why));
            return;
        }
    }
    net::apStaDisconnect();

    // --- Commit --------------------------------------------------------------
    bool ok = wifiOnly ? credentials::addWifi(ssid, pass)
                       : credentials::provision(ssid, pass, token, pin);
    if (ok) {
        sendProgmem(SAVED_HTML);
        gPending = portal::Event::Provisioned;   // main reboots
    } else {
        gServer.send(500, "text/html",
                     resultHtml("Save failed", "Storage/crypto error while saving."));
    }
}

void handleUnlockRoot() { gServer.send(200, "text/html", unlockHtml("")); }

void handleUnlock() {
    String pin = gServer.arg("pin");
    String tok;
    switch (credentials::unlock(pin, tok)) {
        case credentials::UnlockResult::Ok:
            gToken = tok;
            gServer.send(200, "text/html",
                         "<!DOCTYPE html><meta charset=utf-8><body "
                         "style='font-family:sans-serif;background:#0e1016;color:#ececec;"
                         "text-align:center;padding:60px'><h1 style='color:#d97757'>"
                         "Unlocked</h1><p>Monitoring started.</p></body>");
            gPending = portal::Event::Unlocked;
            break;
        case credentials::UnlockResult::WrongPin: {
            String note = "Wrong PIN. " + String(credentials::failsRemaining()) +
                          " attempt(s) left before wipe.";
            gServer.send(401, "text/html", unlockHtml(note));
            break;
        }
        case credentials::UnlockResult::LockedOut:
            gServer.send(403, "text/html",
                         "<body style='font-family:sans-serif'>Too many failures. "
                         "Credentials wiped &mdash; rebooting to setup.</body>");
            gPending = portal::Event::Provisioned;  // force re-setup via reboot
            break;
        default:
            gServer.send(500, "text/plain", "Internal error.");
            break;
    }
}

void installRoutes() {
    if (gMode == Mode::Setup) {
        gServer.on("/", HTTP_GET, handleSetupRoot);
        gServer.on("/save", HTTP_POST, handleSave);
        // Catch-all: redirect OS connectivity probes to the form so the
        // captive-portal window opens by itself when joining the AP.
        gServer.onNotFound(handleCaptive);
    } else {
        gServer.on("/", HTTP_GET, handleUnlockRoot);
        gServer.on("/unlock", HTTP_POST, handleUnlock);
        gServer.onNotFound(handleUnlockRoot);
    }
}

}  // namespace

namespace portal {

void scanNetworks() {
    gScanN = net::scanNetworks(gScan, CUM_SCAN_MAX);
    Serial.printf("[setup] scan found %d network(s)\n", gScanN);
}

void beginSetup() {
    gMode = Mode::Setup;
    gPending = Event::None;
    gToken = "";
    gDns.setErrorReplyCode(DNSReplyCode::NoError);
    gDns.start(CUM_DNS_PORT, "*", IPAddress(CUM_AP_IP));  // catch-all DNS
    gDnsUp = true;
    installRoutes();
    gServer.begin();
}

void beginUnlock() {
    gMode = Mode::Unlock;
    gPending = Event::None;
    gToken = "";
    gDnsUp = false;            // LAN access by IP; no captive DNS needed
    installRoutes();
    gServer.begin();
}

Event handle() {
    if (gDnsUp) gDns.processNextRequest();
    gServer.handleClient();
    Event e = gPending;
    gPending = Event::None;    // report each completion once
    return e;
}

const String& token() { return gToken; }

void stop() {
    gServer.stop();
    if (gDnsUp) { gDns.stop(); gDnsUp = false; }
    gMode = Mode::Idle;
}

}  // namespace portal
