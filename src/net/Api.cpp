// ============================================================================
//  Api.cpp
// ============================================================================
#include "Api.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "../config.h"

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

// One request attempt. httpCode > 0 means the server responded.
api::Usage attempt(const String& token) {
    api::Usage u;

    WiFiClientSecure client;
    // TODO(hardening): pin the api.anthropic.com CA instead of skipping
    // validation. setInsecure() still encrypts but does not authenticate the
    // server, so an active MITM with a forged cert could read the token.
    client.setInsecure();

    HTTPClient http;
    if (!http.begin(client, CUM_API_URL)) {
        Serial.println("[api] begin() failed");
        return u;
    }
    http.setConnectTimeout(15000);   // generous: occasional slow TLS handshakes
    http.setTimeout(15000);
    http.setUserAgent(CUM_API_UA);
    http.addHeader("Authorization", "Bearer " + token);
    http.addHeader("anthropic-version", CUM_API_VERSION);
    http.addHeader("anthropic-beta", CUM_API_BETA);
    http.addHeader("content-type", "application/json");
    http.collectHeaders(kHeaderKeys, sizeof(kHeaderKeys) / sizeof(kHeaderKeys[0]));

    int code = http.POST((uint8_t*)kBody, strlen(kBody));
    u.httpCode = code;
    Serial.printf("[api] POST -> %d\n", code);

    if (code > 0) {
        String u5 = http.header(kHeaderKeys[0]);
        String r5 = http.header(kHeaderKeys[1]);
        String u7 = http.header(kHeaderKeys[2]);
        String r7 = http.header(kHeaderKeys[3]);
        String s5 = http.header(kHeaderKeys[4]);
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

    http.end();
    return u;
}

}  // namespace

namespace api {

Usage poll(const String& token) {
    Usage u;
    for (int i = 0; i < 2; i++) {           // retry once on a transport error
        u = attempt(token);
        if (u.httpCode > 0) break;          // server responded (even an error)
        Serial.printf("[api] transport error %d; retrying\n", u.httpCode);
        delay(400);
    }
    return u;
}

}  // namespace api
