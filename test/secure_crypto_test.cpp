/*
 *  secure_crypto_test.cpp - Host test of the KNX Secure frames against
 *  published vectors.
 *
 *  Runs on the PC, not the ESP32: test/run_host_tests.sh builds it with the
 *  KNX stack's portable AES in place of mbedTLS. X25519 and SHA-256 are not
 *  covered here - they come straight from mbedTLS and are checked on the
 *  device by knxsec::selfTest() at every start.
 *
 *  Sources of the vectors:
 *    AN159   the worked examples of 03_08_09 "KNX IP Secure" Annex A (KNX
 *            Standard v3.0), which go back to AN159; xknx carries the same
 *            values in its tests
 *    bussard synthetic vectors from docs/knx-secure-spec.md section 12.1,
 *            computed by three independent implementations
 *    python  computed with the xknx algorithm and the cryptography package
 */

#include <stdio.h>
#include <string.h>

#include "knx_secure_crypto.h"
#include "knxip_secure_frames.h"

using namespace knxsec;

static int failures = 0;

static size_t hex(const char* text, uint8_t* out)
{
    size_t n = 0;
    while (*text)
    {
        if (*text == ' ')
        {
            text++;
            continue;
        }
        unsigned value;
        sscanf(text, "%2x", &value);
        out[n++] = (uint8_t)value;
        text += 2;
    }
    return n;
}

static void expect(const char* name, const uint8_t* actual, const char* expectedHex)
{
    uint8_t expected[600];
    size_t  length = hex(expectedHex, expected);

    if (memcmp(actual, expected, length) == 0)
    {
        printf("ok    %s\n", name);
        return;
    }

    failures++;
    printf("FAIL  %s\n      got      ", name);
    for (size_t i = 0; i < length; i++)
        printf("%02x", actual[i]);
    printf("\n      expected %s\n", expectedHex);
}

static void expectTrue(const char* name, bool value)
{
    printf("%s %s\n", value ? "ok   " : "FAIL ", name);
    if (!value)
        failures++;
}

int main()
{
    uint8_t key[16], a[600], b[600], mac[16];

    // AN159: ROUTING_INDICATION in a SECURE_WRAPPER, the three stages.
    {
        uint8_t b0[16], ctr0[16], ad[8], payload[17];
        hex("000102030405060708090a0b0c0d0e0f", key);
        hex("c0c1c2c3c4c500fa12345678affe0011", b0);
        hex("c0c1c2c3c4c500fa12345678affeff00", ctr0);
        hex("0610095000370000", ad);
        hex("0610053000112900bcd011590ade010081", payload);

        cbcMac(key, b0, ad, sizeof(ad), payload, sizeof(payload), mac);
        expect("AN159 routing CBC-MAC", mac, "bd0a294b952554b23539204c2271d26b");

        ctrCrypt(key, ctr0, mac, sizeof(mac), payload, sizeof(payload));
        expect("AN159 routing CTR payload", payload, "b7ee7e8a1c2f7bbabec775fd6e10d0bc4b");
        expect("AN159 routing CTR MAC", mac, "7212a03aaae49da85689774c1d2b4da4");
    }

    // The same frame through wrap() and back through unwrap().
    {
        uint8_t serial[6], inner[17];
        hex("00fa12345678", serial);
        size_t n = hex("0610053000112900bcd011590ade010081", inner);

        size_t total = wrap(key, 0, 0xc0c1c2c3c4c5ull, serial, 0xaffe, inner, n, a);
        expectTrue("wrap length 0x37", total == 0x37);
        expect("AN159 routing SECURE_WRAPPER", a,
               "0610095000370000c0c1c2c3c4c500fa12345678affe"
               "b7ee7e8a1c2f7bbabec775fd6e10d0bc4b"
               "7212a03aaae49da85689774c1d2b4da4");

        size_t innerLength = 0;
        expectTrue("unwrap accepts it", unwrap(key, a, total, b, innerLength) && innerLength == n);
        expect("unwrap restores the frame", b, "0610053000112900bcd011590ade010081");

        a[30] ^= 0x01;
        expectTrue("unwrap refuses a changed octet", !unwrap(key, a, total, b, innerLength));
    }

    // AN159: session handshake. Keys of the example, session id 1.
    uint8_t xorKeys[32], clientPub[32], serverPub[32];
    hex("0aa227b4fd7a32319ba9960ac036ce0e5c4507b5ae55161f1078b1dcfb3cb631", clientPub);
    hex("bdf099909923143ef0a5de0b3be3687bc5bd3cf5f9e6f901699cd870ec1ff824", serverPub);
    for (int i = 0; i < 32; i++)
        xorKeys[i] = clientPub[i] ^ serverPub[i];

    {
        // PBKDF2("trustme", device-authentication-code.1.secure.ip.knx.org)
        uint8_t authCode[16];
        hex("e158e4012047bd6cc41aafbc5c04c1fc", authCode);
        sessionResponseMac(authCode, 1, xorKeys, mac);
        expect("AN159 SESSION_RESPONSE MAC", mac, "a922505aaa436163570bd5494c2df2a3");
    }

    {
        // PBKDF2("secret", user-password.1.secure.ip.knx.org), user 1
        uint8_t password[16];
        hex("03fcedb66660251ec81a1a716901696a", password);
        sessionAuthMac(password, 1, xorKeys, mac);
        expect("AN159 SESSION_AUTHENTICATE MAC", mac, "1f1d59ea9f12a152e5d9727f08462cde");
    }

    {
        // SHA-256 of X25519(client private, server public), first 16 octets.
        uint8_t sessionKey[16], serial[6], inner[24];
        hex("289426c2912535ba98279a4d1843c487", sessionKey);
        hex("00fa12345678", serial);
        size_t n = hex("0610095300180001 1f1d59ea9f12a152e5d9727f08462cde", inner);

        wrap(sessionKey, 1, 0, serial, 0xaffe, inner, n, a);
        expect("AN159 wrapped SESSION_AUTHENTICATE", a,
               "0610095000 3e 0001 000000000000 00fa12345678 affe"
               "7915a4f36e6e4208d28b4a207d8f35c0d138c26a7b5e7169"
               "52dba8e7e4bd80bd7d868a3ae78749de");

        // What the example server sends back: SESSION_STATUS success.
        size_t length = hex("06100950002e 0001 000000000000 00faaaaaaaaa affe"
                            "26156db5c749888f"
                            "a373c3e0b4bde4497c395e4b1c2f46a1", a);
        size_t innerLength = 0;
        expectTrue("AN159 SESSION_STATUS unwraps",
                   unwrap(sessionKey, a, length, b, innerLength) && innerLength == 8);
        expect("AN159 SESSION_STATUS is success", b, "0610095400080000");
    }

    // bussard section 12.1, synthetic.
    {
        uint8_t x[32], k[16];
        for (int i = 0; i < 32; i++)
            x[i] = 0x11 ^ 0x22;

        memset(k, 1, 16);
        sessionResponseMac(k, 1, x, mac);
        expect("bussard SESSION_RESPONSE MAC", mac, "3651034f87ba7fdad9675c2db8a9e52c");

        memset(k, 2, 16);
        sessionAuthMac(k, 2, x, mac);
        expect("bussard SESSION_AUTHENTICATE MAC", mac, "a2a7bc462ef36fb1d8a6df0df38f20a1");

        uint8_t serial[6], inner[8];
        memset(k, 3, 16);
        hex("00fa01020304", serial);
        size_t n = hex("0610095400080000", inner);
        wrap(k, 1, 0, serial, 0, inner, n, a);
        expect("bussard SECURE_WRAPPER", a,
               "06100950002e000100000000000000fa010203040000"
               "ebb209450bfa0d24f46fc31ebbe5f744fe21ea882aea3d19");
    }

    // 03_08_09 Annex A.6: TIMER_NOTIFY.
    {
        uint8_t serial[6];
        hex("000102030405060708090a0b0c0d0e0f", key);
        hex("00fa12345678", serial);
        timerNotifyMac(key, 0xc0c1c2c3c4c5ull, serial, 0xaffe, mac);
        expect("03_08_09 A.6 TIMER_NOTIFY MAC", mac, "ee7b9b3083deb1570eb38d073adad985");
    }

    // TIMER_NOTIFY, computed with the xknx algorithm.
    {
        uint8_t serial[6];
        hex("000102030405060708090a0b0c0d0e0f", key);
        hex("00fa12345678", serial);
        size_t n = timerNotify(key, 3600000, serial, 0x1234, a);
        expectTrue("TIMER_NOTIFY length 0x24", n == 0x24);
        expect("TIMER_NOTIFY", a,
               "061009550024 00000036ee80 00fa12345678 1234"
               "d422401041f06968437d5903b44d0ed4");
    }

    // Data Secure, the layout the KNX stack uses, against bussard's group
    // known-answer vector: key 00..0F, sequence 42, 1.1.1 -> 1/2/3, APDU 00 81.
    {
        uint8_t b0[16], ctr0[16];
        hex("000102030405060708090a0b0c0d0e0f", key);
        hex("00000000002a 1101 0a03 00 80 03 f1 00 02", b0);
        hex("00000000002a 1101 0a03 00000000 01 00", ctr0);

        uint8_t scf = 0x10, apdu[2] = {0x00, 0x81};
        cbcMac(key, b0, &scf, 1, apdu, 2, mac);
        ctrCrypt(key, ctr0, mac, 4, apdu, 2);
        expect("Data Secure auth+conf APDU", apdu, "df59");
        expect("Data Secure auth+conf MAC", mac, "49899fd3");

        // Authentication only: the APDU travels in the clear and is part of
        // the additional data after the SCF, B0 carries a payload length of 0.
        hex("00000000002a 1101 0a03 00 80 03 f1 00 00", b0);
        uint8_t ad[3] = {0x00, 0x00, 0x81};
        cbcMac(key, b0, ad, 3, nullptr, 0, mac);
        ctrCrypt(key, ctr0, mac, 4, nullptr, 0);
        expect("Data Secure auth-only MAC", mac, "2e51ca4a");
    }

    printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
