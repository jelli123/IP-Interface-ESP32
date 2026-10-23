/*
 *  knx_security_proxy.h - The Security Proxy of AN192 "Coupler security
 *  extensions" v06 on this router.
 *
 *  What it does
 *
 *    A group address can be secured with KNX Data Secure on one side of the
 *    router and plain on the other. ETS then loads the Security Proxy Group
 *    Key Table (PID 81) with the address, its key and which side is secure,
 *    and the proxy translates: a secured telegram from the secure side goes
 *    on decrypted, a plain one from the plain side goes on secured - from the
 *    router's own address, with its own sequence number. Anything else on
 *    such an address stops here (AN192 Table 8).
 *
 *    Point-to-point and broadcast telegrams can be kept from crossing onto a
 *    side (PID 80, 82). The tool then opens the way for itself for a minute
 *    at a time (PID 83, 84).
 *
 *  Where the pieces are
 *
 *    The logic is in knx_proxy_core.cpp, which knows nothing of the stack.
 *    This file stores the configuration in NVS, takes sequence numbers from
 *    the secure application layer and the last valid ones from the security
 *    object, and turns frames into frames. The primary side's properties sit
 *    in the stack's router object (patch 31 in scripts/patch_knx.py); mask
 *    091A has only that one, so the secondary side's second router object
 *    (AN192 2.3.1) comes from here and SbipBau lists it.
 *
 *  Primary is the IP side, secondary the TP side. Tunnel clients count as TP:
 *  their telegrams reach the TP line without crossing the coupler, as their
 *  addresses belong to it.
 */
#pragma once

#include <Arduino.h>
#include <knx/cemi_frame.h>
#include <knx/interface_object.h>
#include <knx/knx_types.h>

#include "knx_proxy_core.h"

class DeviceObject;
class RouterObject;
class SecureApplicationLayer;
class SecurityInterfaceObject;

class KnxSecurityProxy
{
public:
    struct Stats
    {
        uint32_t decrypted;
        uint32_t encrypted;
        uint32_t blocked;       //!< group telegrams stopped by Table 8
        uint32_t macFailures;
        uint32_t replays;
        uint32_t unknownSource; //!< sender not in the security IA table
        uint32_t ptpBlocked;
        uint32_t bcBlocked;
    };

    /** Load the configuration. Before the stack runs its first loop. */
    void begin(DeviceObject& device, RouterObject& primary, SecurityInterfaceObject& security,
               SecureApplicationLayer& secureLayer);

    /** Persist what changed. KNX task. */
    void loop();

    /** The secondary side's router object, for SbipBau. */
    InterfaceObject& secondaryRouterObject();

    // Called by the stack, see patch 31.
    NPDU*   route(AddressType addrType, uint16_t destination, bool systemBroadcast,
                  NPDU& npdu, uint16_t& source, bool fromPrimary);
    uint8_t propertyRead(uint8_t side, uint8_t pid, uint16_t start, uint8_t count, uint8_t* data);
    uint8_t propertyWrite(uint8_t side, uint8_t pid, uint16_t start, uint8_t count, const uint8_t* data);
    void    propertyFunction(uint8_t side, bool command, uint8_t pid, uint8_t* data, uint8_t length,
                             uint8_t* result, uint8_t& resultLength);

    /** AN192's master reset tables. */
    void masterReset(uint8_t eraseCode);

    /** PID 58 of the secondary router object: that of the first one. */
    uint8_t routingApdu(uint16_t start, uint8_t count, uint8_t* data);

    /** Forget the group keys, with the rest of the Secure configuration. */
    void clearKeys();

    /**
     * True when the last valid sequence numbers in the security object
     * changed and the last save is long enough ago. The caller saves the
     * object; see SAVE_INTERVAL_MS. Never with a FRAM, which holds them.
     */
    bool sequenceSaveDue();

    const Stats& stats() const { return _stats; }

    /** For the dashboard. */
    String json() const;

private:
    bool groupKeysApply() const;
    void restoreSequences();
    void persist();
    void report(const char* what, uint16_t source, uint16_t destination, bool group);

    knxproxy::Proxy _core;
    Stats           _stats = {};

    DeviceObject*            _device      = nullptr;
    RouterObject*            _primary     = nullptr;
    SecurityInterfaceObject* _security    = nullptr;
    SecureApplicationLayer*  _secureLayer = nullptr;

    CemiFrame _out{(uint8_t)0};  //!< the translated frame, valid until it is sent
    uint8_t   _buffer[256];

    bool     _dirty      = false;
    uint32_t _dirtyAt    = 0;
    bool     _seqDirty   = false;
    uint32_t _seqSavedAt = 0;
    uint32_t _reportedAt = 0;
};

extern KnxSecurityProxy knxSecurityProxy;
