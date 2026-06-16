// ============================================================================
//  config.h  -  compile-time configuration for the Claude Usage Monitor
//
//  Central place for tunables so the rest of the firmware never hard-codes
//  magic strings/numbers. Stage 2 adds provisioning + secure storage knobs.
// ============================================================================
#pragma once

#include <stdint.h>

// --- SoftAP / provisioning --------------------------------------------------
// AP SSID is CUM_AP_PREFIX + last 2 bytes of the MAC, e.g. "ClaudeMon-3F7A".
#define CUM_AP_PREFIX        "ClaudeMon-"
#define CUM_AP_PASSWORD      "claude1234"   // shown on screen during setup
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

// --- API polling (used in Stage 3) ------------------------------------------
#define CUM_POLL_INTERVAL_MS 60000          // default 60 s

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
