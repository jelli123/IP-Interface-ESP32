/*
 *  fw_hash.h - Streaming SHA-256 for firmware verification.
 *
 *  Arduino's Update class only knows MD5 (Update::setMD5), so the digest is
 *  computed here alongside the write and checked before the boot partition is
 *  switched.
 *
 *  mbedtls routes SHA-256 through the ESP32 hardware accelerator whenever
 *  CONFIG_MBEDTLS_HARDWARE_SHA is enabled, which it is in the stock Arduino
 *  builds. Hashing therefore costs no measurable time next to the flash write.
 */
#pragma once

#include <Arduino.h>
#include <mbedtls/sha256.h>
#include <stdint.h>

class FwHash
{
public:
    /** Length of a SHA-256 digest as lower case hex, without the terminator. */
    static const size_t HEX_LENGTH = 64;

    FwHash();
    ~FwHash();

    /** Discard any state and start a fresh digest. */
    void begin();

    /** Feed the next chunk of firmware. */
    void update(const uint8_t* data, size_t length);

    /** Close the digest. Further update() calls are ignored. */
    void finish();

    /** @return the digest as lower case hex, empty before finish() */
    String hex() const;

    /**
     * Constant time comparison against an expected hex digest.
     *
     * Case insensitive, tolerates surrounding whitespace. Not a secret, but
     * comparing without an early exit costs nothing and avoids the habit of
     * writing timing-dependent comparisons.
     *
     * @param expectedHex 64 hex characters
     * @return true if the digest matches
     */
    bool matches(const String& expectedHex) const;

    /** @return true if the string is a syntactically valid SHA-256 hex digest */
    static bool isValidHex(const String& hex);

private:
    mbedtls_sha256_context _ctx;
    uint8_t                _digest[32] = {0};
    bool                   _running = false;
    bool                   _done    = false;
};
