// ============================================================================
//  Api.h  -  Anthropic rate-limit probe
//
//  Sends a minimal (max_tokens:1) Messages request with the OAuth token and
//  reads the unified rate-limit headers from the response. The body is
//  irrelevant — even an error response carries the rate-limit headers, as long
//  as authentication succeeds.
// ============================================================================
#pragma once

#include <Arduino.h>

namespace api {

struct Usage {
    bool  ok        = false;   // true once both utilization headers were read
    int   httpCode  = 0;       // HTTPClient code (<0 = transport error)
    float util5h    = -1.0f;   // percent 0..100, -1 if header missing
    float util7d    = -1.0f;
    long  reset5h   = 0;       // unix epoch seconds, 0 if header missing
    long  reset7d   = 0;
};

// Perform one probe. Blocks for the duration of the TLS request.
Usage poll(const String& token);

}  // namespace api
