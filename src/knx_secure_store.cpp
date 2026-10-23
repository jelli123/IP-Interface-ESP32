/*
 *  knx_secure_store.cpp - What KNX Secure has to keep across restarts.
 */

#include "knx_secure_store.h"

#include <Preferences.h>

#include "knx_secure_crypto.h"
#include "log_buffer.h"

KnxSecureStore knxSecureStore;

static const char* SEC_NS = "sbip-sec";
static const char* KEY_FDSK = "fdsk";
static const char* KEY_SEQ[KnxSecureStore::COUNTER_COUNT] = {"seq", "validtool"};

/*
 * How far ahead of the counter the stored mark runs. A telegram per second
 * around the clock would take a quarter of an hour per block, and a device
 * that sends secured frames does so a few times a day - so one NVS write per
 * block is nothing, and a restart skips at most this many numbers, out of
 * 2^48.
 */
static const uint64_t SEQ_BLOCK = 1000;

void KnxSecureStore::begin()
{
    Preferences prefs;

    if (!prefs.begin(SEC_NS, false))
    {
        sysLog.println("SECURE: NVS namespace unavailable, FDSK not persistent");
        knxsec::random(_fdsk, sizeof(_fdsk));
        return;
    }

    if (prefs.getBytesLength(KEY_FDSK) == sizeof(_fdsk))
    {
        prefs.getBytes(KEY_FDSK, _fdsk, sizeof(_fdsk));
    }
    else
    {
        knxsec::random(_fdsk, sizeof(_fdsk));
        prefs.putBytes(KEY_FDSK, _fdsk, sizeof(_fdsk));
        _created = true;
        sysLog.println("SECURE: new FDSK created for this device");
    }

    for (uint8_t c = 0; c < COUNTER_COUNT; c++)
    {
        _stored[c] = prefs.getULong64(KEY_SEQ[c], 0);
        _value[c]  = _stored[c];
    }

    prefs.end();
    _loaded = true;
}

/** CRC-4, generator x^4 + x + 1, a nibble at a time, high nibble first. */
static uint8_t crc4(const uint8_t* data, size_t length)
{
    static const uint8_t TABLE[16] = {0x0, 0x3, 0x6, 0x5, 0xC, 0xF, 0xA, 0x9,
                                      0xB, 0x8, 0xD, 0xE, 0x7, 0x4, 0x1, 0x2};
    uint8_t crc = 0;
    for (size_t i = 0; i < length; i++)
    {
        crc = TABLE[crc ^ (data[i] >> 4)];
        crc = TABLE[crc ^ (data[i] & 0x0F)];
    }
    return crc;
}

String KnxSecureStore::certificate(const uint8_t serial[6]) const
{
    static const char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

    // Serial number, FDSK, and the check value in the high nibble of a 23rd octet
    uint8_t data[23];
    memcpy(data, serial, 6);
    memcpy(data + 6, _fdsk, 16);
    data[22] = (uint8_t)(crc4(data, 22) << 4);

    String   out;
    uint32_t buffer = 0;
    int      bits   = 0;
    int      chars  = 0;

    // 180 of the 184 bits: 36 characters, the last four bits of the 23rd octet unused
    for (size_t i = 0; i < sizeof(data); i++)
    {
        buffer = (buffer << 8) | data[i];
        bits += 8;

        while (bits >= 5 && chars < 36)
        {
            if (chars && chars % 6 == 0) out += '-';
            out += ALPHABET[(buffer >> (bits - 5)) & 0x1F];
            bits -= 5;
            chars++;
        }
    }

    knxsec::wipe(data, sizeof(data));
    return out;
}

uint64_t KnxSecureStore::sequence(Counter counter, uint64_t value, bool store)
{
    if (counter >= COUNTER_COUNT) return value;

    if (!store)
    {
        // begin() set our own counters to the stored mark, which lies above
        // anything handed out before the restart; from then on they follow
        // the stack. The tool's last valid number is the number itself.
        return _value[counter];
    }

    if (value > _value[counter]) _value[counter] = value;

    if (counter == VALID_TOOL)
    {
        _validDirty = true;
        return value;
    }

    // Reserve the next block before the current one runs out - here, in the
    // stack's call, so that no number above the stored mark ever leaves the
    // device.
    if (value + 1 >= _stored[counter])
    {
        persist(counter, value + SEQ_BLOCK);
    }

    return value;
}

void KnxSecureStore::persist(Counter counter, uint64_t value)
{
    Preferences prefs;

    if (!prefs.begin(SEC_NS, false)) return;

    prefs.putULong64(KEY_SEQ[counter], value);
    prefs.end();
    _stored[counter] = value;
}

void KnxSecureStore::loop()
{
    if (!_validDirty) return;

    if ((uint32_t)(millis() - _validAt) < 2000) return;
    _validAt = millis();

    _validDirty = false;
    if (_value[VALID_TOOL] != _stored[VALID_TOOL])
    {
        persist(VALID_TOOL, _value[VALID_TOOL]);
    }
}
