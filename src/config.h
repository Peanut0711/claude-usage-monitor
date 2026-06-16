// ============================================================================
//  config.h  -  compile-time configuration for the Claude Usage Monitor
//
//  Central place for tunables so the rest of the firmware never hard-codes
//  magic strings/numbers. Stage 2 adds provisioning + secure storage knobs.
// ============================================================================
#pragma once

#include <stdint.h>

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

// --- WiFi (station) ---------------------------------------------------------
#define CUM_WIFI_CONNECT_TIMEOUT_MS  20000  // give up + fall back to setup
#define CUM_WIFI_RETRY_MS            500

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
#define CUM_NVS_WIFI_BLOB    "wifi"         // device-key sealed: ssid+pass
#define CUM_NVS_TOKEN_BLOB   "token"        // pin-key sealed: OAuth token
#define CUM_NVS_PIN_FAILS    "pinfails"     // uint8 consecutive failures
