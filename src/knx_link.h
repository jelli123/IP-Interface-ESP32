/*
 *  knx_link.h - KNX stack wiring and TP1 link supervision.
 */
#pragma once

#include <stdint.h>

class HardwareSerial;

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

    /**
     * Read the tunnel addresses stored in the device.
     *
     * These are the addresses ETS and other clients appear under when they
     * The stack keeps them in PID_ADDITIONAL_INDIVIDUAL_ADDRESSES of the
     * KNXnet/IP parameter object; they are derived from the device address on
     * the first connection and can be overwritten from the ETS connection
     * manager afterwards.
     *
     * Read only, so calling it from the web server task is fine - same as
     * configured() and individualAddress(). A tunnel connecting at that very
     * moment can make the result one refresh stale, which is harmless.
     *
     * @param out      receives up to @p max addresses
     * @param max      capacity of @p out
     * @return number of addresses written
     */
    uint8_t tunnelAddresses(uint16_t* out, uint8_t max) const;

    /**
     * Forward group telegrams even though no filter table was downloaded.
     *
     * A coupler without an ETS download blocks every group telegram, which is
     * correct per spec but leaves the device useless as a plain TP-to-IP
     * gateway. With this on it forwards everything instead.
     *
     * Not KNX conformant: in an installation with a second coupler or another
     * IP gateway this invites telegram loops. Safe where this device is the
     * only path between the line and IP.
     *
     * Has no effect once ETS has downloaded a filter table - that one wins.
     *
     * Persisted, so it survives a restart.
     */
    void routeUnfiltered(bool enable);
    bool routeUnfiltered() const;

    /**
     * Read a fixed IP configuration programmed by ETS.
     *
     * The KNXnet/IP parameter object carries PID_IP_ASSIGNMENT_METHOD next to
     * the address, mask and gateway. Bit 0 means "manual": the installer
     * entered an address in the ETS device properties and expects the device
     * to use it. Everything else, DHCP included, leaves the addressing to us.
     *
     * Read only, safe from the web server task.
     *
     * @param ip    receives the address in host byte order
     * @param mask  receives the subnet mask
     * @param gw    receives the default gateway, 0 when none was entered
     * @return true if ETS asked for a fixed address
     */
    bool etsIpConfig(uint32_t& ip, uint32_t& mask, uint32_t& gw) const;

    /**
     * Read the group addresses the filter table lets through.
     *
     * Only meaningful after an ETS download - an unprogrammed coupler has no
     * table and blocks everything. Walking the whole address space costs a
     * few tens of milliseconds, so this is meant for an explicit request,
     * not for the status poll.
     *
     * @param out    receives up to @p max addresses, ascending
     * @param max    capacity of @p out
     * @param total  receives the full count, which may exceed @p max
     * @return true if a filter table is loaded
     */
    bool filterTable(uint16_t* out, uint16_t max, uint16_t& total) const;

    /** @return true if programming mode is active */
    /**
     * The name this device answers with during KNXnet/IP discovery.
     *
     * Written by ETS during a download, which is why it usually reads like a
     * catalogue entry. Before that it holds whatever the firmware announced.
     * This is the name ETS shows when picking an interface - not the device
     * name from the dashboard.
     */
    String friendlyName() const;

    bool progMode() const;
    /**
     * Request a programming mode change.
     *
     * Safe to call from the web server task: the change is queued and applied
     * in loop(). Writing to the stack from another task races knx.loop().
     */
    void requestProgMode(bool active);

    /**
     * Wipe the KNX configuration back to factory state.
     *
     * Clears the stack's non volatile area: individual address, the tunnel
     * addresses in PID_ADDITIONAL_INDIVIDUAL_ADDRESSES and every table object.
     * WiFi credentials and the hardware profile live in their own NVS
     * namespaces and are left alone.
     *
     * The reason this exists: the stack derives the tunnel addresses from the
     * device address once, on the first tunnel connection, and then keeps
     * them. Program the device afterwards and its tunnels stay behind in the
     * old line - ETS then sees two devices in different lines and refuses to
     * download. Short of erasing the whole flash there was no way back.
     *
     * Requires a restart afterwards; the stack caches the tables in RAM.
     *
     * @return true if the area was cleared
     */
    bool resetConfiguration();

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

    /**
     * Hand the KNX UART over to someone else, closing the port.
     *
     * Programming the SB-Interface talks to the LPC's ROM bootloader at
     * 115200 baud 8N1 on the very port the stack drives at 19200 8E1, so the
     * two cannot overlap. Only the main task may touch the stack, which is
     * why this asks rather than acts: the flag is picked up in loop(), and
     * the call returns once loop() has confirmed it stopped.
     *
     * Callable from any task except the main one - it would deadlock there.
     *
     * @param timeoutMs how long to wait for the main task to acknowledge
     * @return true once the stack is idle and the port is closed
     */
    bool suspend(uint32_t timeoutMs = 3000);

    /** Resume, re-running the TP-UART reset handshake from the main task. */
    void resume();

    bool suspended() const { return _suspended; }

    /** The port the stack uses. Null until begin() has run. */
    HardwareSerial* uart() const;

private:
    static void activityTrampoline(uint8_t info);
    void applyIdentity();
    void onActivity(uint8_t info);
    void updateBusLoad();
    void superviseTpLink();

    Stats _stats = {};
    char  _selfTest[48] = "pending";

    volatile bool _progModePending = false;
    volatile bool _progModeValue   = false;

    volatile bool _suspendRequest = false;
    volatile bool _suspended      = false;

    uint32_t _lastBusLoadWindow = 0;
    uint32_t _framesInWindow    = 0;
    uint32_t _lastLinkCheck     = 0;
};

extern KnxLink knxLink;
