/*
 *  knxip_secure_frames.cpp - The frames of KNXnet/IP Secure.
 */

#include "knxip_secure_frames.h"

#include <string.h>

#include "knx_secure_crypto.h"

namespace knxsec
{

namespace
{

/*
 * Counter block 0 of the handshake MACs: fourteen zeros, then FF 00. The
 * FF 00 in the last two octets is what every KNXnet/IP Secure Ctr0 ends in;
 * the handshake simply has no sequence, serial or tag in front of it.
 */
const uint8_t CTR0_HANDSHAKE[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0x00};

const uint8_t ZERO_BLOCK[16] = {0};

void securityInfo(uint8_t out[16], uint64_t sequence, const uint8_t serial[SERIAL_LEN],
                  uint16_t tag, uint16_t last)
{
    put48(out, sequence);
    memcpy(out + 6, serial, SERIAL_LEN);
    out[12] = (uint8_t)(tag >> 8);
    out[13] = (uint8_t)tag;
    out[14] = (uint8_t)(last >> 8);
    out[15] = (uint8_t)last;
}

} // namespace

uint16_t serviceType(const uint8_t* frame, size_t length)
{
    if (length < HEADER_LEN || frame[0] != 0x06 || frame[1] != 0x10)
        return 0;
    return (uint16_t)((frame[2] << 8) | frame[3]);
}

uint16_t totalLength(const uint8_t* frame)
{
    return (uint16_t)((frame[4] << 8) | frame[5]);
}

void header(uint8_t* out, uint16_t service, uint16_t total)
{
    out[0] = 0x06;
    out[1] = 0x10;
    out[2] = (uint8_t)(service >> 8);
    out[3] = (uint8_t)service;
    out[4] = (uint8_t)(total >> 8);
    out[5] = (uint8_t)total;
}

void put48(uint8_t* out, uint64_t value)
{
    for (int i = 5; i >= 0; i--)
    {
        out[i] = (uint8_t)value;
        value >>= 8;
    }
}

uint64_t get48(const uint8_t* in)
{
    uint64_t value = 0;
    for (int i = 0; i < 6; i++)
        value = (value << 8) | in[i];
    return value;
}

size_t wrap(const uint8_t key[16], uint16_t sessionId, uint64_t sequence,
            const uint8_t serial[SERIAL_LEN], uint16_t tag,
            const uint8_t* frame, size_t length, uint8_t* out)
{
    const size_t total = WRAPPER_OVERHEAD + length;

    header(out, SECURE_WRAPPER, (uint16_t)total);
    out[6] = (uint8_t)(sessionId >> 8);
    out[7] = (uint8_t)sessionId;
    put48(out + 8, sequence);
    memcpy(out + 14, serial, SERIAL_LEN);
    out[20] = (uint8_t)(tag >> 8);
    out[21] = (uint8_t)tag;

    uint8_t b0[16], ctr0[16], mac[MAC_LEN];
    securityInfo(b0, sequence, serial, tag, (uint16_t)length);
    securityInfo(ctr0, sequence, serial, tag, 0xFF00);

    // Additional data: the wrapper's own header and the session id.
    cbcMac(key, b0, out, HEADER_LEN + 2, frame, length, mac);

    uint8_t* payload = out + 22;
    memmove(payload, frame, length);
    ctrCrypt(key, ctr0, mac, MAC_LEN, payload, length);
    memcpy(payload + length, mac, MAC_LEN);

    wipe(mac, sizeof(mac));
    return total;
}

bool parseWrapper(const uint8_t* frame, size_t length, Wrapper& out)
{
    // 2.2.1.3.3: the wrapped frame has at least its own header, so anything
    // under 44 octets is discarded.
    if (serviceType(frame, length) != SECURE_WRAPPER || length < WRAPPER_OVERHEAD + HEADER_LEN ||
        totalLength(frame) != length)
        return false;

    out.sessionId   = (uint16_t)((frame[6] << 8) | frame[7]);
    out.sequence    = get48(frame + 8);
    out.serial      = frame + 14;
    out.tag         = (uint16_t)((frame[20] << 8) | frame[21]);
    out.innerLength = length - WRAPPER_OVERHEAD;
    return true;
}

bool unwrap(const uint8_t key[16], const uint8_t* frame, size_t length,
            uint8_t* inner, size_t& innerLength)
{
    Wrapper w;
    if (!parseWrapper(frame, length, w))
        return false;

    uint8_t b0[16], ctr0[16], mac[MAC_LEN], expected[MAC_LEN];
    securityInfo(b0, w.sequence, w.serial, w.tag, (uint16_t)w.innerLength);
    securityInfo(ctr0, w.sequence, w.serial, w.tag, 0xFF00);

    memcpy(mac, frame + 22 + w.innerLength, MAC_LEN);
    memmove(inner, frame + 22, w.innerLength);
    ctrCrypt(key, ctr0, mac, MAC_LEN, inner, w.innerLength);

    cbcMac(key, b0, frame, HEADER_LEN + 2, inner, w.innerLength, expected);

    bool ok = equal(mac, expected, MAC_LEN);
    wipe(mac, sizeof(mac));
    wipe(expected, sizeof(expected));

    // A frame that decrypts but whose header disagrees with its size is as
    // good as a wrong MAC for what follows.
    if (!ok || serviceType(inner, w.innerLength) == 0 ||
        totalLength(inner) != w.innerLength)
    {
        wipe(inner, w.innerLength);
        return false;
    }

    innerLength = w.innerLength;
    return true;
}

void sessionResponseMac(const uint8_t authCode[16], uint16_t sessionId,
                        const uint8_t xorKeys[32], uint8_t mac[MAC_LEN])
{
    uint8_t ad[HEADER_LEN + 2 + 32];
    header(ad, SESSION_RESPONSE, SESSION_RESPONSE_LEN);
    ad[6] = (uint8_t)(sessionId >> 8);
    ad[7] = (uint8_t)sessionId;
    memcpy(ad + 8, xorKeys, 32);

    cbcMac(authCode, ZERO_BLOCK, ad, sizeof(ad), nullptr, 0, mac);
    ctrCrypt(authCode, CTR0_HANDSHAKE, mac, MAC_LEN, nullptr, 0);
}

void sessionAuthMac(const uint8_t passwordHash[16], uint8_t userId,
                    const uint8_t xorKeys[32], uint8_t mac[MAC_LEN])
{
    uint8_t ad[HEADER_LEN + 2 + 32];
    header(ad, SESSION_AUTHENTICATE, SESSION_AUTH_LEN);
    ad[6] = 0x00;
    ad[7] = userId;
    memcpy(ad + 8, xorKeys, 32);

    cbcMac(passwordHash, ZERO_BLOCK, ad, sizeof(ad), nullptr, 0, mac);
    ctrCrypt(passwordHash, CTR0_HANDSHAKE, mac, MAC_LEN, nullptr, 0);
}

void timerNotifyMac(const uint8_t backboneKey[16], uint64_t timer,
                    const uint8_t serial[SERIAL_LEN], uint16_t tag, uint8_t mac[MAC_LEN])
{
    uint8_t ad[HEADER_LEN];
    header(ad, TIMER_NOTIFY, TIMER_NOTIFY_LEN);

    uint8_t b0[16], ctr0[16];
    securityInfo(b0, timer, serial, tag, 0x0000);
    securityInfo(ctr0, timer, serial, tag, 0xFF00);

    cbcMac(backboneKey, b0, ad, sizeof(ad), nullptr, 0, mac);
    ctrCrypt(backboneKey, ctr0, mac, MAC_LEN, nullptr, 0);
}

size_t timerNotify(const uint8_t backboneKey[16], uint64_t timer,
                   const uint8_t serial[SERIAL_LEN], uint16_t tag, uint8_t* out)
{
    header(out, TIMER_NOTIFY, TIMER_NOTIFY_LEN);
    put48(out + 6, timer);
    memcpy(out + 12, serial, SERIAL_LEN);
    out[18] = (uint8_t)(tag >> 8);
    out[19] = (uint8_t)tag;
    timerNotifyMac(backboneKey, timer, serial, tag, out + 20);
    return TIMER_NOTIFY_LEN;
}

} // namespace knxsec
