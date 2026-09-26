/*
 *  knxip_secure_frames.h - The frames of KNXnet/IP Secure (3/8/9), built and
 *  checked octet by octet.
 *
 *  Pure functions over buffers: no sockets, no state, no Arduino. That keeps
 *  them testable on the host against the examples in Annex A of 03_08_09, the only
 *  way to know the bytes are right before a real ETS talks to the device.
 *
 *      SECURE_WRAPPER   0950   header(6) session(2) seq(6) serial(6) tag(2)
 *                              encrypted frame(n) MAC(16)
 *      SESSION_REQUEST  0951   header(6) HPAI(8) client public key(32)
 *      SESSION_RESPONSE 0952   header(6) session(2) server public key(32) MAC(16)
 *      SESSION_AUTH     0953   header(6) reserved(1) user(1) MAC(16)
 *      SESSION_STATUS   0954   header(6) status(1) reserved(1)
 *      TIMER_NOTIFY     0955   header(6) timer(6) serial(6) tag(2) MAC(16)
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace knxsec
{

enum ServiceType : uint16_t
{
    SECURE_WRAPPER       = 0x0950,
    SESSION_REQUEST      = 0x0951,
    SESSION_RESPONSE     = 0x0952,
    SESSION_AUTHENTICATE = 0x0953,
    SESSION_STATUS       = 0x0954,
    TIMER_NOTIFY         = 0x0955,
};

enum SessionStatus : uint8_t
{
    STATUS_AUTH_SUCCESS    = 0x00,
    STATUS_AUTH_FAILED     = 0x01,
    STATUS_UNAUTHENTICATED = 0x02,
    STATUS_TIMEOUT         = 0x03,
    STATUS_KEEPALIVE       = 0x04,
    STATUS_CLOSE           = 0x05,
};

static const size_t HEADER_LEN = 6;
static const size_t MAC_LEN = 16;
static const size_t SEQ_LEN = 6;
static const size_t SERIAL_LEN = 6;

/** Everything a SECURE_WRAPPER adds around the frame it carries. */
static const size_t WRAPPER_OVERHEAD = HEADER_LEN + 2 + SEQ_LEN + SERIAL_LEN + 2 + MAC_LEN;

static const size_t SESSION_REQUEST_LEN = HEADER_LEN + 8 + 32;
static const size_t SESSION_RESPONSE_LEN = HEADER_LEN + 2 + 32 + MAC_LEN;
static const size_t SESSION_AUTH_LEN = HEADER_LEN + 2 + MAC_LEN;
static const size_t SESSION_STATUS_LEN = HEADER_LEN + 2;
static const size_t TIMER_NOTIFY_LEN = HEADER_LEN + SEQ_LEN + SERIAL_LEN + 2 + MAC_LEN;

/** Service type of a KNXnet/IP frame, 0 if the header is not one. */
uint16_t serviceType(const uint8_t* frame, size_t length);

/** Total length from the header. */
uint16_t totalLength(const uint8_t* frame);

/** Write a KNXnet/IP header. */
void header(uint8_t* out, uint16_t service, uint16_t total);

/** 48 bit big-endian numbers, as sequence information and timer use them. */
void put48(uint8_t* out, uint64_t value);
uint64_t get48(const uint8_t* in);

/**
 * Wrap a KNXnet/IP frame.
 *
 * @param out receives WRAPPER_OVERHEAD + length octets
 * @return the length of the wrapper
 */
size_t wrap(const uint8_t key[16], uint16_t sessionId, uint64_t sequence,
            const uint8_t serial[SERIAL_LEN], uint16_t tag,
            const uint8_t* frame, size_t length, uint8_t* out);

/** The fields of a SECURE_WRAPPER, pointers into the frame. */
struct Wrapper
{
    uint16_t       sessionId;
    uint64_t       sequence;
    const uint8_t* serial;
    uint16_t       tag;
    size_t         innerLength;
};

/**
 * Take a SECURE_WRAPPER apart without checking it.
 *
 * @return false if the length does not fit
 */
bool parseWrapper(const uint8_t* frame, size_t length, Wrapper& out);

/**
 * Verify and decrypt a SECURE_WRAPPER.
 *
 * @param inner receives the frame it carried, innerLength octets
 * @return true if the MAC matched and the inner frame's own header agrees
 *         with its length
 */
bool unwrap(const uint8_t key[16], const uint8_t* frame, size_t length,
            uint8_t* inner, size_t& innerLength);

/**
 * The MAC of a SESSION_RESPONSE, already CTR-encrypted as it goes on the
 * wire: proves the server knows the device authentication code.
 */
void sessionResponseMac(const uint8_t authCode[16], uint16_t sessionId,
                        const uint8_t xorKeys[32], uint8_t mac[MAC_LEN]);

/** The MAC a client puts into SESSION_AUTHENTICATE for the given user. */
void sessionAuthMac(const uint8_t passwordHash[16], uint8_t userId,
                    const uint8_t xorKeys[32], uint8_t mac[MAC_LEN]);

/** The MAC of a TIMER_NOTIFY. */
void timerNotifyMac(const uint8_t backboneKey[16], uint64_t timer,
                    const uint8_t serial[SERIAL_LEN], uint16_t tag, uint8_t mac[MAC_LEN]);

/** Build a TIMER_NOTIFY, TIMER_NOTIFY_LEN octets. */
size_t timerNotify(const uint8_t backboneKey[16], uint64_t timer,
                   const uint8_t serial[SERIAL_LEN], uint16_t tag, uint8_t* out);

} // namespace knxsec
