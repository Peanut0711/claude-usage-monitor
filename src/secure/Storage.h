// ============================================================================
//  Storage.h  -  NVS persistence for sealed credential blobs + counters
//
//  Thin wrapper over the Arduino Preferences (NVS) API. Stores only opaque
//  sealed blobs (see Crypto) plus small bookkeeping values; it never sees
//  plaintext credentials or the PIN.
// ============================================================================
#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

namespace storage {

// Create the NVS namespace if it does not exist yet. Call once at boot before
// any read so the first read-only open() doesn't log a spurious NOT_FOUND.
void begin();

// True once a token blob has been written (setup completed at least once).
bool isProvisioned();
void setProvisioned(bool value);

// True if the stored token is sealed under the PIN key (needs an unlock PIN);
// false means it's sealed under the device key and loads without a PIN.
bool tokenPinned();
void setTokenPinned(bool value);

// Sealed-blob get/put. putBlob returns false on NVS write error; getBlob
// returns the number of bytes read (0 if the key is missing or buf too small).
bool   putBlob(const char* key, const uint8_t* data, size_t len);
size_t getBlob(const char* key, uint8_t* buf, size_t bufLen);
size_t blobLen(const char* key);

// Consecutive failed-unlock counter (lockout / wipe policy lives in
// CredentialStore; this is just the persisted integer).
uint8_t pinFails();
void    setPinFails(uint8_t value);

// Number of stored WiFi networks (0..CUM_WIFI_MAX).
uint8_t wifiCount();
void    setWifiCount(uint8_t value);

// Erase every key in the namespace (factory reset back to setup mode).
void wipeAll();

}  // namespace storage
