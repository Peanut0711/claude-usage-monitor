// ============================================================================
//  Storage.cpp  -  Preferences/NVS implementation
// ============================================================================
#include "Storage.h"

#include <Preferences.h>

#include "../config.h"

namespace {

// Open the namespace for the duration of one operation. Preferences keeps an
// NVS handle open between begin()/end(); scoping each call keeps things simple
// and avoids a long-lived handle. `readOnly` skips the read-write commit cost.
struct Scoped {
    Preferences prefs;
    explicit Scoped(bool readOnly) {
        prefs.begin(CUM_NVS_NAMESPACE, readOnly);
    }
    ~Scoped() { prefs.end(); }
};

}  // namespace

namespace storage {

bool isProvisioned() {
    Scoped s(true);
    return s.prefs.getUChar(CUM_NVS_PROVISIONED, 0) != 0;
}

void setProvisioned(bool value) {
    Scoped s(false);
    s.prefs.putUChar(CUM_NVS_PROVISIONED, value ? 1 : 0);
}

bool putBlob(const char* key, const uint8_t* data, size_t len) {
    Scoped s(false);
    return s.prefs.putBytes(key, data, len) == len;
}

size_t getBlob(const char* key, uint8_t* buf, size_t bufLen) {
    Scoped s(true);
    size_t len = s.prefs.getBytesLength(key);
    if (len == 0 || len > bufLen) return 0;
    return s.prefs.getBytes(key, buf, len);
}

size_t blobLen(const char* key) {
    Scoped s(true);
    return s.prefs.getBytesLength(key);
}

uint8_t pinFails() {
    Scoped s(true);
    return s.prefs.getUChar(CUM_NVS_PIN_FAILS, 0);
}

void setPinFails(uint8_t value) {
    Scoped s(false);
    s.prefs.putUChar(CUM_NVS_PIN_FAILS, value);
}

void wipeAll() {
    Scoped s(false);
    s.prefs.clear();
}

}  // namespace storage
