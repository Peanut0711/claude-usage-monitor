// ============================================================================
//  CredentialStore.cpp
// ============================================================================
#include "CredentialStore.h"

#include <string.h>

#include "../config.h"
#include "Crypto.h"
#include "Storage.h"

namespace {

// WiFi blob plaintext layout: [ uint8 ssidLen | ssid bytes | pass bytes ].
// Upper bound: 1 + SSID_MAX + PASS_MAX.
constexpr size_t WIFI_PT_MAX = 1 + CUM_SSID_MAX_LEN + CUM_PASS_MAX_LEN;

bool sealAndStore(const char* key, const uint8_t k[crypto::KEY_LEN],
                  const uint8_t* pt, size_t ptLen) {
    uint8_t blob[crypto::OVERHEAD + WIFI_PT_MAX];
    if (ptLen + crypto::OVERHEAD > sizeof(blob)) return false;  // guard
    size_t blobLen = 0;
    if (!crypto::seal(k, pt, ptLen, blob, &blobLen)) return false;
    bool ok = storage::putBlob(key, blob, blobLen);
    memset(blob, 0, sizeof(blob));
    return ok;
}

}  // namespace

namespace credentials {

void begin() { storage::begin(); }

bool isProvisioned() { return storage::isProvisioned(); }
uint8_t failCount()  { return storage::pinFails(); }

uint8_t failsRemaining() {
    uint8_t f = storage::pinFails();
    return f >= CUM_MAX_PIN_FAILS ? 0 : (CUM_MAX_PIN_FAILS - f);
}

bool provision(const String& ssid, const String& pass,
               const String& token, const String& pin) {
    if (ssid.length() == 0 || ssid.length() > CUM_SSID_MAX_LEN) return false;
    if (pass.length() > CUM_PASS_MAX_LEN) return false;
    if (token.length() == 0 || token.length() > CUM_TOKEN_MAX_LEN) return false;
    if (pin.length() != CUM_PIN_LEN) return false;

    uint8_t devKey[crypto::KEY_LEN];
    uint8_t pinKey[crypto::KEY_LEN];
    bool ok = crypto::deriveDeviceKey(devKey) && crypto::derivePinKey(pin, pinKey);

    if (ok) {
        // Pack WiFi plaintext.
        uint8_t wifiPt[WIFI_PT_MAX];
        wifiPt[0] = (uint8_t)ssid.length();
        memcpy(wifiPt + 1, ssid.c_str(), ssid.length());
        memcpy(wifiPt + 1 + ssid.length(), pass.c_str(), pass.length());
        size_t wifiLen = 1 + ssid.length() + pass.length();

        ok = sealAndStore(CUM_NVS_WIFI_BLOB, devKey, wifiPt, wifiLen) &&
             sealAndStore(CUM_NVS_TOKEN_BLOB, pinKey,
                          (const uint8_t*)token.c_str(), token.length());
        memset(wifiPt, 0, sizeof(wifiPt));
    }

    if (ok) {
        storage::setPinFails(0);
        storage::setProvisioned(true);
    }
    memset(devKey, 0, sizeof(devKey));
    memset(pinKey, 0, sizeof(pinKey));
    return ok;
}

bool loadWifi(String& ssidOut, String& passOut) {
    uint8_t blob[crypto::OVERHEAD + WIFI_PT_MAX];
    size_t blobLen = storage::getBlob(CUM_NVS_WIFI_BLOB, blob, sizeof(blob));
    if (blobLen == 0) return false;

    uint8_t devKey[crypto::KEY_LEN];
    if (!crypto::deriveDeviceKey(devKey)) return false;

    uint8_t pt[WIFI_PT_MAX];
    size_t ptLen = 0;
    bool ok = crypto::open(devKey, blob, blobLen, pt, &ptLen);
    memset(devKey, 0, sizeof(devKey));

    if (ok && ptLen >= 1) {
        uint8_t ssidLen = pt[0];
        if (1 + ssidLen <= ptLen && ssidLen <= CUM_SSID_MAX_LEN) {
            ssidOut = String((const char*)(pt + 1), ssidLen);
            passOut = String((const char*)(pt + 1 + ssidLen), ptLen - 1 - ssidLen);
        } else {
            ok = false;
        }
    } else {
        ok = false;
    }
    memset(pt, 0, sizeof(pt));
    return ok;
}

UnlockResult unlock(const String& pin, String& tokenOut) {
    if (storage::pinFails() >= CUM_MAX_PIN_FAILS) return UnlockResult::LockedOut;
    if (pin.length() != CUM_PIN_LEN) return UnlockResult::WrongPin;

    uint8_t blob[crypto::OVERHEAD + CUM_TOKEN_MAX_LEN];
    size_t blobLen = storage::getBlob(CUM_NVS_TOKEN_BLOB, blob, sizeof(blob));
    if (blobLen == 0) return UnlockResult::Error;

    uint8_t pinKey[crypto::KEY_LEN];
    if (!crypto::derivePinKey(pin, pinKey)) return UnlockResult::Error;

    uint8_t pt[CUM_TOKEN_MAX_LEN];
    size_t ptLen = 0;
    bool ok = crypto::open(pinKey, blob, blobLen, pt, &ptLen);
    memset(pinKey, 0, sizeof(pinKey));
    memset(blob, 0, sizeof(blob));

    if (ok) {
        tokenOut = String((const char*)pt, ptLen);
        memset(pt, 0, sizeof(pt));
        storage::setPinFails(0);
        return UnlockResult::Ok;
    }
    memset(pt, 0, sizeof(pt));

    // Wrong PIN: bump the counter and enforce the wipe policy.
    uint8_t fails = storage::pinFails() + 1;
    storage::setPinFails(fails);
    if (fails >= CUM_MAX_PIN_FAILS) {
        wipe();
        return UnlockResult::LockedOut;
    }
    return UnlockResult::WrongPin;
}

void wipe() {
    storage::wipeAll();
}

}  // namespace credentials
