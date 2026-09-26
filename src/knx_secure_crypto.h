/*
 *  knx_secure_crypto.h - The cryptography of KNX Data Secure and KNXnet/IP
 *  Secure.
 *
 *  Both use AES-128 in CCM mode, but not through a CCM API: the KNX
 *  specification writes the two halves out separately, and so does every
 *  implementation that talks to real devices. A CBC-MAC over
 *
 *      B0 || length(additional data) || additional data || payload
 *
 *  zero padded to whole blocks and chained from an all-zero IV, and one AES-CTR
 *  keystream starting at counter block Ctr0 that first covers the MAC and then
 *  the payload. Data Secure truncates the MAC to four octets before the CTR
 *  stage, so its payload starts at octet 4 of the first keystream block;
 *  KNXnet/IP Secure keeps all sixteen. ctrCrypt() takes the MAC length for
 *  exactly that reason.
 *
 *  AES goes through mbedTLS, which uses the ESP32's AES accelerator. X25519 and
 *  SHA-256 as well. Random numbers come from a CTR-DRBG seeded with hardware
 *  entropy, see random().
 *
 *  The same file builds on the host for the unit test in test/, with the
 *  stack's portable AES in place of mbedTLS (KNXSEC_HOST_TEST).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef KNXSEC_HOST_TEST
extern "C" {
#include "aes.h"
}
#else
#include <mbedtls/aes.h>
#endif

namespace knxsec
{

static const size_t KEY_LEN = 16;
static const size_t BLOCK_LEN = 16;
static const size_t X25519_LEN = 32;

/**
 * A 128 bit AES key prepared for encryption.
 *
 * Holding the expanded key saves the key schedule on every block, which a
 * CBC-MAC over a 500 octet frame would otherwise repeat 32 times.
 */
class Aes
{
public:
    explicit Aes(const uint8_t key[KEY_LEN]);
    ~Aes();

    Aes(const Aes&) = delete;
    Aes& operator=(const Aes&) = delete;

    void encrypt(const uint8_t in[BLOCK_LEN], uint8_t out[BLOCK_LEN]) const;

private:
#ifdef KNXSEC_HOST_TEST
    struct AES_ctx _ctx;
#else
    mutable mbedtls_aes_context _ctx;
#endif
};

/**
 * The KNX CBC-MAC: B0, the length of the additional data as two octets,
 * the additional data, the payload - zero padded, AES-CBC from a zero IV,
 * last cipher block.
 */
void cbcMac(const uint8_t key[KEY_LEN], const uint8_t b0[BLOCK_LEN],
            const uint8_t* ad, size_t adLength,
            const uint8_t* payload, size_t payloadLength,
            uint8_t mac[BLOCK_LEN]);

/**
 * One continuous AES-CTR keystream from ctr0 over the MAC and then the data.
 *
 * Symmetric: the same call encrypts and decrypts. Both buffers are changed in
 * place. The counter block is incremented as a 128 bit big-endian number.
 *
 * @param macLength 16 for KNXnet/IP Secure, 4 for Data Secure
 */
void ctrCrypt(const uint8_t key[KEY_LEN], const uint8_t ctr0[BLOCK_LEN],
              uint8_t* mac, size_t macLength, uint8_t* data, size_t dataLength);

/** Comparison without an early exit, for MACs. */
bool equal(const uint8_t* a, const uint8_t* b, size_t length);

/** Overwrite key material before the memory is reused. */
void wipe(void* data, size_t length);

void sha256(const uint8_t* data, size_t length, uint8_t out[32]);

/**
 * Random octets for keys, challenges and message tags.
 *
 * A CTR-DRBG, seeded once from the hardware RNG with the SAR ADC as entropy
 * source (bootloader_random_enable), and fed with esp_random() on every call.
 * esp_random() alone is only truly random while the radio runs, and in
 * Ethernet mode it does not.
 */
void random(uint8_t* out, size_t length);

/**
 * Seed the generator. Call once, early and before WiFi starts: the ADC
 * entropy source and the radio do not run at the same time.
 */
void beginRandom();

/** A fresh X25519 key pair, both halves in RFC 7748 encoding. */
bool x25519Keypair(uint8_t priv[X25519_LEN], uint8_t pub[X25519_LEN]);

/** X25519(priv, peer) - 32 octets, RFC 7748 encoding. */
bool x25519(const uint8_t priv[X25519_LEN], const uint8_t peer[X25519_LEN],
            uint8_t shared[X25519_LEN]);

/**
 * Check the primitives against published vectors: RFC 7748 for X25519, the
 * examples of 03_08_09 "KNX IP Secure" Annex A for the CBC-MAC, the CTR
 * stage and the server side of a session.
 *
 * @param report receives a one line summary
 * @return true if every vector matched
 */
bool selfTest(char* report, size_t reportLength);

} // namespace knxsec
