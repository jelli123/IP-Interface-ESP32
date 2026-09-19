/*
 *  ets_access.h - Which path may reprogram the device.
 *
 *  A coupler into an unprotected line - a garden, a garage, a car port - can
 *  be reprogrammed from that line. Anyone there who knows its address opens
 *  a connection, rewrites the filter table and has the inner line. The
 *  programming button is no protection: it only guards the assignment of the
 *  individual address, everything after that works without it.
 *
 *  So the firmware decides per path whether management may reach the device
 *  at all. Three paths, because they carry different risks:
 *
 *    TP        the line itself - the one that may be outside
 *    tunnel    a KNXnet/IP tunnel or configuration connection from the LAN
 *    routing   the KNXnet/IP multicast, which any other router in the LAN
 *              and anything that can send UDP to the group also reaches
 *
 *  Only management of THIS device is refused. Telegrams to other devices are
 *  forwarded as before; keeping those out is the job of the filter table and
 *  of the coupler's individual address setting in ETS - which is exactly
 *  what the lock stops anyone from undoing.
 *
 *  The setting lives in NVS and is changed only through the dashboard or a
 *  button, never over KNX: a lock that could be lifted over the path it
 *  guards would guard nothing. A KNX master reset leaves it alone.
 */
#pragma once

#include <stdint.h>

class EtsAccess
{
public:
    /** Where a management frame came from, as scripts/patch_knx.py reports it. */
    enum Path : uint8_t
    {
        PATH_ROUTING = 0,
        PATH_TP      = 1,
        PATH_TUNNEL  = 2,
        PATH_COUNT
    };

    /** Bits of the stored setting. */
    enum : uint8_t
    {
        ALLOW_TP      = 0x01,
        ALLOW_TUNNEL  = 0x02,
        ALLOW_ROUTING = 0x04,
        ALLOW_NET     = ALLOW_TUNNEL | ALLOW_ROUTING,
        ALLOW_ALL     = ALLOW_TP | ALLOW_NET
    };

    /** How long a temporary unlock lasts. */
    static const uint32_t UNLOCK_MS = 15UL * 60UL * 1000UL;

    /** Load the setting and install the hook. Before knxLink.begin(). */
    void begin();

    /** Ends a temporary unlock once it has run out. Main task. */
    void loop();

    /** What is stored, ALLOW_* bits. */
    uint8_t allowed() const { return _allowed; }

    /** What applies right now, a running temporary unlock included. */
    uint8_t effective() const { return unlocked() ? ALLOW_ALL : _allowed; }

    /** Store a new setting. Safe from the web server task. */
    void setAllowed(uint8_t mask);

    /**
     * Open every path for UNLOCK_MS, or end that early.
     *
     * Not stored: a restart ends it. While a tool keeps talking to the
     * device the time is extended, so a download that runs over the end is
     * not cut off halfway.
     */
    void unlock(bool enable);
    bool unlocked() const { return _unlockUntil != 0; }

    /** Seconds left of a temporary unlock, 0 when none runs. */
    uint32_t unlockRemaining() const;

    /** Button templates. Each one logs what it did. */
    void toggleTp();
    void toggleNet();
    void toggleAll();

    /** Refused management frames since the start. */
    uint32_t refused() const { return _refused; }

    /** Path and source of the last refusal; source 0 for a cEMI request. */
    uint8_t  lastRefusedPath() const { return _lastPath; }
    uint16_t lastRefusedSource() const { return _lastSource; }

    /** Seconds since the last refusal, or UINT32_MAX when there was none. */
    uint32_t lastRefusedAge() const;

    /** Short name of a path for the log and the dashboard. */
    static const char* pathName(uint8_t path);

    /** Called by the KNX stack for every frame that would manage us. */
    bool permits(uint8_t path, uint16_t source, bool individual);

private:
    void store(uint8_t mask, bool remember);
    void logSetting(const char* by) const;

    volatile uint8_t  _allowed     = ALLOW_ALL;
    volatile uint32_t _unlockUntil = 0; //!< millis(), 0 = no unlock running

    /*
     * What the network or the whole access was before a button switched it
     * off, so switching it back on restores "tunnel only" rather than
     * opening the multicast as well.
     */
    uint8_t _remembered = ALLOW_ALL;

    volatile uint32_t _refused    = 0;
    volatile uint8_t  _lastPath   = 0;
    volatile uint16_t _lastSource = 0;
    volatile uint32_t _lastAt     = 0;

    uint32_t _logSeen = 0;
    uint32_t _logAt   = 0;
};

extern EtsAccess etsAccess;
