// ============================================================================
//  Api.cpp
// ============================================================================
#include "Api.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "../config.h"
#include "anthropic_ca.h"

namespace {

const char* kHeaderKeys[] = {
    "anthropic-ratelimit-unified-5h-utilization",
    "anthropic-ratelimit-unified-5h-reset",
    "anthropic-ratelimit-unified-7d-utilization",
    "anthropic-ratelimit-unified-7d-reset",
    "anthropic-ratelimit-unified-5h-status",       // "allowed" | "limited"
};

static const char* kBody =
    "{\"model\":\"" CUM_PROBE_MODEL "\",\"max_tokens\":1,"
    "\"messages\":[{\"role\":\"user\",\"content\":\".\"}]}";

// Persistent TLS client + HTTP wrapper, kept alive between polls so requests
// after the first reuse the TLS connection instead of repeating the ~1-3 s
// handshake every time (HTTP/1.1 keep-alive). If the server closed the idle
// connection, begin()/POST transparently opens a fresh one. Only ever touched
// from the single poll task, so no locking is needed.
WiFiClientSecure gClient;
HTTPClient       gHttp;
bool             gInit = false;

void apiInit() {
    if (gInit) return;
    // Authenticate the server against the pinned Google Trust Services roots
    // (api.anthropic.com's CA) so an active MITM with a forged cert can't read
    // the OAuth token. See anthropic_ca.h for the bundle / update procedure.
    gClient.setCACert(ANTHROPIC_CA_BUNDLE);
    gHttp.setReuse(true);            // keep-alive: don't drop the connection on end()
    gHttp.setConnectTimeout(15000);  // generous: occasional slow TLS handshakes
    gHttp.setTimeout(15000);
    gHttp.setUserAgent(CUM_API_UA);
    gInit = true;
}

// One request attempt. httpCode > 0 means the server responded.
api::Usage attempt(const String& token) {
    apiInit();
    api::Usage u;

    // Reuses the live connection when gClient is still connected to the host;
    // otherwise opens a fresh one (full TLS handshake).
    if (!gHttp.begin(gClient, CUM_API_URL)) {
        Serial.println("[api] begin() failed");
        return u;
    }
    gHttp.addHeader("Authorization", "Bearer " + token);
    gHttp.addHeader("anthropic-version", CUM_API_VERSION);
    gHttp.addHeader("anthropic-beta", CUM_API_BETA);
    gHttp.addHeader("content-type", "application/json");
    gHttp.collectHeaders(kHeaderKeys, sizeof(kHeaderKeys) / sizeof(kHeaderKeys[0]));

    int code = gHttp.POST((uint8_t*)kBody, strlen(kBody));
    u.httpCode = code;
    Serial.printf("[api] POST -> %d\n", code);

    if (code > 0) {
        String u5 = gHttp.header(kHeaderKeys[0]);
        String r5 = gHttp.header(kHeaderKeys[1]);
        String u7 = gHttp.header(kHeaderKeys[2]);
        String r7 = gHttp.header(kHeaderKeys[3]);
        String s5 = gHttp.header(kHeaderKeys[4]);
        Serial.printf("[api] 5h util='%s' reset='%s' status='%s'\n",
                      u5.c_str(), r5.c_str(), s5.c_str());
        Serial.printf("[api] 7d util='%s' reset='%s'\n", u7.c_str(), r7.c_str());

        // utilization headers are a 0..1 fraction -> percent for the bars.
        if (u5.length()) u.util5h  = u5.toFloat() * 100.0f;
        if (u7.length()) u.util7d  = u7.toFloat() * 100.0f;
        if (r5.length()) u.reset5h = r5.toInt();   // unix epoch seconds
        if (r7.length()) u.reset7d = r7.toInt();
        u.limited = s5.equalsIgnoreCase("limited");
        u.ok = (u.util5h >= 0.0f && u.util7d >= 0.0f);
    }

    gHttp.end();        // with setReuse(true) this keeps the socket open for reuse
    return u;
}

}  // namespace

namespace api {

Usage poll(const String& token) {
    Usage u;
    for (int i = 0; i < 2; i++) {           // retry once on a transport error
        u = attempt(token);
        if (u.httpCode > 0) break;          // server responded (even an error)
        // A reused connection may have gone stale (server closed it, half-open);
        // drop it so the retry does a clean fresh handshake.
        gClient.stop();
        Serial.printf("[api] transport error %d; reconnecting\n", u.httpCode);
        delay(400);
    }
    return u;
}

}  // namespace api
