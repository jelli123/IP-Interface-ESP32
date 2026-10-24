/*
 *  knx_secure_diag.h - Why Data Secure dropped a telegram.
 *
 *  The secure application layer drops a sync.req with a wrong MAC, a replay
 *  or a telegram it has no key for without any answer, as the specification
 *  wants it - and without a trace, its own prints being compiled out. One
 *  such case (a tool that computed the MAC with sender 0.0.0, while the
 *  device checked it with the tunnel address) took hours that a single log
 *  line would have saved.
 *
 *  The stack reports each drop here (scripts/patch_knx.py 37). Every reason
 *  is counted for /api/status, and logged at most once a minute per sender
 *  and reason.
 */
#pragma once

#include <Arduino.h>
#include <stdint.h>

class KnxSecureDiag
{
public:
    /** Reasons, as the stack passes them. Prefixed: Arduino.h defines SERIAL. */
    enum Reason : uint8_t
    {
        REJECT_NO_KEY        = 1, //!< no key for the sender or group address
        REJECT_REPLAY        = 2, //!< sequence number not above the last valid one
        REJECT_SERIAL        = 3, //!< sync.req to this device with another serial number
        REJECT_SYNC_TOO_SOON = 4, //!< sync.req less than 1 s after the last sync.res
        REJECT_NO_CHALLENGE  = 5, //!< sync.res without a sync.req of ours pending
        REJECT_MAC           = 6, //!< the MAC does not match the key
        REJECT_REASONS       = 7
    };

    /** Install the hook in the stack. */
    void begin();

    /** Counters as a JSON object, for /api/status. */
    String json() const;

private:
    static void onReject(uint8_t reason, uint16_t src, uint16_t dst, bool group,
                         uint8_t scf, uint64_t seq, uint64_t expected);

    void report(Reason reason, uint16_t src, uint16_t dst, bool group,
                uint8_t scf, uint64_t seq, uint64_t expected);
    bool throttled(Reason reason, uint16_t src);

    struct Recent
    {
        uint16_t src;
        uint8_t  reason;
        uint32_t at;
    };

    static const uint8_t RECENT = 8;

    uint32_t _count[REJECT_REASONS] = {};
    uint32_t _toolMacFail    = 0;
    Recent   _recent[RECENT] = {};
};

extern KnxSecureDiag knxSecureDiag;
