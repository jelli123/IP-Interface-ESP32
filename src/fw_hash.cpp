/*
 *  fw_hash.cpp - Streaming SHA-256 for firmware verification.
 */

#include <cstring>

#include "fw_hash.h"

FwHash::FwHash()
{
    mbedtls_sha256_init(&_ctx);
}

FwHash::~FwHash()
{
    mbedtls_sha256_free(&_ctx);
}

void FwHash::begin()
{
    mbedtls_sha256_free(&_ctx);
    mbedtls_sha256_init(&_ctx);

    // Second argument 0 selects SHA-256 rather than SHA-224.
    if (mbedtls_sha256_starts(&_ctx, 0) == 0)
    {
        _running = true;
        _done    = false;
        memset(_digest, 0, sizeof(_digest));
    }
}

void FwHash::update(const uint8_t* data, size_t length)
{
    if (!_running || data == nullptr || length == 0)
    {
        return;
    }
    mbedtls_sha256_update(&_ctx, data, length);
}

void FwHash::finish()
{
    if (!_running)
    {
        return;
    }
    mbedtls_sha256_finish(&_ctx, _digest);
    _running = false;
    _done    = true;
}

String FwHash::hex() const
{
    if (!_done)
    {
        return String("");
    }

    char buffer[HEX_LENGTH + 1];
    for (size_t i = 0; i < sizeof(_digest); i++)
    {
        snprintf(&buffer[i * 2], 3, "%02x", _digest[i]);
    }
    return String(buffer);
}

bool FwHash::isValidHex(const String& hex)
{
    String value = hex;
    value.trim();

    if (value.length() != HEX_LENGTH)
    {
        return false;
    }
    for (size_t i = 0; i < HEX_LENGTH; i++)
    {
        if (!isxdigit((int)value[i]))
        {
            return false;
        }
    }
    return true;
}

bool FwHash::matches(const String& expectedHex) const
{
    if (!_done || !isValidHex(expectedHex))
    {
        return false;
    }

    String expected = expectedHex;
    expected.trim();
    expected.toLowerCase();

    String actual = hex();

    uint8_t difference = 0;
    for (size_t i = 0; i < HEX_LENGTH; i++)
    {
        difference |= (uint8_t)(expected[i] ^ actual[i]);
    }
    return difference == 0;
}
