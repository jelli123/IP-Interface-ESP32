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

String KnxSecureStore::certificate(const uint8_t serial[6]) const
{
    static const char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

    uint8_t data[22];
    memcpy(data, serial, 6);
    memcpy(data + 6, _fdsk, 16);

    String   out;
    uint32_t buffer = 0;
    int      bits   = 0;
    int      chars  = 0;

    for (size_t i = 0; i < sizeof(data); i++)
    {
        buffer = (buffer << 8) | data[i];
        bits += 8;

        while (bits >= 5)
        {
            if (chars && chars % 6 == 0) out += '-';
            out += ALPHABET[(buffer >> (bits - 5)) & 0x1F];
            bits -= 5;
            chars++;
        }
    }

    // 176 bits leave one last character of which four bits are spare.
    if (bits > 0)
    {
        if (chars % 6 == 0) out += '-';
        out += ALPHABET[(buffer << (5 - bits)) & 0x1F];
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
