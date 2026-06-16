// ============================================================================
//  Portal.cpp
// ============================================================================
#include "Portal.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

#include "../config.h"
#include "../secure/CredentialStore.h"

namespace {

enum class Mode { Idle, Setup, Unlock };

Mode       gMode = Mode::Idle;
WebServer  gServer(CUM_PORTAL_PORT);
DNSServer  gDns;
bool       gDnsUp = false;

// Latched flow result, consumed by handle().
portal::Event gPending = portal::Event::None;
String        gToken;

// --- Pages (PROGMEM) --------------------------------------------------------
const char SETUP_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Claude Usage Monitor</title><style>
body{font-family:system-ui,sans-serif;background:#0e1016;color:#ececec;margin:0;padding:24px}
h1{color:#d97757;font-size:20px}label{display:block;margin:14px 0 4px;color:#c8ccd0}
input,textarea{width:100%;box-sizing:border-box;padding:10px;border-radius:8px;
border:1px solid #3a404a;background:#1a1d24;color:#ececec;font-size:16px}
textarea{height:90px}button{margin-top:20px;width:100%;padding:14px;border:0;
border-radius:8px;background:#d97757;color:#fff;font-size:16px;font-weight:600}
small{color:#8a9099}</style></head><body>
<h1>Claude Usage Monitor</h1>
<form method="POST" action="/save">
<label>WiFi name (SSID)</label><input name="ssid" maxlength="32" required>
<label>WiFi password</label><input name="pass" type="password" maxlength="64">
<label>OAuth token</label>
<textarea name="token" placeholder="run: claude setup-token"></textarea>
<label>PIN (4 digits)</label>
<input name="pin" inputmode="numeric" pattern="[0-9]{4}" maxlength="4">
<small>The PIN encrypts your token and is never stored.<br>
Adding/changing WiFi? Leave token &amp; PIN blank: your token is kept and the
network is remembered (up to 3, e.g. home + office).</small>
<button type="submit">Save &amp; reboot</button></form></body></html>)HTML";

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
void handleSetupRoot() { sendProgmem(SETUP_HTML); }

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
        gServer.send(400, "text/plain", err + "\nGo back and fix this field.");
        return;
    }

    bool ok = wifiOnly ? credentials::addWifi(ssid, pass)
                       : credentials::provision(ssid, pass, token, pin);
    if (ok) {
        sendProgmem(SAVED_HTML);
        gPending = portal::Event::Provisioned;   // main reboots
    } else {
        gServer.send(500, "text/plain", "Storage/crypto error while saving.");
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
        // Catch-all so any captive-portal probe lands on the form.
        gServer.onNotFound(handleSetupRoot);
    } else {
        gServer.on("/", HTTP_GET, handleUnlockRoot);
        gServer.on("/unlock", HTTP_POST, handleUnlock);
        gServer.onNotFound(handleUnlockRoot);
    }
}

}  // namespace

namespace portal {

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
