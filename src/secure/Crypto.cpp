// ============================================================================
//  Crypto.cpp  -  mbedtls-backed implementation of the seal/open primitives
// ============================================================================
#include "Crypto.h"

#include <esp_mac.h>
#include <esp_random.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/sha256.h>
#include <string.h>

#include "../config.h"

namespace {

// Factory-default base MAC (6 bytes). Stable per chip; used as a salt so a
// sealed blob only opens on the board that created it.
bool readMac(uint8_t mac[6]) {
    return esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK;
}

}  // namespace

namespace crypto {

bool deriveDeviceKey(uint8_t key[KEY_LEN]) {
    uint8_t mac[6];
    if (!readMac(mac)) return false;

    // SHA-256( "device" | MAC | app-salt ) -> 32-byte key.
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    bool ok = mbedtls_sha256_starts_ret(&ctx, 0) == 0;
    if (ok) {
        static const char kCtx[] = "device";
        ok &= mbedtls_sha256_update_ret(&ctx, (const uint8_t*)kCtx, sizeof(kCtx) - 1) == 0;
        ok &= mbedtls_sha256_update_ret(&ctx, mac, sizeof(mac)) == 0;
        ok &= mbedtls_sha256_update_ret(&ctx, (const uint8_t*)CUM_CRYPTO_SALT,
                                        strlen(CUM_CRYPTO_SALT)) == 0;
        ok &= mbedtls_sha256_finish_ret(&ctx, key) == 0;
    }
    mbedtls_sha256_free(&ctx);
    return ok;
}

bool derivePinKey(const String& pin, uint8_t key[KEY_LEN]) {
    uint8_t mac[6];
    if (!readMac(mac)) return false;

    // Salt = MAC | app-salt. The PIN is the (low-entropy) password, so the
    // PBKDF2 stretch is what makes brute force expensive.
    uint8_t salt[6 + 64];
    size_t saltLen = sizeof(mac);
    memcpy(salt, mac, sizeof(mac));
    size_t appLen = strlen(CUM_CRYPTO_SALT);
    if (appLen > sizeof(salt) - saltLen) appLen = sizeof(salt) - saltLen;
    memcpy(salt + saltLen, CUM_CRYPTO_SALT, appLen);
    saltLen += appLen;

    mbedtls_md_context_t md;
    mbedtls_md_init(&md);
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    bool ok = info && mbedtls_md_setup(&md, info, 1 /*HMAC*/) == 0;
    if (ok) {
        ok = mbedtls_pkcs5_pbkdf2_hmac(&md,
                                       (const uint8_t*)pin.c_str(), pin.length(),
                                       salt, saltLen, CUM_PBKDF2_ROUNDS,
                                       KEY_LEN, key) == 0;
    }
    mbedtls_md_free(&md);
    return ok;
}

bool seal(const uint8_t key[KEY_LEN], const uint8_t* in, size_t len,
          uint8_t* out, size_t* outLen) {
    uint8_t* iv  = out;                 // [0 .. 12)
    uint8_t* tag = out + IV_LEN;         // [12 .. 28)
    uint8_t* ct  = out + OVERHEAD;       // [28 .. 28+len)

    esp_fill_random(iv, IV_LEN);

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    bool ok = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, KEY_LEN * 8) == 0;
    if (ok) {
        ok = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, len,
                                       iv, IV_LEN, nullptr, 0,
                                       in, ct, TAG_LEN, tag) == 0;
    }
    mbedtls_gcm_free(&gcm);
    if (ok && outLen) *outLen = len + OVERHEAD;
    return ok;
}

bool open(const uint8_t key[KEY_LEN], const uint8_t* in, size_t inLen,
          uint8_t* out, size_t* outLen) {
    if (inLen < OVERHEAD) return false;
    const uint8_t* iv  = in;
    const uint8_t* tag = in + IV_LEN;
    const uint8_t* ct  = in + OVERHEAD;
    size_t ctLen = inLen - OVERHEAD;

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    bool ok = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, KEY_LEN * 8) == 0;
    if (ok) {
        ok = mbedtls_gcm_auth_decrypt(&gcm, ctLen, iv, IV_LEN, nullptr, 0,
                                      tag, TAG_LEN, ct, out) == 0;
    }
    mbedtls_gcm_free(&gcm);
    if (ok && outLen) *outLen = ctLen;
    return ok;
}

}  // namespace crypto
