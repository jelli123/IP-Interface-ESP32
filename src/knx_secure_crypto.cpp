/*
 *  knx_secure_crypto.cpp - The cryptography of KNX Data Secure and KNXnet/IP
 *  Secure.
 */

#include "knx_secure_crypto.h"

#include <stdio.h>
#include <string.h>

#ifndef KNXSEC_HOST_TEST
#include <bootloader_random.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/ecp.h>
#include <mbedtls/sha256.h>
#endif

namespace knxsec
{

// --------------------------------------------------------------------------
// AES, CBC-MAC, CTR
// --------------------------------------------------------------------------

#ifdef KNXSEC_HOST_TEST

Aes::Aes(const uint8_t key[KEY_LEN])
{
    AES_init_ctx(&_ctx, key);
}

Aes::~Aes()
{
    wipe(&_ctx, sizeof(_ctx));
}

void Aes::encrypt(const uint8_t in[BLOCK_LEN], uint8_t out[BLOCK_LEN]) const
{
    memcpy(out, in, BLOCK_LEN);
    AES_ECB_encrypt(&_ctx, out);
}

#else

Aes::Aes(const uint8_t key[KEY_LEN])
{
    mbedtls_aes_init(&_ctx);
    mbedtls_aes_setkey_enc(&_ctx, key, 128);
}

Aes::~Aes()
{
    mbedtls_aes_free(&_ctx);
}

void Aes::encrypt(const uint8_t in[BLOCK_LEN], uint8_t out[BLOCK_LEN]) const
{
    mbedtls_aes_crypt_ecb(&_ctx, MBEDTLS_AES_ENCRYPT, in, out);
}

#endif

namespace
{

/*
 * Feeds octets into a running CBC-MAC one at a time and encrypts whenever a
 * block is full. Keeps the three parts of the input apart without first
 * copying them into one padded buffer - a KNXnet/IP frame can be 500 octets,
 * and this runs on the main task's stack.
 */
class CbcMac
{
public:
    explicit CbcMac(const uint8_t key[KEY_LEN]) : _aes(key) {}

    void add(const uint8_t* data, size_t length)
    {
        for (size_t i = 0; i < length; i++)
        {
            _state[_fill++] ^= data[i];

            if (_fill == BLOCK_LEN)
            {
                _aes.encrypt(_state, _state);
                _fill = 0;
            }
        }
    }

    // Zero padding: the missing octets XOR in as zero, so only a partly
    // filled block needs one more encryption.
    void finish(uint8_t mac[BLOCK_LEN])
    {
        if (_fill != 0)
        {
            _aes.encrypt(_state, _state);
            _fill = 0;
        }
        memcpy(mac, _state, BLOCK_LEN);
        wipe(_state, sizeof(_state));
    }

private:
    Aes     _aes;
    uint8_t _state[BLOCK_LEN] = {0};
    size_t  _fill = 0;
};

void increment(uint8_t counter[BLOCK_LEN])
{
    for (int i = BLOCK_LEN - 1; i >= 0; i--)
    {
        if (++counter[i] != 0)
            break;
    }
}

} // namespace

void cbcMac(const uint8_t key[KEY_LEN], const uint8_t b0[BLOCK_LEN],
            const uint8_t* ad, size_t adLength,
            const uint8_t* payload, size_t payloadLength,
            uint8_t mac[BLOCK_LEN])
{
    CbcMac cbc(key);
    const uint8_t adLen[2] = {(uint8_t)(adLength >> 8), (uint8_t)adLength};

    cbc.add(b0, BLOCK_LEN);
    cbc.add(adLen, sizeof(adLen));
    cbc.add(ad, adLength);
    cbc.add(payload, payloadLength);
    cbc.finish(mac);
}

void ctrCrypt(const uint8_t key[KEY_LEN], const uint8_t ctr0[BLOCK_LEN],
              uint8_t* mac, size_t macLength, uint8_t* data, size_t dataLength)
{
    Aes     aes(key);
    uint8_t counter[BLOCK_LEN];
    uint8_t stream[BLOCK_LEN];
    size_t  used = BLOCK_LEN; // forces a fresh block on the first octet

    memcpy(counter, ctr0, BLOCK_LEN);

    auto next = [&]() -> uint8_t {
        if (used == BLOCK_LEN)
        {
            aes.encrypt(counter, stream);
            increment(counter);
            used = 0;
        }
        return stream[used++];
    };

    for (size_t i = 0; i < macLength; i++)
        mac[i] ^= next();

    for (size_t i = 0; i < dataLength; i++)
        data[i] ^= next();

    wipe(stream, sizeof(stream));
}

bool equal(const uint8_t* a, const uint8_t* b, size_t length)
{
    uint8_t difference = 0;

    for (size_t i = 0; i < length; i++)
        difference |= (uint8_t)(a[i] ^ b[i]);

    return difference == 0;
}

void wipe(void* data, size_t length)
{
    // volatile keeps the compiler from dropping a store to memory that is
    // about to go out of scope.
    volatile uint8_t* p = (volatile uint8_t*)data;

    while (length--)
        *p++ = 0;
}

// --------------------------------------------------------------------------
// SHA-256, random numbers, X25519 - target only
// --------------------------------------------------------------------------

#ifndef KNXSEC_HOST_TEST

void sha256(const uint8_t* data, size_t length, uint8_t out[32])
{
    // Last argument 0 selects SHA-256 rather than SHA-224.
    mbedtls_sha256(data, length, out, 0);
}

namespace
{

mbedtls_ctr_drbg_context drbg;
SemaphoreHandle_t        drbgLock = nullptr;
bool                     drbgReady = false;

int hardwareEntropy(void*, unsigned char* out, size_t length)
{
    esp_fill_random(out, length);
    return 0;
}

int drbgCallback(void*, unsigned char* out, size_t length)
{
    random(out, length);
    return 0;
}

} // namespace

void beginRandom()
{
    if (drbgReady)
        return;

    drbgLock = xSemaphoreCreateMutex();
    mbedtls_ctr_drbg_init(&drbg);

    /*
     * The SAR ADC adds thermal noise to the hardware RNG while it is enabled.
     * Without the radio running that is the only real entropy the chip has,
     * so the seed is drawn here and the source switched off again before
     * WiFi or the ADC are touched by anyone else.
     */
    bootloader_random_enable();

    static const char personal[] = "sbip knx secure";
    int rc = mbedtls_ctr_drbg_seed(&drbg, hardwareEntropy, nullptr,
                                   (const unsigned char*)personal, sizeof(personal) - 1);

    bootloader_random_disable();

    drbgReady = (rc == 0);
}

void random(uint8_t* out, size_t length)
{
    if (!drbgReady)
        beginRandom();

    // Whatever the radio contributes by now goes in as additional input.
    uint32_t fresh[4];
    esp_fill_random(fresh, sizeof(fresh));

    if (drbgReady && drbgLock != nullptr &&
        xSemaphoreTake(drbgLock, portMAX_DELAY) == pdTRUE)
    {
        mbedtls_ctr_drbg_random_with_add(&drbg, out, length,
                                         (const unsigned char*)fresh, sizeof(fresh));
        xSemaphoreGive(drbgLock);
    }
    else
    {
        esp_fill_random(out, length);
    }

    wipe(fresh, sizeof(fresh));
}

namespace
{

/*
 * RFC 7748 clamping. mbedTLS insists on a clamped scalar (ecp_check_privkey
 * refuses anything else); X25519 clamps implicitly, so doing it here keeps
 * the result identical for any 32 octets, test vectors included.
 */
void clamp(uint8_t k[X25519_LEN])
{
    k[0] &= 248;
    k[31] &= 127;
    k[31] |= 64;
}

} // namespace

bool x25519(const uint8_t priv[X25519_LEN], const uint8_t peer[X25519_LEN],
            uint8_t shared[X25519_LEN])
{
    mbedtls_ecp_group grp;
    mbedtls_ecp_point q;
    mbedtls_mpi       d;
    mbedtls_mpi       z;

    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&q);
    mbedtls_mpi_init(&d);
    mbedtls_mpi_init(&z);

    uint8_t scalar[X25519_LEN];
    memcpy(scalar, priv, X25519_LEN);
    clamp(scalar);

    // Montgomery curves use little-endian encoding throughout, and
    // point_read_binary clears the top bit of u as RFC 7748 asks.
    bool ok = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_CURVE25519) == 0 &&
              mbedtls_mpi_read_binary_le(&d, scalar, X25519_LEN) == 0 &&
              mbedtls_ecp_point_read_binary(&grp, &q, peer, X25519_LEN) == 0 &&
              mbedtls_ecdh_compute_shared(&grp, &z, &q, &d, drbgCallback, nullptr) == 0 &&
              mbedtls_mpi_write_binary_le(&z, shared, X25519_LEN) == 0;

    wipe(scalar, sizeof(scalar));
    mbedtls_mpi_free(&z);
    mbedtls_mpi_free(&d);
    mbedtls_ecp_point_free(&q);
    mbedtls_ecp_group_free(&grp);

    // An all-zero result means the peer sent a low order point.
    static const uint8_t zero[X25519_LEN] = {0};
    return ok && !equal(shared, zero, X25519_LEN);
}

bool x25519Keypair(uint8_t priv[X25519_LEN], uint8_t pub[X25519_LEN])
{
    // The public key is X25519(priv, 9), the base point.
    static const uint8_t basePoint[X25519_LEN] = {9};

    random(priv, X25519_LEN);
    clamp(priv);

    return x25519(priv, basePoint, pub);
}

// --------------------------------------------------------------------------
// Self test
// --------------------------------------------------------------------------

namespace
{

bool fromHex(const char* hex, uint8_t* out, size_t length)
{
    for (size_t i = 0; i < length; i++)
    {
        unsigned value = 0;
        if (sscanf(hex + 2 * i, "%2x", &value) != 1)
            return false;
        out[i] = (uint8_t)value;
    }
    return true;
}

bool matchesHex(const uint8_t* data, const char* hex, size_t length)
{
    uint8_t expected[64];
    return length <= sizeof(expected) && fromHex(hex, expected, length) &&
           equal(data, expected, length);
}

} // namespace

bool selfTest(char* report, size_t reportLength)
{
    // RFC 7748 section 6.1: Alice's key pair and the shared secret with Bob.
    uint8_t alice[32], bobPublic[32], out[32];
    fromHex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", alice, 32);
    fromHex("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f", bobPublic, 32);

    static const uint8_t basePoint[32] = {9};
    bool x25519Ok =
        x25519(alice, basePoint, out) &&
        matchesHex(out, "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a", 32) &&
        x25519(alice, bobPublic, out) &&
        matchesHex(out, "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742", 32);

    // 03_08_09 Annex A.5: a ROUTING_INDICATION in a SECURE_WRAPPER.
    uint8_t key[16], b0[16], ctr0[16], ad[8], payload[17], mac[16];
    fromHex("000102030405060708090a0b0c0d0e0f", key, 16);
    fromHex("c0c1c2c3c4c500fa12345678affe0011", b0, 16);
    fromHex("c0c1c2c3c4c500fa12345678affeff00", ctr0, 16);
    fromHex("0610095000370000", ad, 8);
    fromHex("0610053000112900bcd011590ade010081", payload, 17);

    cbcMac(key, b0, ad, sizeof(ad), payload, sizeof(payload), mac);
    bool macOk = matchesHex(mac, "bd0a294b952554b23539204c2271d26b", 16);

    ctrCrypt(key, ctr0, mac, sizeof(mac), payload, sizeof(payload));
    bool ctrOk = matchesHex(payload, "b7ee7e8a1c2f7bbabec775fd6e10d0bc4b", 17) &&
                 matchesHex(mac, "7212a03aaae49da85689774c1d2b4da4", 16);

    // 03_08_09 Annex A.2: the server side of the example session - this
    // device's side. Public key from the private one, shared secret with the
    // client's public key, session key from that.
    uint8_t serverPrivate[32], clientPublic[32], digest[32];
    fromHex("68c1744813f4e65cf10cca671caa1336a796b4ac40cc5cf2655674225c1e5264", serverPrivate, 32);
    fromHex("0aa227b4fd7a32319ba9960ac036ce0e5c4507b5ae55161f1078b1dcfb3cb631", clientPublic, 32);
    bool sessionOk =
        x25519(serverPrivate, basePoint, out) &&
        matchesHex(out, "bdf099909923143ef0a5de0b3be3687bc5bd3cf5f9e6f901699cd870ec1ff824", 32) &&
        x25519(serverPrivate, clientPublic, out) &&
        matchesHex(out, "d801525217618f0da90a4ff22148aee0ff4c19b430e8081223ffe99c81a98b05", 32);
    sha256(out, 32, digest);
    sessionOk = sessionOk && matchesHex(digest, "289426c2912535ba98279a4d1843c487", 16);

    bool ok = x25519Ok && macOk && ctrOk && sessionOk;

    snprintf(report, reportLength, "%s (X25519 %s, CBC-MAC %s, CTR %s, session key %s)",
             ok ? "OK" : "FAILED", x25519Ok ? "ok" : "FAIL", macOk ? "ok" : "FAIL",
             ctrOk ? "ok" : "FAIL", sessionOk ? "ok" : "FAIL");
    return ok;
}

#endif // KNXSEC_HOST_TEST

} // namespace knxsec
