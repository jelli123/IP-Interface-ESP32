/*
 *  knxip_secure.h - KNXnet/IP Secure: the configuration ETS loads, the
 *  secure sessions, and the timer of secure routing.
 *
 *  Reference: 03_08_09 "KNX IP Secure" v01.01.02 of the KNX Standard v3.0.
 *  Section numbers below refer to it.
 *
 *  Configuration (2.3.1)
 *
 *    ETS writes it into the KNXnet/IP parameter object (PID 79, 91-97). The
 *    stack only has callback properties there (patch 19); the values live
 *    here and in NVS, outside the stack's memory image, so that a device
 *    already programmed keeps a valid image across this update. In delivery
 *    state the device authentication code is the FDSK, user 1 has the empty
 *    password, the backbone key is zero and nothing is secured.
 *
 *  Sessions (2.2.3)
 *
 *    Over TCP only (2.2.3.3, 2.2.3.6.4). X25519, session key =
 *    SHA-256(shared secret)[0..15]; the SESSION_RESPONSE proves the device
 *    knows the device authentication code, SESSION_AUTHENTICATE that the
 *    client knows a user's password. User 1 is the management user. The
 *    state machine of 2.2.3.5.2: 10 s to authenticate, 60 s without a valid
 *    frame closes an authenticated session. One TCP connection may carry
 *    several sessions (3/8/2 8.4.3.5).
 *
 *  Secure routing (2.2.2)
 *
 *    A shared 48 bit millisecond timer is the sequence information of every
 *    routing frame. It never runs backwards, not even across a power cycle
 *    (2.2.4.2), and restarts from 0 with a new backbone key. The state
 *    machine of 2.2.2.3.2, including the acquisition of an authentic timer
 *    after the start (2.2.2.3.2.8).
 *
 *  Transport-agnostic: the shim (src/knxip_shim.cpp) feeds frames in and
 *  passes what comes out to UDP or TCP.
 */
#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "knxip_secure_frames.h"

#ifndef KNX_TUNNELING
#define KNX_TUNNELING 1
#endif

/** Service families as numbered in the DIBs and in PID 94. */
enum KnxIpFamily : uint8_t
{
    FAMILY_CORE        = 0x02,
    FAMILY_DEVICE_MGMT = 0x03,
    FAMILY_TUNNELLING  = 0x04,
    FAMILY_ROUTING     = 0x05,
    FAMILY_SECURITY    = 0x09,
};

class KnxIpSecure
{
public:
    static const uint8_t MAX_USERS      = KNX_TUNNELING + 1; //!< management user + one per tunnel
    static const uint8_t MAX_USER_SLOTS = 2 * KNX_TUNNELING;
    static const uint8_t MAX_SESSIONS   = 4;
    static const uint8_t NO_SESSION     = 0xFF;

    struct Session
    {
        bool     used;
        bool     authenticated;
        uint16_t id;
        uint8_t  user;
        int8_t   tcp;           //!< the connection it belongs to
        uint8_t  key[16];
        uint8_t  xorKeys[32];
        uint64_t sendSequence;
        uint64_t nextReceive;   //!< lowest sequence still acceptable
        uint32_t lastValid;     //!< millis() of the last valid frame
    };

    /**
     * Sends a frame. Set by the shim: tcp >= 0 selects a TCP connection,
     * -1 the routing multicast.
     */
    typedef void (*SendFn)(int8_t tcp, const uint8_t* frame, size_t length);

    /** A session ended; @p status is what the client was told. */
    typedef void (*SessionEndFn)(uint8_t index, uint8_t status);

    void begin(SendFn send, SessionEndFn sessionEnd);

    /** @param networkUp secure routing waits for an address to send from */
    void loop(bool networkUp);

    // ---- configuration ----------------------------------------------------

    /** Property callbacks, see patch 19. */
    uint8_t propertyRead(uint8_t pid, uint16_t start, uint8_t count, uint8_t* data);
    uint8_t propertyWrite(uint8_t pid, uint16_t start, uint8_t count, const uint8_t* data);
    void    propertyFunction(bool command, uint8_t pid, uint8_t* data, uint8_t length,
                             uint8_t* result, uint8_t& resultLength);

    /** Is a family in PID 94? */
    bool secured(KnxIpFamily family) const;
    uint16_t securedFamilies() const { return _cfg.securedFamilies; }

    bool hasBackboneKey() const { return _cfg.flags & FLAG_BACKBONE; }
    bool authCodeFromEts() const { return _cfg.flags & FLAG_AUTH; }
    uint8_t passwordCount() const { return _cfg.passwordCount; }
    uint8_t userSlotCount() const { return _cfg.userCount; }
    uint16_t latencyToleranceMs() const { return _cfg.latencyMs; }

    /** Secure routing in effect: routing secured and a key to do it with. */
    bool routingSecure() const { return secured(FAMILY_ROUTING) && hasBackboneKey(); }

    /**
     * May @p user connect a tunnel over PID 53 entry @p additional
     * (1-based)? Only asked when tunnelling is secured; user 1 always may
     * (2.3.1.8).
     */
    bool userMayUseAddress(uint8_t user, uint8_t additional) const;

    /** Back to delivery state (2.3.1.2-2.3.1.8). The FDSK is not touched. */
    void clearConfiguration();

    // ---- sessions ---------------------------------------------------------

    /** A SESSION_REQUEST that came over TCP connection @p tcp. */
    void sessionRequest(const uint8_t* frame, size_t length, int8_t tcp);

    /**
     * Unwrap a session frame from TCP connection @p tcp.
     *
     * Handles SESSION_AUTHENTICATE and SESSION_STATUS itself. Anything else
     * from an authenticated session comes back in @p inner for the stack.
     *
     * @return the session index, or NO_SESSION if nothing is to be passed on
     */
    uint8_t sessionUnwrap(const uint8_t* frame, size_t length, int8_t tcp,
                          uint8_t* inner, size_t& innerLength);

    /** Wrap a frame for a session and send it. */
    bool sessionSend(uint8_t index, const uint8_t* frame, size_t length);

    const Session& session(uint8_t index) const { return _sessions[index]; }

    /** Is @p index a session of TCP connection @p tcp? */
    bool sessionOn(uint8_t index, int8_t tcp) const;

    /** Sessions a TCP connection carries. */
    uint8_t sessionsOn(int8_t tcp) const;

    /** Drop the sessions of a TCP connection that has gone (8.4.3.5). */
    void tcpClosed(int8_t tcp);

    /** Close a session, telling the client. */
    void sessionClose(uint8_t index, uint8_t status);

    uint8_t sessionCount() const;

    // ---- routing ----------------------------------------------------------

    /**
     * Check and unwrap a routing SECURE_WRAPPER (session 0).
     * @return true if @p inner holds a valid routing frame for the stack
     */
    bool routingUnwrap(const uint8_t* frame, size_t length, uint8_t* inner, size_t& innerLength);

    /**
     * Wrap a routing frame for the backbone.
     * @return length of the wrapper, 0 while the timer is not authentic yet
     */
    size_t routingWrap(const uint8_t* frame, size_t length, uint8_t* out);

    /** A TIMER_NOTIFY from the backbone. */
    void timerNotify(const uint8_t* frame, size_t length);

    uint64_t timerValue() const;
    bool timekeeper() const { return _timekeeper; }
    bool timerSynced() const { return _authentic; }

    /** Counters for the dashboard. */
    struct Stats
    {
        uint32_t sessionsOpened;
        uint32_t authFailures;
        uint32_t macFailures;
        uint32_t replays;
        uint32_t routingIn;
        uint32_t routingOut;
        uint32_t routingStale;
        uint32_t timerNotifies;
        uint32_t plainRefused;
    };
    const Stats& stats() const { return _stats; }
    void countPlainRefused() { _stats.plainRefused++; }

    /** The device's KNX serial number, sender of every wrapper. */
    void serial(const uint8_t sn[6]) { memcpy(_serial, sn, 6); }

private:
    enum : uint8_t
    {
        FLAG_BACKBONE = 0x01,
        FLAG_AUTH     = 0x02, //!< written by ETS; without it the FDSK applies
    };

    struct Config
    {
        uint8_t  version;
        uint8_t  flags;
        uint8_t  backboneKey[16];
        uint8_t  deviceAuth[16];
        uint8_t  passwordCount;
        uint8_t  passwords[MAX_USERS][16];
        uint16_t securedFamilies;
        uint16_t latencyMs;
        uint8_t  syncFraction;           //!< PDT_SCALING, 255 = 100 %
        uint8_t  tunnelAddrCount;
        uint8_t  tunnelAddrs[KNX_TUNNELING];
        uint8_t  userCount;
        uint8_t  users[MAX_USER_SLOTS][2]; //!< user id, tunnelling address index
    };

    struct ArrayProperty
    {
        uint8_t* data;
        uint8_t* count;
        uint8_t  size;
        uint8_t  max;
        bool     secret; //!< never readable
    };

    bool describe(uint8_t pid, ArrayProperty& out);
    void loadConfig();
    void markDirty();
    const uint8_t* authCode() const;
    void authenticate(uint8_t index, const uint8_t* inner, size_t length);
    void sendStatus(uint8_t index, uint8_t status);
    void endSession(uint8_t index, uint8_t status);

    // Timer, all in milliseconds.
    uint64_t now() const;
    uint32_t syncTolerance() const;
    void setTimer(uint64_t value);
    void persistTimer(bool force);
    void restartSync(bool powerUp);
    void reschedule(bool update, uint16_t tag = 0, const uint8_t* serial = nullptr);
    void sendTimerNotify(uint16_t tag, const uint8_t* serial);
    void firstSyncEvent();
    bool timerCheck(uint64_t received, uint16_t tag, const uint8_t* serial, bool fromNotify);

    Config   _cfg = {};
    uint8_t  _keyCount = 0; //!< element count of PID 91/92 for describe()
    bool     _dirty   = false;
    uint32_t _dirtyAt = 0;

    SendFn       _send       = nullptr;
    SessionEndFn _sessionEnd = nullptr;
    Session      _sessions[MAX_SESSIONS] = {};
    uint16_t     _lastSessionId = 0;
    uint8_t      _serial[6] = {0};

    int64_t  _timerOffset    = 0;
    uint64_t _timerPersisted = 0;    //!< what NVS holds
    bool     _routingOn      = false;
    bool     _poweredUp      = true;  //!< the next start is the one after power-up
    bool     _timekeeper     = false;
    bool     _authentic      = false;
    bool     _syncWaiting    = false; //!< acquisition deadline running
    uint32_t _syncDeadline   = 0;
    uint16_t _syncTag        = 0;
    bool     _initialPending = false;
    uint32_t _initialAt      = 0;
    bool     _schedUpdate    = false;
    uint16_t _updateTag      = 0;
    uint8_t  _updateSerial[6] = {0};
    uint32_t _notifyAt       = 0;
    bool     _notifyArmed    = false;

    Stats _stats = {};
};

extern KnxIpSecure knxIpSecure;
