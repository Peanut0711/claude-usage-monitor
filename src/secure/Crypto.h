// ============================================================================
//  Crypto.h  -  AES-256-GCM seal/open + key derivation (mbedtls, on-chip)
//
//  Two keys are derived, both salted with the device MAC so a sealed blob is
//  bound to this physical board:
//    - device key : fast hash of MAC + app salt. No secret input, so it only
//                   obfuscates (real at-rest protection needs flash encryption
//                   enabled in eFuse). Used for the WiFi credentials so the
//                   board can auto-connect on boot.
//    - PIN key    : PBKDF2-HMAC-SHA256 (CUM_PBKDF2_ROUNDS) over PIN + MAC salt.
//                   Used for the OAuth token. The PIN is never stored; a wrong
//                   PIN simply fails the GCM auth tag on open().
//
//  Blob format produced by seal() / consumed by open():
//      [ iv(12) | tag(16) | ciphertext(N) ]   total = N + 28
// ============================================================================
#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

namespace crypto {

constexpr size_t KEY_LEN = 32;   // AES-256
constexpr size_t IV_LEN  = 12;   // GCM nonce
constexpr size_t TAG_LEN = 16;   // GCM auth tag
constexpr size_t OVERHEAD = IV_LEN + TAG_LEN;  // 28 bytes added by seal()

// Derive the device-bound key (no PIN). Returns false on internal error.
bool deriveDeviceKey(uint8_t key[KEY_LEN]);

// Derive the PIN-bound key. Slow by design (PBKDF2 stretch).
bool derivePinKey(const String& pin, uint8_t key[KEY_LEN]);

// Seal `in` (len bytes) into `out`. Caller must provide out >= len + OVERHEAD.
// On success writes *outLen = len + OVERHEAD and returns true.
bool seal(const uint8_t key[KEY_LEN], const uint8_t* in, size_t len,
          uint8_t* out, size_t* outLen);

// Open a sealed blob. Caller must provide out >= inLen - OVERHEAD.
// Returns false if inLen < OVERHEAD or the auth tag fails (wrong key/PIN or
// tampered data). On success writes *outLen = inLen - OVERHEAD.
bool open(const uint8_t key[KEY_LEN], const uint8_t* in, size_t inLen,
          uint8_t* out, size_t* outLen);

}  // namespace crypto
