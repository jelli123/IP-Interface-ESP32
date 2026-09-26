/*
 *  knx_secure_store.h - What KNX Secure has to keep across restarts, outside
 *  the stack's memory image.
 *
 *  The FDSK
 *
 *    The Factory Default Setup Key is the device's tool key until ETS
 *    replaces it, and again after every factory reset. The ETS learns it from
 *    the device certificate on the label - so it has to be unique per device
 *    and must never change. The stack had it compiled in as 00 01 02 .. 0F,
 *    identical on every device ever built, which made "secure" commissioning
 *    a formality anyone could replay. Here it is drawn from the hardware RNG
 *    on the first start and kept in NVS; the knxcfg partition is no place for
 *    it, because a master reset wipes that.
 *
 *  Sequence numbers
 *
 *    Data Secure refuses any frame whose sequence number is not above the
 *    last one it accepted from that sender. The stack keeps its own counters
 *    in RAM and starts them at a constant after every restart, so after a
 *    power cycle the device sends numbers the ETS has already seen, and its
 *    answers are thrown away. The counters are therefore reserved in blocks:
 *    NVS holds a high-water mark above everything handed out, one write per
 *    block, and a restart continues from the mark.
 *
 *    The last number accepted from the tool is kept too, or a frame recorded
 *    before the restart could be replayed after it. It changes only while ETS
 *    is programming the device and is written at most every two seconds.
 */
#pragma once

#include <Arduino.h>
#include <stdint.h>

class KnxSecureStore
{
public:
    /** Which counter a sequence number belongs to. */
    enum Counter : uint8_t
    {
        SEND        = 0, //!< our own frames, one counter for every key
        VALID_TOOL  = 1, //!< last one accepted from the tool
        COUNTER_COUNT
    };

    /**
     * Load the FDSK, creating it on the very first start, and the sequence
     * marks. After knxsec::beginRandom(), before the KNX stack starts.
     */
    void begin();

    /** Persist what changed. Main task. */
    void loop();

    /** The 16 octet FDSK. */
    const uint8_t* fdsk() const { return _fdsk; }

    /** True if begin() found no key and made one this start. */
    bool fdskCreated() const { return _created; }

    /**
     * The device certificate as the label would carry it: KNX serial number
     * and FDSK, 22 octets, Base32 (RFC 4648 alphabet), six groups of six
     * characters.
     *
     * The alphabet and length are what ETS documents (36 characters, no 0 or
     * 1). Whether ETS expects a check value in the four spare bits is not
     * public; they are left at zero. Serial number and FDSK are shown in hex
     * alongside for exactly that reason.
     */
    String certificate(const uint8_t serial[6]) const;

    /**
     * Called by the stack, see patch 22 in scripts/patch_knx.py.
     *
     * With store false: the lowest value the counter may continue from. With
     * store true: the stack has used or accepted @p value.
     */
    uint64_t sequence(Counter counter, uint64_t value, bool store);

    /** Current value of a counter, for the dashboard. */
    uint64_t current(Counter counter) const { return _value[counter]; }

    /** Result of knxsec::selfTest() at start-up, for the dashboard. */
    void selfTest(bool ok, const char* report)
    {
        _testOk = ok;
        strlcpy(_testReport, report, sizeof(_testReport));
    }
    bool selfTestOk() const { return _testOk; }
    const char* selfTestReport() const { return _testReport; }

private:
    void persist(Counter counter, uint64_t value);

    uint8_t  _fdsk[16] = {0};
    bool     _created  = false;
    bool     _loaded   = false;

    uint64_t _value[COUNTER_COUNT]  = {0, 0}; //!< last value the stack reported
    uint64_t _stored[COUNTER_COUNT] = {0, 0}; //!< what NVS holds

    bool     _validDirty = false;
    uint32_t _validAt    = 0;

    bool _testOk = false;
    char _testReport[96] = "not run";
};

extern KnxSecureStore knxSecureStore;
