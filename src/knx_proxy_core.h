/*
 *  knx_proxy_core.h - The Security Proxy of AN192 "Coupler security
 *  extensions" v06, without the stack around it.
 *
 *  A coupler with a Security Proxy translates group communication between a
 *  side where a group address is secured with KNX Data Secure and a side
 *  where it is plain, and it may block point-to-point and broadcast traffic
 *  onto a side unless the tool opened the way for a while:
 *
 *    PID 80  PID_PTP_CONFIG                    one per side
 *    PID 81  PID_SECURITY_PROXY_GRP_KEY_TABLE  primary router object only
 *    PID 82  PID_BC_CONFIG                     one per side
 *    PID 83  PID_PTP_ROUTING_TEMP              primary router object only
 *    PID 84  PID_BC_ROUTING_TEMP               one per side
 *
 *  Everything here is plain data and bytes in, bytes out: the decisions of
 *  AN192 2.2.2, the Data Secure frames of a group telegram, the white lists
 *  and the property formats. src/knx_security_proxy.cpp ties it to the
 *  stack, NVS and the sequence numbers; test/security_proxy_test.cpp checks
 *  it on the host.
 *
 *  Side 0 is the primary side (IP), side 1 the secondary one (TP).
 *
 *  Not here: the Security Proxy Zone Key Table (LTE mode, object type 7).
 *  The stack has no LTE mode and ETS does not fill that table (AN192
 *  3.5.2.1); LTE telegrams pass as before, except S-A_Sync, which may not
 *  cross a coupler at all.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace knxproxy
{

static const uint8_t PRIMARY   = 0;
static const uint8_t SECONDARY = 1;

// Security Settings of a group key table entry, AN192 Figure 25.
static const uint8_t SEC_AT_PRIMARY   = 0x80;
static const uint8_t SEC_AT_SECONDARY = 0x40;
static const uint8_t FLAG_CONF        = 0x02;
static const uint8_t FLAG_AUTH        = 0x01;

// PTP_ROUTE and BC_ROUTE, AN192 2.3.1.1.3 and 2.3.1.2.4.
static const uint8_t ROUTE_BLOCK = 0;
static const uint8_t ROUTE_RULE  = 1;  //!< KNX default

/** Elements of PID 81. The patch in scripts/patch_knx.py (31) announces the same. */
static const uint16_t MAX_GROUP_KEYS = 128;

/** Octets of one PID 81 element: GA, key, settings. */
static const uint8_t GROUP_KEY_SIZE = 19;

static const uint8_t MAX_WHITE_LISTS = 8;   //!< user addresses at a time
static const uint8_t MAX_RANGES      = 8;   //!< partner ranges per user

/** Life of a white list and of a temporary broadcast mode, 2.3.1.3.5.1 and 2.3.1.4.1. */
static const uint32_t TEMP_MS = 60000;

// Return codes of the function properties (03_03_07, AN192 Table 7).
static const uint8_t E_SUCCESS           = 0x00;
static const uint8_t E_COMMAND_INVALID   = 0xF2;
static const uint8_t E_COMMAND_IMPOSSIBLE = 0xF3;
static const uint8_t E_DATA_VOID         = 0xF8;

struct GroupKey
{
    uint16_t ga;
    uint8_t  key[16];
    uint8_t  settings;
};

/** What a TPDU carries, as far as the proxy cares. */
enum Kind : uint8_t
{
    PLAIN,
    SECURE_DATA,   //!< S-A_Data
    SECURE_SYNC,   //!< S-A_Sync_Req or S-A_Sync_Res
    SECURE_OTHER,  //!< APCI 03F1h with a service AN158 does not define, or too short
};

/** What to do with a group telegram the coupler would route. */
enum Action : uint8_t
{
    FORWARD,   //!< unchanged
    BLOCK,
    DECRYPT,   //!< forward only the plain telegram
    ENCRYPT,   //!< forward only the secured telegram, from the proxy's address
};

/**
 * @param tpdu  the TPDU, starting at the TPCI octet
 * @param length its octets, i.e. the L field plus one
 */
Kind classify(const uint8_t* tpdu, size_t length);

/** Octets a secured group TPDU adds to the plain one: TPCI/APCI 03F1h, SCF, sequence, MAC. */
static const size_t SECURE_OVERHEAD = 2 + 1 + 6 + 4;

/**
 * Verify and, with confidentiality, decrypt an S-A_Data group TPDU
 * (AN158, as the stack's SecureApplicationLayer does it).
 *
 * @param plain  receives the plain TPDU, length - SECURE_OVERHEAD octets
 * @param seq    receives the sequence number of the telegram
 * @return the length of the plain TPDU, 0 if the MAC does not match
 */
size_t open(const uint8_t key[16], uint16_t source, uint16_t group,
            const uint8_t* tpdu, size_t length, uint8_t* plain, uint64_t& seq);

/**
 * Secure a plain group TPDU as S-A_Data.
 *
 * @param secure receives length + SECURE_OVERHEAD octets
 * @return the length of the secured TPDU
 */
size_t seal(const uint8_t key[16], uint16_t source, uint16_t group, uint64_t seq, bool conf,
            const uint8_t* plain, size_t length, uint8_t* secure);

/**
 * AN192 2.2.2.3 and Table 8, for a group telegram the routing rules would
 * forward.
 *
 * @param entry  the GA's entry of the group key table, null if it has none
 *               or the table does not count (not loaded)
 * @param scf    the SCF of a secured telegram, otherwise unused
 */
Action groupAction(const GroupKey* entry, uint8_t fromSide, Kind kind, uint8_t scf);

/** PID 81, the Security Proxy Group Key Table. */
class GroupKeyTable
{
public:
    uint16_t count() const { return _count; }
    const GroupKey& at(uint16_t index) const { return _entries[index]; }

    /** First entry for @p ga, or null. */
    const GroupKey* find(uint16_t ga) const;

    /** Property semantics: start 0 is the element count. */
    uint8_t read(uint16_t start, uint8_t count, uint8_t* data) const;
    uint8_t write(uint16_t start, uint8_t count, const uint8_t* data);

    void clear();

    /** Raw elements as the property carries them, for NVS. */
    size_t serialize(uint8_t* out, size_t max) const;
    void deserialize(const uint8_t* in, size_t length);

private:
    GroupKey _entries[MAX_GROUP_KEYS] = {};
    uint16_t _count = 0;
};

/**
 * The Temporary Unicast White Lists of PID 83 (2.3.1.3), one per user
 * address. Times in milliseconds of any free-running clock.
 */
class WhiteLists
{
public:
    uint8_t add(uint16_t own, uint16_t user, uint16_t start, uint16_t count, uint32_t now);
    void    revoke(uint16_t user);
    uint8_t retrigger(uint16_t user, uint32_t now);

    /** ReadServiceID 00h: are all of start .. start + count - 1 partners of user? */
    bool check(uint16_t user, uint16_t start, uint16_t count, uint32_t now);

    /** 2.2.2.5: may a and b talk across the coupler? */
    bool allows(uint16_t a, uint16_t b, uint32_t now);

    void    clear();
    uint8_t active(uint32_t now);

private:
    struct Range
    {
        uint16_t start;
        uint16_t count;
    };

    struct List
    {
        bool     used;
        uint16_t user;
        uint32_t since;
        uint8_t  ranges;
        Range    range[MAX_RANGES];
    };

    List* find(uint16_t user, uint32_t now);
    bool  contains(const List& list, uint16_t address) const;

    List _lists[MAX_WHITE_LISTS] = {};
};

/** Everything of the proxy that is not the stack. */
class Proxy
{
public:
    // PID 80 and 82: the whole octet is kept, only bits 0-1 count.
    uint8_t ptpConfig(uint8_t side) const { return _ptp[side]; }
    uint8_t bcConfig(uint8_t side) const { return _bc[side]; }
    void    ptpConfig(uint8_t side, uint8_t value) { _ptp[side] = value; }
    void    bcConfig(uint8_t side, uint8_t value) { _bc[side] = value; }

    GroupKeyTable& groupKeys() { return _keys; }
    const GroupKeyTable& groupKeys() const { return _keys; }
    WhiteLists& whiteLists() { return _white; }

    /** 2.2.2.5, for an individually addressed telegram the routing rules would forward. */
    bool routeIndividual(uint8_t fromSide, uint16_t source, uint16_t destination, uint32_t now);

    /** 2.2.2.6, for a (system) broadcast the routing rules would forward. */
    bool routeBroadcast(uint8_t fromSide, bool systemBroadcast, uint32_t now);

    /**
     * PID 83, the data of an A_FunctionPropertyCommand / _StateRead from the
     * reserved octet on; the result from the return code on.
     */
    void ptpTemp(bool command, const uint8_t* data, uint8_t length,
                 uint8_t* result, uint8_t& resultLength, uint16_t own, uint32_t now);

    /** PID 84 of one side. */
    void bcTemp(uint8_t side, bool command, const uint8_t* data, uint8_t length,
                uint8_t* result, uint8_t& resultLength, uint32_t now);

    /**
     * Whether a side has a system broadcast mode of its own. TP1 has not
     * (2.7.2.1): a system broadcast travels there as a broadcast, and the
     * BC mode covers it.
     */
    static bool hasSystemBroadcast(uint8_t side) { return side == PRIMARY; }

    bool bcTempEnabled(uint8_t side, bool systemBroadcast, uint32_t now);

    /**
     * What a master reset with this erase code leaves behind (the tables in
     * 2.3.1.1.6, 2.3.1.2.6, 2.3.1.3.8, 2.3.1.4.7, 2.3.1.5.6). Returns true if
     * something that is kept across restarts changed.
     */
    bool masterReset(uint8_t eraseCode);

private:
    struct BcTemp
    {
        bool     bc;
        bool     sbc;
        uint32_t since;
    };

    void expireBc(uint8_t side, uint32_t now);

    uint8_t       _ptp[2] = {ROUTE_RULE, ROUTE_RULE};
    uint8_t       _bc[2]  = {ROUTE_RULE, ROUTE_RULE};
    BcTemp        _bcTemp[2] = {};
    GroupKeyTable _keys;
    WhiteLists    _white;
};

} // namespace knxproxy
