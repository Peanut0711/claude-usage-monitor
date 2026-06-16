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
// Seals WiFi creds under the device key and the token under the PIN key, then
// marks the device provisioned. Returns false on any crypto/NVS failure.
bool provision(const String& ssid, const String& pass,
               const String& token, const String& pin);

bool isProvisioned();

// --- WiFi (available without the PIN) ---------------------------------------
// Loads + decrypts the stored WiFi creds. Returns false if missing/corrupt.
bool loadWifi(String& ssidOut, String& passOut);

// --- Token unlock -----------------------------------------------------------
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
