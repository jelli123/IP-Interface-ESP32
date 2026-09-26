/*
 *  knxip_shim.h - Between the KNX stack and the network: TCP, KNXnet/IP
 *  Secure, and what a secure router announces.
 *
 *  The stack speaks plain KNXnet/IP over one UDP socket and nothing else. It
 *  reads through Platform::readBytesMultiCast() and writes through
 *  sendBytesUniCast() and sendBytesMultiCast(), all three virtual, and
 *  SbipPlatform routes them through here. So the stack keeps seeing plain
 *  UDP, while on the wire there is:
 *
 *  TCP (03_08_02 Core 8.4.3)
 *    A listener on port 3671. Frames are cut out of the stream by the length
 *    in their header and handed to the stack under a made-up sender:
 *    127.77.0.<connection>, port 3671 for plain frames and 40001 + <session>
 *    for frames of a secure session. The stack's answers go to that address,
 *    which says which connection and which session they belong to - one TCP
 *    connection may carry several sessions and plain connections at once
 *    (8.4.3.5). No TUNNELING_ACK and no sequence counter check over TCP
 *    (03_08_04 2.6.2): the stack's acknowledgements are dropped, and it is
 *    handed one for each tunnelling request it sends, because it counts
 *    them. A connection without any session or KNXnet/IP connection is
 *    closed after 10 s of silence.
 *
 *  Secure sessions and secure routing (03_08_09)
 *    Unwrapped before the stack sees a frame, wrapped after it answered; see
 *    src/knxip_secure.cpp. Sessions exist over TCP only. Every channel
 *    remembers its connection and session; a frame for it from anywhere else
 *    is dropped, and a CONNECTIONSTATE_REQUEST or DISCONNECT_REQUEST is
 *    answered with E_CONNECTION_ID (2.4.1).
 *
 *  Access control on CONNECT_REQUEST (03_08_09 2.2.1.4.2)
 *    A plain request for a secured family is refused with E_CONNECTION_TYPE.
 *    Inside a session: device management only for user 1 if it is secured;
 *    tunnels only over the addresses PID 97 gives the user if tunnelling is
 *    secured. An extended CRI naming an address is checked the same way:
 *    E_NO_TUNNELLING_ADDRESS, E_AUTHORISATION_ERROR, E_CONNECTION_IN_USE
 *    (03_08_02 table 8).
 *
 *  What the device announces
 *    Search and description answers: core version 2 while the TCP listener
 *    runs, the security family and the DIB of secured families in the
 *    extended search answer only (03_08_09 2.6.2), and the "authorized" bit
 *    of each tunnel slot for the user asking.
 */
#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "knxip_secure.h"

class KnxIpShim
{
public:
    static const uint8_t MAX_TCP = 4;

    /** The plain UDP side, provided by SbipPlatform. */
    struct Udp
    {
        int (*receive)(uint8_t* buffer, uint16_t max, uint32_t& ip, uint16_t& port);
        bool (*send)(uint32_t ip, uint16_t port, const uint8_t* buffer, uint16_t length);
        bool (*multicast)(const uint8_t* buffer, uint16_t length);
        uint32_t (*ownIp)(); //!< host order, 0 without an address
    };

    /** What the stack knows about its tunnel addresses, from src/knx_link.cpp. */
    struct Tunnels
    {
        uint16_t (*slotAddress)(uint8_t slot);  //!< PID 53 entry slot + 1
        bool (*addressInUse)(uint16_t address); //!< an open tunnel has it
    };

    void begin(const Udp& udp, const Tunnels& tunnels);

    /**
     * Start the TCP listener, unless it is switched off. With it the device
     * announces core version 2 and ETS 6 moves every tunnel to TCP; off, the
     * device is UDP only and announces core 1 - and there are no secure
     * sessions, which exist over TCP alone.
     */
    void startTcp();

    /** Stored setting, takes effect at the next start. */
    void tcpEnabled(bool enable);
    bool tcpEnabled() const { return _tcpSetting; }

    /** Main task, next to the stack. */
    void loop();

    // ---- the stack's side --------------------------------------------------

    int  receive(uint8_t* buffer, uint16_t max, uint32_t& ip, uint16_t& port);
    bool sendUnicast(uint32_t ip, uint16_t port, uint8_t* buffer, uint16_t length);
    bool sendMulticast(uint8_t* buffer, uint16_t length);

    /** Asked by the stack for every free slot while it handles a CONNECT_REQUEST. */
    bool slotAllowed(uint8_t slot) const;

    // ---- dashboard ---------------------------------------------------------

    bool tcpListening() const { return _listening; }
    uint8_t tcpConnections() const;

    struct Stats
    {
        uint32_t tcpAccepted;
        uint32_t tcpRefused;
        uint32_t tcpFrames;
        uint32_t plainConnectRefused;
        uint32_t plainRoutingDropped;
        uint32_t channelHijackDropped;
        uint32_t routingQueued;
    };
    const Stats& stats() const { return _stats; }

    /** JSON array of the open secure sessions and plain TCP connections. */
    String connectionsJson() const;

private:
    struct Source
    {
        int8_t   tcp;         //!< connection index, -1 for UDP
        uint32_t ip;
        uint16_t port;
        uint8_t  session;     //!< KnxIpSecure::NO_SESSION for a plain frame
        uint8_t  user;
        bool     injected;    //!< made up here, not from the network
        uint16_t requestedIa; //!< extended CRI, 0 if none
    };

    struct Tcp
    {
        bool     used;
        bool     closeSoon;   //!< close from loop(), not from inside a callback
        uint16_t fill;
        uint16_t skip;        //!< octets of an oversized frame still to discard
        uint32_t lastRx;
        uint32_t ip;
        uint16_t port;
        uint8_t  rx[600];
    };

    struct Channel
    {
        uint8_t id;           //!< 0 = unused
        int8_t  tcp;
        uint8_t session;
        uint8_t rxSeq;        //!< next sequence counter the stack expects (TCP)
    };

    // A few more than tunnels: a channel the stack dropped without telling
    // anyone stays until its id comes round again.
    static const uint8_t MAX_CHANNELS = KNX_TUNNELING + 4;

    static const uint16_t PLAIN_PORT   = 3671;
    static const uint16_t SESSION_PORT = 40001;

    // Incoming
    bool acceptTcp();
    bool readTcp(uint8_t index, uint8_t* frame, uint16_t& length);
    bool process(uint8_t* frame, uint16_t& length, uint16_t max, Source src);
    bool checkConnect(const uint8_t* frame, uint16_t length, Source& src);
    bool checkChannel(uint8_t* frame, uint16_t length, const Source& src);
    void refuseConnect(const uint8_t* frame, uint16_t length, const Source& src, uint8_t status);

    // Outgoing
    bool deliver(const Source& to, const uint8_t* frame, uint16_t length);
    uint16_t rewriteDescription(const uint8_t* frame, uint16_t length, const Source& to);
    bool tcpWrite(uint8_t index, const uint8_t* frame, uint16_t length);
    bool slotAuthorized(uint8_t slot, uint8_t session, uint8_t user) const;

    // Bookkeeping
    void closeTcp(uint8_t index);
    void sessionEnded(uint8_t session, uint8_t status);
    void releaseChannels(int8_t tcp, uint8_t session);
    bool tcpIdle(uint8_t index) const;
    void inject(const uint8_t* frame, uint16_t length);
    Channel* channel(uint8_t id);
    void flushRouting();

    static uint32_t pseudoIp(uint8_t tcp) { return 0x7F4D0000u | (uint32_t)(tcp + 1); }
    static int8_t tcpOf(uint32_t ip)
    {
        uint32_t low = ip & 0xFF;
        return ((ip & 0xFFFFFF00u) == 0x7F4D0000u && low >= 1 && low <= MAX_TCP) ? (int8_t)(low - 1) : -1;
    }
    static uint16_t pseudoPort(uint8_t session)
    {
        return session == KnxIpSecure::NO_SESSION ? PLAIN_PORT : (uint16_t)(SESSION_PORT + session);
    }
    static const uint32_t INJECTED_IP = 0x7F4DFFFFu;

    Udp      _udp = {};
    Tunnels  _tunnels = {};
    bool     _listening = false;
    bool     _tcpSetting = true;
    Tcp      _tcp[MAX_TCP] = {};
    Channel  _channels[MAX_CHANNELS] = {};
    Source   _ctx = {-1, 0, 0, KnxIpSecure::NO_SESSION, 0, false, 0};

    // Frames made up here for the stack: disconnects, acknowledgements.
    struct Injected
    {
        uint8_t length;
        uint8_t frame[24];
    };
    Injected _queue[8] = {};
    uint8_t  _queueHead = 0;
    uint8_t  _queueCount = 0;

    // Routing frames waiting for the secure timer.
    struct Pending
    {
        uint16_t length;
        uint8_t* frame;
    };
    Pending _pending[8] = {};
    uint8_t _pendingCount = 0;

    Stats _stats = {};
};

extern KnxIpShim knxIpShim;
