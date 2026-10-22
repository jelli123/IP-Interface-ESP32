/*
 *  knx_proxy_core.cpp - The Security Proxy of AN192, without the stack.
 *  See knx_proxy_core.h.
 */

#include "knx_proxy_core.h"

#include <string.h>

#include "knx_secure_crypto.h"

namespace knxproxy
{

namespace
{

const uint8_t APCI_SECURE_LOW = 0xF1;  // APCI 03F1h, the upper two bits sit in the TPCI octet
const uint8_t SCF_TOOL        = 0x80;
const uint8_t SCF_SBC         = 0x08;
const uint8_t SCF_SERVICE     = 0x07;
const uint8_t SCF_CONF        = 0x10;  // SAI 001b: authentication and confidentiality

/** Shortest S-A_Data: TPCI/APCI, SCF, sequence, a two octet APDU, MAC. */
const size_t MIN_SECURE_DATA = 2 + 1 + 6 + 2 + 4;

void block0(uint8_t* b0, const uint8_t* seq, uint16_t source, uint16_t group,
            uint8_t tpci, uint8_t payloadLength)
{
    // The stack's SecureApplicationLayer::block0(): no LTE, frame type 0.
    memcpy(b0, seq, 6);
    b0[6]  = (uint8_t)(source >> 8);
    b0[7]  = (uint8_t)source;
    b0[8]  = (uint8_t)(group >> 8);
    b0[9]  = (uint8_t)group;
    b0[10] = 0x00;
    b0[11] = 0x80;  // group address, standard frame format
    b0[12] = tpci;
    b0[13] = APCI_SECURE_LOW;
    b0[14] = 0x00;
    b0[15] = payloadLength;
}

void ctr0(uint8_t* ctr, const uint8_t* seq, uint16_t source, uint16_t group)
{
    memset(ctr, 0, 16);
    memcpy(ctr, seq, 6);
    ctr[6]  = (uint8_t)(source >> 8);
    ctr[7]  = (uint8_t)source;
    ctr[8]  = (uint8_t)(group >> 8);
    ctr[9]  = (uint8_t)group;
    ctr[14] = 0x01;
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

/**
 * The MAC as it travels: the first four octets of the CBC-MAC, run through
 * the first four octets of the keystream. With confidentiality the payload
 * follows in the same keystream - ctrCrypt() does both in one go, so this
 * only covers authentication alone.
 */
void authOnlyMac(const uint8_t key[16], uint8_t* b0, const uint8_t* ctr,
                 uint8_t scf, const uint8_t* plain, size_t length, uint8_t mac[4])
{
    // AN158: SCF and APDU are the additional data, B0 announces no payload.
    uint8_t ad[1 + 255];
    ad[0] = scf;
    memcpy(ad + 1, plain, length);
    b0[15] = 0;

    uint8_t full[16];
    knxsec::cbcMac(key, b0, ad, 1 + length, nullptr, 0, full);
    memcpy(mac, full, 4);
    knxsec::ctrCrypt(key, ctr, mac, 4, nullptr, 0);
    knxsec::wipe(full, sizeof(full));
}

} // namespace

Kind classify(const uint8_t* tpdu, size_t length)
{
    if (length < 2 || (tpdu[0] & 0x03) != 0x03 || tpdu[1] != APCI_SECURE_LOW)
        return PLAIN;

    if (length < 3)
        return SECURE_OTHER;

    switch (tpdu[2] & SCF_SERVICE)
    {
        case 0:
            return length >= MIN_SECURE_DATA ? SECURE_DATA : SECURE_OTHER;
        case 2:
        case 3:
            return SECURE_SYNC;
        default:
            return SECURE_OTHER;
    }
}

size_t open(const uint8_t key[16], uint16_t source, uint16_t group,
            const uint8_t* tpdu, size_t length, uint8_t* plain, uint64_t& seq)
{
    if (length < MIN_SECURE_DATA || length - SECURE_OVERHEAD > 255)
        return 0;

    uint8_t        scf     = tpdu[2];
    const uint8_t* seqRaw  = tpdu + 3;
    const uint8_t* payload = tpdu + 9;
    size_t         plainLength = length - SECURE_OVERHEAD;

    uint8_t b0[16], ctr[16], mac[4], expected[4];
    block0(b0, seqRaw, source, group, tpdu[0], (uint8_t)plainLength);
    ctr0(ctr, seqRaw, source, group);
    memcpy(mac, payload + plainLength, 4);
    memcpy(plain, payload, plainLength);

    bool ok;

    if (scf & SCF_CONF)
    {
        // One keystream: first the MAC, then the APDU.
        knxsec::ctrCrypt(key, ctr, mac, 4, plain, plainLength);

        uint8_t full[16];
        knxsec::cbcMac(key, b0, &scf, 1, plain, plainLength, full);
        memcpy(expected, full, 4);
        knxsec::wipe(full, sizeof(full));
        ok = knxsec::equal(mac, expected, 4);
    }
    else
    {
        authOnlyMac(key, b0, ctr, scf, plain, plainLength, expected);
        ok = knxsec::equal(mac, expected, 4);
    }

    if (!ok)
    {
        knxsec::wipe(plain, plainLength);
        return 0;
    }

    seq = get48(seqRaw);
    return plainLength;
}

size_t seal(const uint8_t key[16], uint16_t source, uint16_t group, uint64_t seq, bool conf,
            const uint8_t* plain, size_t length, uint8_t* secure)
{
    if (length < 1 || length + SECURE_OVERHEAD > 255)
        return 0;

    uint8_t scf = conf ? SCF_CONF : 0x00;  // S-A_Data, no tool access, no SBC

    secure[0] = 0x03;  // T_Data_Group, upper APCI bits
    secure[1] = APCI_SECURE_LOW;
    secure[2] = scf;
    put48(secure + 3, seq);

    uint8_t* payload = secure + 9;
    uint8_t* mac     = payload + length;
    memcpy(payload, plain, length);

    uint8_t b0[16], ctr[16];
    block0(b0, secure + 3, source, group, secure[0], (uint8_t)length);
    ctr0(ctr, secure + 3, source, group);

    if (conf)
    {
        uint8_t full[16];
        knxsec::cbcMac(key, b0, &scf, 1, payload, length, full);
        memcpy(mac, full, 4);
        knxsec::wipe(full, sizeof(full));
        knxsec::ctrCrypt(key, ctr, mac, 4, payload, length);
    }
    else
    {
        authOnlyMac(key, b0, ctr, scf, payload, length, mac);
    }

    return length + SECURE_OVERHEAD;
}

Action groupAction(const GroupKey* entry, uint8_t fromSide, Kind kind, uint8_t scf)
{
    // Not allowed in this communication mode at all (AN158), whatever the table says.
    if (kind == SECURE_SYNC)
        return BLOCK;

    // Cases 2 and 5 of Table 8: the proxy has nothing to do with this GA.
    if (entry == nullptr)
        return FORWARD;

    uint8_t rxBit = fromSide == PRIMARY ? SEC_AT_PRIMARY : SEC_AT_SECONDARY;
    uint8_t txBit = fromSide == PRIMARY ? SEC_AT_SECONDARY : SEC_AT_PRIMARY;
    bool    rxSecure = (entry->settings & rxBit) != 0;
    bool    txSecure = (entry->settings & txBit) != 0;

    // 2.3.1.5.6 "Rule": the same on both sides is a configuration mistake.
    if (rxSecure == txSecure)
        return BLOCK;

    // Data Secure has no confidentiality without authentication (Example 5).
    if (!(entry->settings & FLAG_AUTH))
        return BLOCK;

    if (kind == PLAIN)
        return rxSecure ? BLOCK : ENCRYPT;  // 3.2 and 3.1

    // 4.1: secured on the plain side - a misconfiguration or someone probing.
    if (kind != SECURE_DATA || !rxSecure)
        return BLOCK;

    // 4.2, if the frame is what the table promises: a group key, no system
    // broadcast, and the SAI of the Security Flags.
    if (scf & (SCF_TOOL | SCF_SBC))
        return BLOCK;

    bool conf = (entry->settings & FLAG_CONF) != 0;
    if (((scf & 0x70) == SCF_CONF) != conf || (scf & 0x60) != 0)
        return BLOCK;

    return DECRYPT;
}

// ---------------------------------------------------------------- PID 81

const GroupKey* GroupKeyTable::find(uint16_t ga) const
{
    for (uint16_t i = 0; i < _count; i++)
    {
        if (_entries[i].ga == ga)
            return &_entries[i];
    }
    return nullptr;
}

uint8_t GroupKeyTable::read(uint16_t start, uint8_t count, uint8_t* data) const
{
    if (start == 0)
    {
        data[0] = (uint8_t)(_count >> 8);
        data[1] = (uint8_t)_count;
        return 1;
    }

    if (start - 1 + count > _count)
        return 0;

    for (uint8_t i = 0; i < count; i++)
    {
        const GroupKey& e = _entries[start - 1 + i];
        uint8_t*        p = data + i * GROUP_KEY_SIZE;
        p[0] = (uint8_t)(e.ga >> 8);
        p[1] = (uint8_t)e.ga;
        memcpy(p + 2, e.key, 16);
        p[18] = e.settings;
    }
    return count;
}

uint8_t GroupKeyTable::write(uint16_t start, uint8_t count, const uint8_t* data)
{
    if (start == 0)
    {
        // The element count; ETS clears a table this way before writing it anew.
        uint16_t elements = (uint16_t)((data[0] << 8) | data[1]);
        if (elements > MAX_GROUP_KEYS)
            return 0;

        if (elements < _count)
            knxsec::wipe(&_entries[elements], (size_t)(_count - elements) * sizeof(GroupKey));
        _count = elements;
        return 1;
    }

    if (start - 1 + count > MAX_GROUP_KEYS)
        return 0;

    // Writing past the end leaves no gap of stale entries behind.
    if (start - 1 > _count)
        memset(&_entries[_count], 0, (size_t)(start - 1 - _count) * sizeof(GroupKey));

    for (uint8_t i = 0; i < count; i++)
    {
        GroupKey&      e = _entries[start - 1 + i];
        const uint8_t* p = data + i * GROUP_KEY_SIZE;
        e.ga = (uint16_t)((p[0] << 8) | p[1]);
        memcpy(e.key, p + 2, 16);
        e.settings = p[18];
    }

    if (start - 1 + count > _count)
        _count = (uint16_t)(start - 1 + count);

    return count;
}

void GroupKeyTable::clear()
{
    knxsec::wipe(_entries, sizeof(_entries));
    _count = 0;
}

size_t GroupKeyTable::serialize(uint8_t* out, size_t max) const
{
    size_t length = (size_t)_count * GROUP_KEY_SIZE;
    if (length > max || _count == 0)
        return 0;

    for (uint16_t i = 0; i < _count; i++)
        read(i + 1, 1, out + (size_t)i * GROUP_KEY_SIZE);
    return length;
}

void GroupKeyTable::deserialize(const uint8_t* in, size_t length)
{
    clear();

    size_t elements = length / GROUP_KEY_SIZE;
    if (elements > MAX_GROUP_KEYS)
        elements = MAX_GROUP_KEYS;

    for (size_t i = 0; i < elements; i++)
        write((uint16_t)(i + 1), 1, in + i * GROUP_KEY_SIZE);
}

// ---------------------------------------------------------------- PID 83

WhiteLists::List* WhiteLists::find(uint16_t user, uint32_t now)
{
    for (List& list : _lists)
    {
        if (!list.used)
            continue;

        if (now - list.since >= TEMP_MS)
        {
            list.used = false;
            continue;
        }

        if (list.user == user)
            return &list;
    }
    return nullptr;
}

bool WhiteLists::contains(const List& list, uint16_t address) const
{
    for (uint8_t i = 0; i < list.ranges; i++)
    {
        const Range& r = list.range[i];
        if (address >= r.start && (uint32_t)address < (uint32_t)r.start + r.count)
            return true;
    }
    return false;
}

uint8_t WhiteLists::add(uint16_t own, uint16_t user, uint16_t start, uint16_t count, uint32_t now)
{
    if (user == own)
        return E_DATA_VOID;

    // Allowed and without effect - in particular no timer is (re)started.
    if (count == 0)
        return E_SUCCESS;

    // Implementation specific (2.3.1.3.5.1): a range beyond 15.15.255 is void.
    if ((uint32_t)start + count > 0x10000)
        return E_DATA_VOID;

    if (own >= start && (uint32_t)own < (uint32_t)start + count)
        return E_DATA_VOID;

    List* list = find(user, now);

    if (list == nullptr)
    {
        for (List& candidate : _lists)
        {
            if (!candidate.used)
            {
                list = &candidate;
                break;
            }
        }

        if (list == nullptr)
            return E_COMMAND_IMPOSSIBLE;

        memset(list, 0, sizeof(*list));
        list->used = true;
        list->user = user;
    }
    else if (list->ranges == MAX_RANGES)
    {
        return E_COMMAND_IMPOSSIBLE;
    }

    list->range[list->ranges++] = {start, count};
    list->since = now;
    return E_SUCCESS;
}

void WhiteLists::revoke(uint16_t user)
{
    for (List& list : _lists)
    {
        if (list.used && list.user == user)
            list.used = false;
    }
}

uint8_t WhiteLists::retrigger(uint16_t user, uint32_t now)
{
    List* list = find(user, now);
    if (list == nullptr)
        return E_DATA_VOID;

    list->since = now;
    return E_SUCCESS;
}

bool WhiteLists::check(uint16_t user, uint16_t start, uint16_t count, uint32_t now)
{
    List* list = find(user, now);
    if (list == nullptr)
        return false;

    if (count == 0)
        return true;

    // Walk the searched range from range to range instead of address by
    // address: a count of up to 65535 is allowed.
    uint32_t position = start;
    uint32_t end      = (uint32_t)start + count;

    while (position < end)
    {
        uint32_t reach = position;

        for (uint8_t i = 0; i < list->ranges; i++)
        {
            const Range& r = list->range[i];
            uint32_t     rangeEnd = (uint32_t)r.start + r.count;
            if (position >= r.start && position < rangeEnd && rangeEnd > reach)
                reach = rangeEnd;
        }

        if (reach == position)
            return false;
        position = reach;
    }
    return true;
}

bool WhiteLists::allows(uint16_t a, uint16_t b, uint32_t now)
{
    List* list = find(a, now);
    if (list != nullptr && contains(*list, b))
        return true;

    list = find(b, now);
    return list != nullptr && contains(*list, a);
}

void WhiteLists::clear()
{
    memset(_lists, 0, sizeof(_lists));
}

uint8_t WhiteLists::active(uint32_t now)
{
    uint8_t n = 0;
    for (List& list : _lists)
    {
        if (list.used && now - list.since < TEMP_MS)
            n++;
    }
    return n;
}

// ---------------------------------------------------------------- Proxy

bool Proxy::routeIndividual(uint8_t fromSide, uint16_t source, uint16_t destination, uint32_t now)
{
    uint8_t toSide = fromSide == PRIMARY ? SECONDARY : PRIMARY;

    if ((_ptp[toSide] & 0x03) == ROUTE_RULE)
        return true;

    // BLOCK, or a reserved value: only what a white list opened.
    return _white.allows(source, destination, now);
}

bool Proxy::routeBroadcast(uint8_t fromSide, bool systemBroadcast, uint32_t now)
{
    uint8_t toSide = fromSide == PRIMARY ? SECONDARY : PRIMARY;

    if ((_bc[toSide] & 0x03) == ROUTE_RULE)
        return true;

    return bcTempEnabled(toSide, systemBroadcast, now);
}

void Proxy::expireBc(uint8_t side, uint32_t now)
{
    BcTemp& t = _bcTemp[side];
    if ((t.bc || t.sbc) && now - t.since >= TEMP_MS)
    {
        t.bc  = false;
        t.sbc = false;
    }
}

bool Proxy::bcTempEnabled(uint8_t side, bool systemBroadcast, uint32_t now)
{
    expireBc(side, now);
    const BcTemp& t = _bcTemp[side];

    if (systemBroadcast && hasSystemBroadcast(side))
        return t.sbc;
    return t.bc;
}

void Proxy::bcTemp(uint8_t side, bool command, const uint8_t* data, uint8_t length,
                   uint8_t* result, uint8_t& resultLength, uint32_t now)
{
    if (length < 2)
    {
        result[0]    = E_COMMAND_INVALID;
        resultLength = 1;
        return;
    }

    uint8_t service = data[1];
    result[1]    = service;
    resultLength = 2;

    if (service > 1 || (service == 1 && !hasSystemBroadcast(side)))
    {
        result[0] = E_COMMAND_INVALID;
        return;
    }

    expireBc(side, now);
    BcTemp& t = _bcTemp[side];
    bool&   mode = service == 0 ? t.bc : t.sbc;

    if (!command)
    {
        result[0]    = E_SUCCESS;
        result[2]    = mode ? 1 : 0;
        resultLength = 3;
        return;
    }

    if (length < 3 || data[2] > 1)
    {
        // A failed command starts no timer (2.3.1.4.6).
        result[0] = E_DATA_VOID;
        return;
    }

    mode = data[2] == 1;

    // One timer for both modes of this side; enabling either restarts it.
    if (mode)
        t.since = now;

    result[0] = E_SUCCESS;
}

void Proxy::ptpTemp(bool command, const uint8_t* data, uint8_t length,
                    uint8_t* result, uint8_t& resultLength, uint16_t own, uint32_t now)
{
    if (length < 2)
    {
        result[0]    = E_COMMAND_INVALID;
        resultLength = 1;
        return;
    }

    uint8_t service = data[1];
    result[1]    = service;
    resultLength = 2;

    auto word = [&](uint8_t at) -> uint16_t { return (uint16_t)((data[at] << 8) | data[at + 1]); };

    if (!command)
    {
        if (service != 0)
        {
            result[0] = E_COMMAND_INVALID;
            return;
        }
        if (length < 8)
        {
            result[0] = E_DATA_VOID;
            return;
        }

        result[0]    = E_SUCCESS;
        result[2]    = _white.check(word(2), word(4), word(6), now) ? 1 : 0;
        resultLength = 3;
        return;
    }

    switch (service)
    {
        case 0:
            result[0] = length < 8 ? E_DATA_VOID : _white.add(own, word(2), word(4), word(6), now);
            break;

        case 1:
            if (length < 4)
            {
                result[0] = E_DATA_VOID;
                break;
            }
            _white.revoke(word(2));
            result[0] = E_SUCCESS;
            break;

        case 2:
            result[0] = length < 4 ? E_DATA_VOID : _white.retrigger(word(2), now);
            break;

        default:
            result[0] = E_COMMAND_INVALID;
            break;
    }
}

bool Proxy::masterReset(uint8_t eraseCode)
{
    // The temporary state goes with every erase code and every restart.
    _white.clear();
    memset(_bcTemp, 0, sizeof(_bcTemp));

    bool changed = false;

    if (eraseCode == 0x02)  // reset to default state
    {
        _ptp[PRIMARY] = _ptp[SECONDARY] = ROUTE_RULE;
        _bc[PRIMARY]  = _bc[SECONDARY]  = ROUTE_RULE;
        changed = true;
    }

    if (eraseCode == 0x02 || eraseCode == 0x07)  // ... with or without the address
    {
        _keys.clear();
        changed = true;
    }

    return changed;
}

} // namespace knxproxy
