/*
 *  knx_link.h - KNX stack wiring and TP1 link supervision.
 */
#pragma once

#include <stdint.h>

/**
 * Owns the KNX stack and the serial link to the Selfbus SB-Interface.
 *
 * The SB-Interface runs the TP-UART 2 emulator, so from the stack's point of
 * view this is a plain TP-UART - no NCN512x extensions, no analog registers,
 * no baud rate switching.
 */
class KnxLink
{
public:
    /** Aggregated bus statistics for the dashboard. */
    struct Stats
    {
        uint32_t tpRxFrames;      //!< frames received from TP1 and processed
        uint32_t tpRxIgnored;     //!< valid TP1 frames not addressed to us
        uint32_t tpRxInvalid;     //!< TP1 frames with a bad checksum or length
        uint32_t tpTxFrames;      //!< frames handed to the TP-UART
        uint32_t tpTxProcessed;   //!< frames confirmed by the TP-UART
        uint32_t ipRxFrames;      //!< frames received from KNXnet/IP
        uint32_t ipTxFrames;      //!< frames sent to KNXnet/IP
        uint16_t busLoadPermille; //!< TP1 utilisation over the last second
    };

    /**
     * Bring up the UART and the KNX stack.
     *
     * @return true if the TP-UART answered the initial reset
     */
    bool begin();

    /** Drive the stack. Must be called from the main task only. */
    void loop();

    /**
     * Close and reopen the KNXnet/IP multicast socket.
     *
     * The socket is created when the stack is enabled, which happens before
     * the network interface has an address. An IGMP join binds to whatever is
     * the default netif at that moment, so the membership has to be renewed
     * once the interface is really up. Harmless at startup - there are no
     * tunnel connections to lose yet.
     */
    void restartIpLayer();

    /** @return true if the TP-UART emulator is answering */
    bool tpConnected() const;

    /** @return true if the device has been programmed by ETS */
    bool configured() const;

    /** @return the individual address assigned by ETS */
    uint16_t individualAddress() const;

    /** @return true if programming mode is active */
    bool progMode() const;

    /**
     * Request a programming mode change.
     *
     * Safe to call from the web server task: the change is queued and applied
     * in loop(). Writing to the stack from another task races knx.loop().
     */
    void requestProgMode(bool active);

    /**
     * Send a GroupValueWrite telegram onto TP1, the routing multicast and all
     * open tunnels.
     *
     * A 091A coupler has no group objects, so this bypasses the application
     * layer and hands the frame straight to both data link layers. Sender is
     * our own individual address.
     *
     * Must be called from the main task only - it touches the KNX stack.
     *
     * @param groupAddress destination group address, 0 is rejected
     * @param payload      APDU payload, 1..14 octets
     * @param length       number of payload octets
     * @return true if the frame was handed to at least one data link layer
     */
    bool sendGroupValue(uint16_t groupAddress, const uint8_t* payload, uint8_t length);

    const Stats& stats() const { return _stats; }

    /** Result of the boot time link check, for /api/status. */
    const char* selfTestResult() const { return _selfTest; }

private:
    static void activityTrampoline(uint8_t info);
    void onActivity(uint8_t info);
    void updateBusLoad();
    void superviseTpLink();

    Stats _stats = {};
    char  _selfTest[48] = "pending";

    volatile bool _progModePending = false;
    volatile bool _progModeValue   = false;

    uint32_t _lastBusLoadWindow = 0;
    uint32_t _framesInWindow    = 0;
    uint32_t _lastLinkCheck     = 0;
};

extern KnxLink knxLink;
