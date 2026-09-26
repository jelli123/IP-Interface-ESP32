/*
 *  knxip_shim.cpp - Between the KNX stack and the network: TCP, KNXnet/IP
 *  Secure, and what a secure router announces.
 */

#include "knxip_shim.h"

#include <Network.h>
#include <NetworkClient.h>
#include <NetworkServer.h>
#include <Preferences.h>

#include "knxip_secure_frames.h"
#include "log_buffer.h"

KnxIpShim knxIpShim;

/*
 * Asked by the stack for every free tunnel slot while it handles a
 * CONNECT_REQUEST, see patch 20 in scripts/patch_knx.py.
 */
bool (*sbipTunnelSlotHook)(uint8_t slot) = nullptr;

static NetworkServer tcpServer(3671, KnxIpShim::MAX_TCP);
static NetworkClient tcpClients[KnxIpShim::MAX_TCP];

// Unwrapping and rewriting need room of their own: the stack's buffers are
// sized to the frame it built.
static uint8_t scratch[600];
static uint8_t outFrame[600];

// Service types of plain KNXnet/IP.
enum : uint16_t
{
    SEARCH_REQUEST           = 0x0201,
    SEARCH_RESPONSE          = 0x0202,
    DESCRIPTION_RESPONSE     = 0x0204,
    CONNECT_REQUEST          = 0x0205,
    CONNECT_RESPONSE         = 0x0206,
    CONNECTIONSTATE_REQUEST  = 0x0207,
    CONNECTIONSTATE_RESPONSE = 0x0208,
    DISCONNECT_REQUEST       = 0x0209,
    DISCONNECT_RESPONSE      = 0x020A,
    SEARCH_RESPONSE_EXT      = 0x020C,
    DEVICE_CONFIG_REQUEST    = 0x0310,
    DEVICE_CONFIG_ACK        = 0x0311,
    TUNNELING_REQUEST        = 0x0420,
    TUNNELING_ACK            = 0x0421,
};

// 03_08_02 table 8 and 5.5
static const uint8_t E_CONNECTION_ID         = 0x21;
static const uint8_t E_CONNECTION_TYPE       = 0x22;
static const uint8_t E_AUTHORISATION_ERROR   = 0x28;
static const uint8_t E_NO_TUNNELLING_ADDRESS = 0x2D;
static const uint8_t E_CONNECTION_IN_USE     = 0x2E;

static const uint8_t CRI_DEVICE_MGMT = 0x03;
static const uint8_t CRI_TUNNEL      = 0x04;

// 8.4.3.2 a: a TCP connection with nothing in it is closed after 10 s.
static const uint32_t TCP_IDLE_MS = 10000;

void KnxIpShim::begin(const Udp& udp, const Tunnels& tunnels)
{
    _udp     = udp;
    _tunnels = tunnels;

    knxIpSecure.begin(
        [](int8_t tcp, const uint8_t* frame, size_t length) {
            if (tcp >= 0)
                knxIpShim.tcpWrite((uint8_t)tcp, frame, (uint16_t)length);
            else
                knxIpShim._udp.multicast(frame, (uint16_t)length);
        },
        [](uint8_t session, uint8_t status) { knxIpShim.sessionEnded(session, status); });

    sbipTunnelSlotHook = [](uint8_t slot) -> bool { return knxIpShim.slotAllowed(slot); };
}

void KnxIpShim::tcpEnabled(bool enable)
{
    Preferences prefs;
    if (prefs.begin("sbip-knx", false))
    {
        prefs.putBool("tcp", enable);
        prefs.end();
    }
    _tcpSetting = enable;
    sysLog.printf("KNX: KNXnet/IP over TCP %s after the next restart\n", enable ? "on" : "off");
}

void KnxIpShim::startTcp()
{
    if (_listening) return;

    Preferences prefs;
    if (prefs.begin("sbip-knx", true))
    {
        _tcpSetting = prefs.getBool("tcp", true);
        prefs.end();
    }

    if (!_tcpSetting)
    {
        sysLog.println("KNX: KNXnet/IP over TCP switched off - core version 1, no secure sessions");
        return;
    }

    tcpServer.begin(PLAIN_PORT);
    tcpServer.setNoDelay(true);
    _listening = true;

    sysLog.println("KNX: KNXnet/IP over TCP on port 3671 (core version 2)");
}

uint8_t KnxIpShim::tcpConnections() const
{
    uint8_t n = 0;
    for (const Tcp& t : _tcp)
        if (t.used) n++;
    return n;
}

// ---------------------------------------------------------------------------
// Incoming
// ---------------------------------------------------------------------------

int KnxIpShim::receive(uint8_t* buffer, uint16_t max, uint32_t& ip, uint16_t& port)
{
    // A few inputs per call at most: the stack's loop also serves TP1.
    for (uint8_t budget = 0; budget < 6; budget++)
    {
        if (_queueCount > 0)
        {
            Injected& q = _queue[_queueHead];
            _queueHead = (uint8_t)((_queueHead + 1) % 8);
            _queueCount--;

            if (q.length > max) continue;
            memcpy(buffer, q.frame, q.length);
            ip   = INJECTED_IP;
            port = PLAIN_PORT;
            _ctx = Source{-1, INJECTED_IP, PLAIN_PORT, KnxIpSecure::NO_SESSION, 0, true, 0};
            return q.length;
        }

        acceptTcp();

        bool handled = false;
        for (uint8_t i = 0; i < MAX_TCP && !handled; i++)
        {
            uint16_t length = 0;
            if (!_tcp[i].used || !readTcp(i, buffer, length)) continue;

            handled = true;
            _stats.tcpFrames++;

            Source src{(int8_t)i, _tcp[i].ip, _tcp[i].port, KnxIpSecure::NO_SESSION, 0, false, 0};
            if (process(buffer, length, max, src))
            {
                ip   = pseudoIp(i);
                port = pseudoPort(_ctx.session);
                return length;
            }
        }
        if (handled) continue;

        uint32_t fromIp   = 0;
        uint16_t fromPort = 0;
        int received = _udp.receive(buffer, max, fromIp, fromPort);
        if (received <= 0) return 0;

        uint16_t length = (uint16_t)received;
        Source src{-1, fromIp, fromPort, KnxIpSecure::NO_SESSION, 0, false, 0};
        if (process(buffer, length, max, src))
        {
            ip   = fromIp;
            port = fromPort;
            return length;
        }
    }

    return 0;
}

bool KnxIpShim::acceptTcp()
{
    if (!_listening) return false;

    static uint32_t lastPoll = 0;
    if ((uint32_t)(millis() - lastPoll) < 20) return false;
    lastPoll = millis();

    NetworkClient client = tcpServer.accept();
    if (!client) return false;

    for (uint8_t i = 0; i < MAX_TCP; i++)
    {
        if (_tcp[i].used) continue;

        tcpClients[i] = client;
        Tcp& t      = _tcp[i];
        t.used      = true;
        t.closeSoon = false;
        t.fill      = 0;
        t.skip      = 0;
        t.lastRx    = millis();

        IPAddress remote = client.remoteIP();
        t.ip   = ((uint32_t)remote[0] << 24) | ((uint32_t)remote[1] << 16) |
                 ((uint32_t)remote[2] << 8) | remote[3];
        t.port = client.remotePort();

        _stats.tcpAccepted++;
        return true;
    }

    // 8.4.3.1: no more connections - not creating it is the whole answer.
    client.stop();
    _stats.tcpRefused++;
    return false;
}

/*
 * 8.4.3.3: frames recovered from the stream by the length in their header,
 * an oversized one skipped without closing, a malformed header closes the
 * connection, and so does the peer closing its side.
 */
bool KnxIpShim::readTcp(uint8_t index, uint8_t* frame, uint16_t& length)
{
    Tcp&           t      = _tcp[index];
    NetworkClient& client = tcpClients[index];

    int available = client.available();

    if (available <= 0)
    {
        if (!client.connected()) closeTcp(index);
        if (!t.used || t.fill < 6) return false;
    }

    while (available > 0 && t.skip > 0)
    {
        uint8_t discard[64];
        int got = client.read(discard, t.skip < sizeof(discard) ? t.skip : sizeof(discard));
        if (got <= 0) break;
        t.skip -= (uint16_t)got;
        available -= got;
        t.lastRx = millis();
    }

    if (available > 0 && t.skip == 0 && t.fill < sizeof(t.rx))
    {
        int got = client.read(t.rx + t.fill, sizeof(t.rx) - t.fill);
        if (got > 0)
        {
            t.fill += (uint16_t)got;
            t.lastRx = millis();
        }
    }

    if (t.fill < 6) return false;

    uint16_t total = (uint16_t)((t.rx[4] << 8) | t.rx[5]);

    if (t.rx[0] != 0x06 || t.rx[1] != 0x10 || total < 6)
    {
        sysLog.println("KNX: malformed KNXnet/IP header on TCP, connection closed");
        closeTcp(index);
        return false;
    }

    if (total > sizeof(t.rx))
    {
        // Longer than anything this device handles: skip what is left of it.
        t.skip = (uint16_t)(total - t.fill);
        t.fill = 0;
        return false;
    }

    if (t.fill < total) return false;

    memcpy(frame, t.rx, total);
    memmove(t.rx, t.rx + total, t.fill - total);
    t.fill -= total;
    length = total;
    return true;
}

bool KnxIpShim::process(uint8_t* frame, uint16_t& length, uint16_t max, Source src)
{
    using namespace knxsec;

    uint16_t service = serviceType(frame, length);
    if (service == 0 || totalLength(frame) != length) return false;

    switch (service)
    {
        case SECURE_WRAPPER:
        {
            size_t inner = 0;

            if (length >= 8 && frame[6] == 0 && frame[7] == 0)
            {
                // Session 0 is secure routing, which exists on the multicast only.
                if (src.tcp >= 0 || !knxIpSecure.routingUnwrap(frame, length, scratch, inner))
                    return false;

                memcpy(frame, scratch, inner);
                length = (uint16_t)inner;
                _ctx   = src;
                return true;
            }

            // Secure sessions exist over TCP only (2.2.3.3).
            if (src.tcp < 0) return false;

            uint8_t session = knxIpSecure.sessionUnwrap(frame, length, src.tcp, scratch, inner);
            if (session == KnxIpSecure::NO_SESSION || inner > max) return false;

            memcpy(frame, scratch, inner);
            length      = (uint16_t)inner;
            src.session = session;
            src.user    = knxIpSecure.session(session).user;
            service     = serviceType(frame, length);
            break;
        }

        case SESSION_REQUEST:
            // 2.2.3.6.4: over TCP, or discarded.
            if (src.tcp >= 0) knxIpSecure.sessionRequest(frame, length, src.tcp);
            return false;

        case TIMER_NOTIFY:
            // Our own notifies come back over the multicast loopback. Taken
            // for someone else's they would make the device follow itself -
            // and the one it sends at start-up is indistinguishable from the
            // answer it waits for.
            if (src.tcp < 0 && (_udp.ownIp == nullptr || src.ip != _udp.ownIp()))
                knxIpSecure.timerNotify(frame, length);
            return false;

        case SESSION_RESPONSE:
        case SESSION_AUTHENTICATE:
        case SESSION_STATUS:
            // Only valid inside a session, which was handled above.
            return false;

        default:
            break;
    }

    switch (service)
    {
        case SEARCH_REQUEST:
            // 03_08_02 7.6.1: to the discovery endpoint, over UDP only.
            if (src.tcp >= 0) return false;
            break;

        case 0x0530: case 0x0531: case 0x0532: case 0x0533:
            // Routing is multicast and never inside a session.
            if (src.tcp >= 0 || src.session != KnxIpSecure::NO_SESSION) return false;

            // 2.2.1.4.5: plain routing is accepted if and only if routing
            // is not secured.
            if (knxIpSecure.secured(FAMILY_ROUTING))
            {
                _stats.plainRoutingDropped++;
                knxIpSecure.countPlainRefused();
                return false;
            }
            break;

        case CONNECT_REQUEST:
            if (!checkConnect(frame, length, src)) return false;
            break;

        case TUNNELING_REQUEST:
        case TUNNELING_ACK:
        case DEVICE_CONFIG_REQUEST:
        case DEVICE_CONFIG_ACK:
        case CONNECTIONSTATE_REQUEST:
        case DISCONNECT_REQUEST:
        case DISCONNECT_RESPONSE:
            if (!checkChannel(frame, length, src)) return false;
            break;

        default:
            break;
    }

    _ctx = src;
    return true;
}

/*
 * 03_08_09 2.2.1.4.2 and 2.3.1.8.4, 03_08_02 table 8. Refuses by itself and
 * returns false, or leaves the requested address in @p src for the slot
 * hook.
 */
bool KnxIpShim::checkConnect(const uint8_t* frame, uint16_t length, Source& src)
{
    // Header, two HPAIs, then the CRI: length, connection type, ...
    if (length < 26) return false;

    uint8_t criLength = frame[22];
    uint8_t type      = frame[23];
    bool    inSession = src.session != KnxIpSecure::NO_SESSION;

    if (type == CRI_DEVICE_MGMT)
    {
        bool secured = knxIpSecure.secured(FAMILY_DEVICE_MGMT);

        if (secured && (!inSession || src.user != 1))
        {
            _stats.plainConnectRefused++;
            knxIpSecure.countPlainRefused();
            refuseConnect(frame, length, src, E_CONNECTION_TYPE);
            return false;
        }
        return true;
    }

    if (type != CRI_TUNNEL) return true;

    bool secured = knxIpSecure.secured(FAMILY_TUNNELLING);

    if (secured && !inSession)
    {
        _stats.plainConnectRefused++;
        knxIpSecure.countPlainRefused();
        refuseConnect(frame, length, src, E_CONNECTION_TYPE);
        return false;
    }

    // Extended CRI (tunnelling v2): a specific address is asked for.
    if (criLength >= 6 && length >= 28)
    {
        uint16_t address = (uint16_t)((frame[26] << 8) | frame[27]);
        int      slot    = -1;

        for (uint8_t i = 0; _tunnels.slotAddress && i < KNX_TUNNELING; i++)
            if (_tunnels.slotAddress(i) == address) { slot = i; break; }

        if (slot < 0)
        {
            refuseConnect(frame, length, src, E_NO_TUNNELLING_ADDRESS);
            return false;
        }

        if (secured && src.user != 1 && !knxIpSecure.userMayUseAddress(src.user, (uint8_t)(slot + 1)))
        {
            refuseConnect(frame, length, src, E_AUTHORISATION_ERROR);
            return false;
        }

        if (_tunnels.addressInUse && _tunnels.addressInUse(address))
        {
            refuseConnect(frame, length, src, E_CONNECTION_IN_USE);
            return false;
        }

        src.requestedIa = address;
    }

    return true;
}

bool KnxIpShim::slotAllowed(uint8_t slot) const
{
    if (_ctx.requestedIa != 0)
        return _tunnels.slotAddress && _tunnels.slotAddress(slot) == _ctx.requestedIa;

    // 2.3.1.8.1: the tunnelling users only count in an authenticated session
    // while tunnelling is secured; otherwise every address is open.
    if (_ctx.session == KnxIpSecure::NO_SESSION || _ctx.user == 1 ||
        !knxIpSecure.secured(FAMILY_TUNNELLING))
        return true;

    return knxIpSecure.userMayUseAddress(_ctx.user, (uint8_t)(slot + 1));
}

bool KnxIpShim::checkChannel(uint8_t* frame, uint16_t length, const Source& src)
{
    uint16_t service = knxsec::serviceType(frame, length);
    bool connectionHeader = service == TUNNELING_REQUEST || service == TUNNELING_ACK ||
                            service == DEVICE_CONFIG_REQUEST || service == DEVICE_CONFIG_ACK;

    if (length < (connectionHeader ? 10 : 8)) return false;

    uint8_t  id = connectionHeader ? frame[7] : frame[6];
    Channel* c  = channel(id);

    // Unknown channels are the stack's business: it answers E_CONNECTION_ID.
    if (c != nullptr && (c->session != src.session || c->tcp != src.tcp))
    {
        _stats.channelHijackDropped++;

        // 2.4.1: from outside its session - plain, or another session - the
        // channel does not exist.
        if (service == CONNECTIONSTATE_REQUEST || service == DISCONNECT_REQUEST)
        {
            uint8_t response[8];
            knxsec::header(response, service == CONNECTIONSTATE_REQUEST ? CONNECTIONSTATE_RESPONSE
                                                                        : DISCONNECT_RESPONSE,
                           sizeof(response));
            response[6] = id;
            response[7] = E_CONNECTION_ID;

            Source to = src;
            if (src.tcp < 0 && length >= 16)
            {
                uint32_t ip   = ((uint32_t)frame[10] << 24) | ((uint32_t)frame[11] << 16) |
                                ((uint32_t)frame[12] << 8) | frame[13];
                uint16_t port = (uint16_t)((frame[14] << 8) | frame[15]);
                if (ip != 0 && port != 0)
                {
                    to.ip   = ip;
                    to.port = port;
                }
            }
            deliver(to, response, sizeof(response));
        }
        return false;
    }

    if (src.tcp >= 0)
    {
        // 03_08_04 2.6.2: no acknowledgements over TCP, and the sequence
        // counter is not evaluated - the stack still insists on it, so it
        // gets the one it expects.
        if (service == TUNNELING_ACK || service == DEVICE_CONFIG_ACK) return false;

        if (c != nullptr && (service == TUNNELING_REQUEST || service == DEVICE_CONFIG_REQUEST))
            frame[8] = c->rxSeq++;
    }

    return true;
}

void KnxIpShim::refuseConnect(const uint8_t* frame, uint16_t length, const Source& src, uint8_t status)
{
    uint8_t response[8];
    knxsec::header(response, CONNECT_RESPONSE, sizeof(response));
    response[6] = 0;
    response[7] = status;

    Source to = src;
    if (src.tcp < 0 && length >= 16)
    {
        // Control endpoint HPAI: length, protocol, address, port.
        uint32_t ip   = ((uint32_t)frame[8] << 24) | ((uint32_t)frame[9] << 16) |
                        ((uint32_t)frame[10] << 8) | frame[11];
        uint16_t port = (uint16_t)((frame[12] << 8) | frame[13]);
        if (ip != 0 && port != 0)
        {
            to.ip   = ip;
            to.port = port;
        }
    }

    deliver(to, response, sizeof(response));
}

// ---------------------------------------------------------------------------
// Outgoing
// ---------------------------------------------------------------------------

bool KnxIpShim::sendUnicast(uint32_t ip, uint16_t port, uint8_t* buffer, uint16_t length)
{
    // The stack answers "to where it came from" as 0.0.0.0:0 - the NAT form
    // of the request's HPAI, and the only form a TCP client sends.
    if (ip == 0 || port == 0)
    {
        if (_ctx.injected) return true;
        ip   = _ctx.tcp >= 0 ? pseudoIp((uint8_t)_ctx.tcp) : _ctx.ip;
        port = _ctx.tcp >= 0 ? pseudoPort(_ctx.session) : _ctx.port;
    }

    // Answers to frames this shim made up itself.
    if (ip == INJECTED_IP) return true;

    Source to{-1, ip, port, KnxIpSecure::NO_SESSION, 0, false, 0};

    int8_t tcp = tcpOf(ip);
    if (tcp >= 0)
    {
        if (!_tcp[tcp].used) return false;
        to.tcp = tcp;

        if (port != PLAIN_PORT)
        {
            uint8_t session = (uint8_t)(port - SESSION_PORT);
            if (port < SESSION_PORT || !knxIpSecure.sessionOn(session, tcp))
                return true; // its session is gone, and nothing leaves in plain instead
            to.session = session;
            to.user    = knxIpSecure.session(session).user;
        }
    }

    uint16_t service = knxsec::serviceType(buffer, length);

    switch (service)
    {
        case CONNECT_RESPONSE:
            if (length >= 8 && buffer[7] == 0)
            {
                // Channel ids come round again; whatever held this one before
                // is gone.
                for (Channel& c : _channels)
                    if (c.id == buffer[6]) c.id = 0;

                for (Channel& c : _channels)
                {
                    if (c.id != 0) continue;
                    c = Channel{buffer[6], to.tcp, to.session, 0};
                    break;
                }
            }

            // 8.4.3.4.3: over TCP the data endpoint is the route back HPAI.
            if (to.tcp >= 0 && length >= 16)
            {
                static const uint8_t routeBack[8] = {0x08, 0x02, 0, 0, 0, 0, 0, 0};
                memcpy(buffer + 8, routeBack, sizeof(routeBack));
            }
            break;

        case TUNNELING_ACK:
        case DEVICE_CONFIG_ACK:
            if (to.tcp >= 0) return true;
            break;

        case SEARCH_RESPONSE:
        case SEARCH_RESPONSE_EXT:
        case DESCRIPTION_RESPONSE:
        {
            uint16_t rewritten = rewriteDescription(buffer, length, to);
            if (rewritten > 0) return deliver(to, outFrame, rewritten);
            break;
        }

        default:
            break;
    }

    bool sent = deliver(to, buffer, length);

    if (service == DISCONNECT_REQUEST || service == DISCONNECT_RESPONSE)
    {
        Channel* c = length >= 7 ? channel(buffer[6]) : nullptr;
        if (c != nullptr) c->id = 0;

        // 8.4.3.2 b: the server ended the last connection in it (the stack
        // does that on a heartbeat timeout) - the TCP connection goes too.
        if (service == DISCONNECT_REQUEST && to.tcp >= 0 && tcpIdle((uint8_t)to.tcp))
            _tcp[to.tcp].closeSoon = true;
    }

    // The stack counts acknowledgements for what it sends to a tunnel; over
    // TCP there are none, so it gets one from here.
    if (to.tcp >= 0 && (service == TUNNELING_REQUEST || service == DEVICE_CONFIG_REQUEST) && length >= 10)
    {
        uint8_t ack[10];
        knxsec::header(ack, service == TUNNELING_REQUEST ? TUNNELING_ACK : DEVICE_CONFIG_ACK, sizeof(ack));
        ack[6] = 4;
        ack[7] = buffer[7];
        ack[8] = buffer[8];
        ack[9] = 0;
        inject(ack, sizeof(ack));
    }

    return sent;
}

bool KnxIpShim::deliver(const Source& to, const uint8_t* frame, uint16_t length)
{
    if (to.session != KnxIpSecure::NO_SESSION)
        return knxIpSecure.sessionSend(to.session, frame, length);

    if (to.tcp >= 0) return tcpWrite((uint8_t)to.tcp, frame, length);

    return _udp.send(to.ip, to.port, frame, length);
}

bool KnxIpShim::tcpWrite(uint8_t index, const uint8_t* frame, uint16_t length)
{
    if (index >= MAX_TCP || !_tcp[index].used) return false;

    size_t written = tcpClients[index].write(frame, length);
    if (written != length)
    {
        sysLog.println("KNX: TCP send failed, connection closed");
        _tcp[index].closeSoon = true;
        return false;
    }
    return true;
}

bool KnxIpShim::slotAuthorized(uint8_t slot, uint8_t session, uint8_t user) const
{
    if (!knxIpSecure.secured(FAMILY_TUNNELLING)) return true;
    if (session == KnxIpSecure::NO_SESSION) return false;
    return knxIpSecure.userMayUseAddress(user, (uint8_t)(slot + 1));
}

/*
 * Search and description answers, rewritten into outFrame.
 *
 *   core version  2 while the TCP listener runs (patch 18 makes the stack
 *                 say 1, which is right without TCP)
 *   family 09     the security family, in the extended search answer only
 *                 (03_08_09 2.6.2.1)
 *   DIB 06        the secured families, there as well and only if any are
 *                 secured (2.6.2.2)
 *   DIB 07        tunnel slots: "authorized" only if tunnelling is open or
 *                 the session user may use the slot
 */
uint16_t KnxIpShim::rewriteDescription(const uint8_t* frame, uint16_t length, const Source& to)
{
    uint16_t service  = knxsec::serviceType(frame, length);
    bool     extended = service == SEARCH_RESPONSE_EXT;
    uint16_t pos      = (service == DESCRIPTION_RESPONSE) ? 6 : 14;
    uint16_t max      = sizeof(outFrame);

    if (length < pos || max < length + 16) return 0;

    memcpy(outFrame, frame, pos);
    uint16_t out = pos;

    while (pos + 2 <= length)
    {
        uint8_t dibLength = frame[pos];
        uint8_t type      = frame[pos + 1];

        if (dibLength < 2 || pos + dibLength > length || out + dibLength + 2 > max) return 0;

        if (type == 0x02)
        {
            uint16_t start    = out;
            bool     security = false;
            outFrame[out++] = 0;
            outFrame[out++] = 0x02;

            for (uint16_t k = pos + 2; k + 1 < pos + dibLength; k += 2)
            {
                uint8_t family  = frame[k];
                uint8_t version = frame[k + 1];

                if (family == FAMILY_CORE && _listening) version = 2;
                if (family == FAMILY_SECURITY) security = true;

                outFrame[out++] = family;
                outFrame[out++] = version;
            }

            if (extended && !security)
            {
                outFrame[out++] = FAMILY_SECURITY;
                outFrame[out++] = 1;
            }

            outFrame[start] = (uint8_t)(out - start);
        }
        else if (type == 0x07 && dibLength >= 4)
        {
            uint16_t start = out;
            memcpy(outFrame + out, frame + pos, 4);
            out += 4;

            uint8_t slot = 0;
            for (uint16_t k = pos + 4; k + 3 < pos + dibLength; k += 4, slot++)
            {
                // The stack sets all 16 bits of a free slot. Bit 0 free,
                // bit 1 authorized, bit 2 usable.
                uint8_t raw = frame[k + 3];
                bool authorized = slotAuthorized(slot, to.session, to.user);

                outFrame[out++] = frame[k];
                outFrame[out++] = frame[k + 1];
                outFrame[out++] = 0;
                outFrame[out++] = (uint8_t)((raw & 0x05) | (authorized ? 0x02 : 0));
            }

            outFrame[start] = (uint8_t)(out - start);
        }
        else if (type != 0x06)
        {
            memcpy(outFrame + out, frame + pos, dibLength);
            out += dibLength;
        }

        pos += dibLength;
    }

    const uint16_t families = knxIpSecure.securedFamilies() &
                              ((1u << FAMILY_DEVICE_MGMT) | (1u << FAMILY_TUNNELLING) | (1u << FAMILY_ROUTING));

    if (extended && families != 0)
    {
        uint16_t start = out;
        outFrame[out++] = 0;
        outFrame[out++] = 0x06;

        for (uint8_t family = FAMILY_DEVICE_MGMT; family <= FAMILY_ROUTING; family++)
        {
            if (!(families & (1u << family))) continue;
            outFrame[out++] = family;
            outFrame[out++] = 1;
        }

        outFrame[start] = (uint8_t)(out - start);
    }

    outFrame[4] = (uint8_t)(out >> 8);
    outFrame[5] = (uint8_t)out;
    return out;
}

bool KnxIpShim::sendMulticast(uint8_t* buffer, uint16_t length)
{
    // 2.2.1.4.5: every routing frame wrapped once routing is secured.
    if (!knxIpSecure.routingSecure())
        return _udp.multicast(buffer, length);

    size_t wrapped = knxIpSecure.routingWrap(buffer, length, outFrame);
    if (wrapped > 0)
        return _udp.multicast(outFrame, (uint16_t)wrapped);

    // The timer is still being acquired; a few frames can wait for it.
    if (_pendingCount >= 8) return false;

    uint8_t* copy = (uint8_t*)malloc(length);
    if (copy == nullptr) return false;

    memcpy(copy, buffer, length);
    _pending[_pendingCount++] = Pending{length, copy};
    _stats.routingQueued++;
    return true;
}

void KnxIpShim::flushRouting()
{
    if (_pendingCount == 0) return;

    if (knxIpSecure.routingSecure() && !knxIpSecure.timerSynced()) return;

    for (uint8_t i = 0; i < _pendingCount; i++)
    {
        sendMulticast(_pending[i].frame, _pending[i].length);
        free(_pending[i].frame);
    }
    _pendingCount = 0;
}

// ---------------------------------------------------------------------------
// Bookkeeping
// ---------------------------------------------------------------------------

KnxIpShim::Channel* KnxIpShim::channel(uint8_t id)
{
    if (id == 0) return nullptr;

    for (Channel& c : _channels)
        if (c.id == id) return &c;

    return nullptr;
}

void KnxIpShim::inject(const uint8_t* frame, uint16_t length)
{
    if (_queueCount >= 8 || length > sizeof(_queue[0].frame)) return;

    Injected& q = _queue[(_queueHead + _queueCount) % 8];
    q.length = (uint8_t)length;
    memcpy(q.frame, frame, length);
    _queueCount++;
}

/*
 * 2.4.2: connections opened in a session end with it, without telling the
 * client. The stack frees a tunnel when it gets a disconnect for it; its
 * answer goes to the made-up sender and is dropped.
 */
void KnxIpShim::releaseChannels(int8_t tcp, uint8_t session)
{
    for (Channel& c : _channels)
    {
        if (c.id == 0) continue;

        bool mine = (tcp >= 0 && c.tcp == tcp) ||
                    (session != KnxIpSecure::NO_SESSION && c.session == session);
        if (!mine) continue;

        uint8_t request[16];
        knxsec::header(request, DISCONNECT_REQUEST, sizeof(request));
        request[6] = c.id;
        request[7] = 0;
        request[8] = 8;
        request[9] = 1;
        memset(request + 10, 0, 6);
        inject(request, sizeof(request));

        c.id = 0;
    }
}

bool KnxIpShim::tcpIdle(uint8_t index) const
{
    if (knxIpSecure.sessionsOn((int8_t)index) > 0) return false;

    for (const Channel& c : _channels)
        if (c.id != 0 && c.tcp == (int8_t)index) return false;

    return true;
}

void KnxIpShim::closeTcp(uint8_t index)
{
    if (index >= MAX_TCP || !_tcp[index].used) return;

    // 8.4.3.5: closing the connection closes everything in it.
    _tcp[index].used = false;
    _tcp[index].fill = 0;
    _tcp[index].skip = 0;

    knxIpSecure.tcpClosed((int8_t)index);
    releaseChannels((int8_t)index, KnxIpSecure::NO_SESSION);
    tcpClients[index].stop();
}

void KnxIpShim::sessionEnded(uint8_t session, uint8_t status)
{
    int8_t tcp = knxIpSecure.session(session).tcp;

    releaseChannels(-1, session);

    // 8.4.3.2 b and 03_08_09 2.2.3.9: after a timeout or a refused
    // authentication the connection goes too, unless something else still
    // lives in it. Not from here: this runs inside the session's own end.
    bool serverEnded = status == knxsec::STATUS_TIMEOUT || status == knxsec::STATUS_AUTH_FAILED ||
                       status == knxsec::STATUS_UNAUTHENTICATED;

    if (serverEnded && tcp >= 0 && tcp < MAX_TCP && _tcp[tcp].used &&
        knxIpSecure.sessionsOn(tcp) <= 1)
    {
        bool channels = false;
        for (const Channel& c : _channels)
            if (c.id != 0 && c.tcp == tcp) channels = true;

        if (!channels) _tcp[tcp].closeSoon = true;
    }
}

void KnxIpShim::loop()
{
    // Secure routing starts with a timer request on the multicast, which
    // needs an address to go out from.
    knxIpSecure.loop(_udp.ownIp != nullptr && _udp.ownIp() != 0);
    flushRouting();

    for (uint8_t i = 0; i < MAX_TCP; i++)
    {
        if (!_tcp[i].used) continue;

        if (_tcp[i].closeSoon)
        {
            closeTcp(i);
            continue;
        }

        // 8.4.3.2 a: nothing open in it and 10 s without an octet.
        if (tcpIdle(i) && (uint32_t)(millis() - _tcp[i].lastRx) > TCP_IDLE_MS)
            closeTcp(i);
    }
}

String KnxIpShim::connectionsJson() const
{
    String j = "[";
    bool first = true;

    auto ipText = [](uint32_t ip) -> String {
        return String(ip >> 24) + "." + String((ip >> 16) & 0xFF) + "." +
               String((ip >> 8) & 0xFF) + "." + String(ip & 0xFF);
    };

    for (uint8_t i = 0; i < KnxIpSecure::MAX_SESSIONS; i++)
    {
        const KnxIpSecure::Session& s = knxIpSecure.session(i);
        if (!s.used || s.tcp < 0 || s.tcp >= MAX_TCP) continue;

        if (!first) j += ",";
        first = false;

        j += "{\"session\":" + String(s.id);
        j += ",\"user\":" + String(s.user);
        j += ",\"auth\":" + String(s.authenticated ? "true" : "false");
        j += ",\"ip\":\"" + ipText(_tcp[s.tcp].ip) + "\"}";
    }

    for (uint8_t i = 0; i < MAX_TCP; i++)
    {
        if (!_tcp[i].used || knxIpSecure.sessionsOn((int8_t)i) > 0) continue;

        if (!first) j += ",";
        first = false;

        j += "{\"session\":0,\"user\":0,\"auth\":false,\"ip\":\"" + ipText(_tcp[i].ip) + "\"}";
    }

    return j + "]";
}
