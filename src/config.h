// ============================================================================
//  config.h  -  compile-time configuration for the Claude Usage Monitor
//
//  Central place for tunables so the rest of the firmware never hard-codes
//  magic strings/numbers. Stage 2 adds provisioning + secure storage knobs.
// ============================================================================
#pragma once

#include <stdint.h>

// Diagnostic: boot straight into a touch-coordinate test screen (no WiFi/unlock)
// to inspect touch coordinates. Set to 1 to enable; 0 = normal boot.
#define CUM_TOUCH_TEST 0

// Diagnostic: log the measured count-up frame rate to serial. Set to 0 to mute.
#define CUM_FPS_DEBUG 0

// Local, git-ignored secrets (AP password). Falls back to a placeholder so the
// project still builds without it — but change it before trusting setup mode.
#if __has_include("secrets.h")
#include "secrets.h"
#endif

// --- SoftAP / provisioning --------------------------------------------------
// AP SSID is CUM_AP_PREFIX + last 2 bytes of the MAC, e.g. "ClaudeMon-3F7A".
#define CUM_AP_PREFIX        "ClaudeMon-"
#ifndef CUM_AP_PASSWORD
#define CUM_AP_PASSWORD      "change-me-strong-pw"  // override in secrets.h
#endif
#define CUM_PORTAL_PORT      80
#define CUM_DNS_PORT         53
#define CUM_AP_IP            192, 168, 4, 1 // captive-portal landing IP
#define CUM_SCAN_MAX         16             // SSIDs shown in the setup dropdown

// --- WiFi (station) ---------------------------------------------------------
#define CUM_WIFI_CONNECT_TIMEOUT_MS  20000  // give up + fall back to setup
#define CUM_WIFI_RETRY_MS            500
// Whole-sequence connect attempts before falling back to setup mode. A single
// post-boot scan can transiently return 0 networks (radio still settling), so
// retry the scan a couple of times before assuming the known APs are absent.
#define CUM_WIFI_CONNECT_RETRIES     3
// Runtime roaming: consecutive failed polls (each nudging a cheap same-AP
// reconnect, for a brief blip) before assuming we've physically moved
// (home<->office) and doing a full rescan to roam onto the strongest saved
// network. Kept small so a real location change recovers within ~1 min while
// the cheap reconnect still absorbs short blips without scanning.
#define CUM_REROAM_AFTER             3
// Pre-commit verify (setup portal): how long to wait for the STA link to come
// up while validating the entered WiFi + token. Kept well under the browser's
// POST timeout so the result page still reaches the phone.
#define CUM_VERIFY_WIFI_TIMEOUT_MS   12000

// --- Credentials / PIN ------------------------------------------------------
#define CUM_PIN_LEN          4              // exact digits required
#define CUM_MAX_PIN_FAILS    10             // wipe everything after this many
#define CUM_TOKEN_MAX_LEN    1024           // OAuth token upper bound (bytes)
#define CUM_SSID_MAX_LEN     32
#define CUM_PASS_MAX_LEN     64

// --- Anthropic API polling (Stage 3) ----------------------------------------
#define CUM_POLL_INTERVAL_MS 60000          // default 60 s (ref: 30-300 s)
#define CUM_API_URL          "https://api.anthropic.com/v1/messages"
#define CUM_API_VERSION      "2023-06-01"   // anthropic-version
#define CUM_API_BETA         "oauth-2025-04-20"  // required for OAuth tokens
#define CUM_API_UA           "claude-code/2.1.5" // OAuth tokens expect this UA
#define CUM_PROBE_MODEL      "claude-haiku-4-5-20251001"
#define CUM_NTP_SERVER       "pool.ntp.org"  // for reset countdowns

// --- Crypto -----------------------------------------------------------------
// App-specific salt mixed into both key derivations. Changing it invalidates
// any previously stored credentials (they will fail to decrypt -> re-setup).
#define CUM_CRYPTO_SALT      "claude-usage-monitor/v1"
#define CUM_PBKDF2_ROUNDS    10000          // PIN-key stretch (SHA-256 HMAC)

// --- NVS layout -------------------------------------------------------------
#define CUM_NVS_NAMESPACE    "cum"
#define CUM_NVS_PROVISIONED  "prov"         // uint8 flag
#define CUM_NVS_WIFI_PREFIX  "wifi"         // device-key sealed blobs: wifi0..N
#define CUM_NVS_WIFI_COUNT   "wifin"        // uint8 number of stored networks
#define CUM_NVS_TOKEN_BLOB   "token"        // sealed OAuth token (PIN or device key)
#define CUM_NVS_PIN_FAILS    "pinfails"     // uint8 consecutive failures
#define CUM_NVS_TOKEN_PINNED "tokpin"       // uint8: 1 = token sealed under PIN key
#define CUM_NVS_LAST_WIFI    "lastwifi"     // uint8 index of last-connected network (0xFF=none)

// Max WiFi networks remembered for auto-connect (e.g. home + office).
#define CUM_WIFI_MAX         3
