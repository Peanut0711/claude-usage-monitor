// ============================================================================
//  CredentialStore.h  -  the firmware's view of stored secrets
//
//  Combines Crypto (sealing) + Storage (NVS). Everything above this layer
//  works with plain Strings and never touches mbedtls or NVS directly.
//
//  Security model (see Crypto.h for the key details):
//    - WiFi SSID/pass : sealed under the device key  -> auto-loads on boot.
//    - OAuth token    : sealed under the PIN key      -> needs unlock(pin).
//  The PIN is never persisted. Ten consecutive wrong PINs wipe everything.
// ============================================================================
#pragma once

#include <Arduino.h>

namespace credentials {

// Initialise the backing store. Call once at boot before any other call.
void begin();

// --- Provisioning (called once from the captive portal) ---------------------
// Seals WiFi creds under the device key. The token is sealed under the PIN key
// when `pin` is the required length, or under the device key when `pin` is empty
// (no unlock screen — simpler, weaker at rest). Marks the device provisioned.
// Returns false on any crypto/NVS failure (or a non-empty pin of wrong length).
bool provision(const String& ssid, const String& pass,
               const String& token, const String& pin);

// Add (or update) a WiFi network, keeping the stored token and PIN. Used for
// remembering extra networks (e.g. home + office) without re-entering the
// token. Updates in place if the SSID already exists; evicts the oldest once
// CUM_WIFI_MAX is reached. Returns false if not provisioned or on error.
bool addWifi(const String& ssid, const String& pass);

bool isProvisioned();

// --- WiFi (available without the PIN) ---------------------------------------
// Decrypt all stored networks into the given arrays (size >= maxN). Returns
// how many were loaded.
int loadWifiList(String ssids[], String passwords[], int maxN);

// --- Token load -------------------------------------------------------------
// True if the stored token is PIN-encrypted (an unlock PIN must be entered).
// When false, the token loads with loadToken() and no unlock screen is shown.
bool tokenNeedsPin();

// Decrypt the device-key-sealed token (the no-PIN case). Returns false if the
// token is PIN-encrypted, missing, or corrupt.
bool loadToken(String& tokenOut);

enum class UnlockResult { Ok, WrongPin, LockedOut, Error };

// Attempts to decrypt the token with `pin`. On success resets the fail
// counter and returns the token via `tokenOut`. On a wrong PIN, increments the
// counter and — once CUM_MAX_PIN_FAILS is reached — wipes all credentials
// (result becomes LockedOut and the device must be re-provisioned).
UnlockResult unlock(const String& pin, String& tokenOut);

uint8_t failCount();          // consecutive wrong-PIN attempts so far
uint8_t failsRemaining();     // attempts left before wipe

// --- Reset ------------------------------------------------------------------
void wipe();                  // factory reset -> back to setup mode

}  // namespace credentials
