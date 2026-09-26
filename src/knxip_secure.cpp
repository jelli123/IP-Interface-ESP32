/*
 *  knxip_secure.cpp - KNXnet/IP Secure: configuration, sessions, routing
 *  timer. Section numbers refer to 03_08_09 "KNX IP Secure" v01.01.02.
 */

#include "knxip_secure.h"

#include <Preferences.h>
#include <esp_random.h>
#include <esp_timer.h>

#include "knx_secure_crypto.h"
#include "knx_secure_store.h"
#include "log_buffer.h"

KnxIpSecure knxIpSecure;

/*
 * The property callbacks the stack calls, see patch 19 in
 * scripts/patch_knx.py. Plain functions because the stack's lambdas cannot
 * capture anything.
 */
uint8_t (*sbipIpSecureRead)(uint8_t pid, uint16_t start, uint8_t count, uint8_t* data) = nullptr;
uint8_t (*sbipIpSecureWrite)(uint8_t pid, uint16_t start, uint8_t count, const uint8_t* data) = nullptr;
void (*sbipIpSecureFunction)(bool command, uint8_t pid, uint8_t* data, uint8_t length,
                             uint8_t* result, uint8_t& resultLength) = nullptr;

static const char* SEC_NS    = "sbip-sec";
static const char* KEY_CFG   = "ipsec";
static const char* KEY_TIMER = "mctimer";

static const uint8_t CONFIG_VERSION = 2;

// 2.2.3.5.2.1.1
static const uint32_t TIMEOUT_SESSION_MS        = 60000;
static const uint32_t TIMEOUT_AUTHENTICATION_MS = 10000;

// 2.3.1.6.3 and 2.3.1.7.3: delivery state
static const uint16_t DEFAULT_LATENCY_MS    = 2000;
static const uint8_t  DEFAULT_SYNC_FRACTION = 0x1A; // 10,2 %

// 2.3.1.4.3: PBKDF2 of the empty password, what user 1 starts with.
static const uint8_t EMPTY_PASSWORD_HASH[16] = {0xE9, 0xC3, 0x04, 0xB9, 0x14, 0xA3, 0x51, 0x75,
                                                0xFD, 0x7D, 0x1C, 0x67, 0x3A, 0xB5, 0x2F, 0xE1};

// 2.2.2.3.2.2
static const uint32_t MAX_DELAY_INITIAL_NOTIFY_MS = 10000;
static const uint32_t MIN_DELAY_KEEPER_PERIODIC   = 10000;
static const uint32_t MIN_DELAY_KEEPER_UPDATE     = 100;

/*
 * 2.2.4.2: the timer is persisted at least once per hour of timer time, and
 * a start assumes the worst case, a whole hour lost since. The margin makes
 * the write happen before the hour is used up, not after.
 */
static const uint64_t TIMER_PERSIST_INTERVAL = 3600000ull;
static const uint64_t TIMER_PERSIST_MARGIN   = 60000ull;

static uint8_t frameBuffer[600];

// ---------------------------------------------------------------------------
// Setup and configuration
// ---------------------------------------------------------------------------

void KnxIpSecure::begin(SendFn send, SessionEndFn sessionEnd)
{
    _send       = send;
    _sessionEnd = sessionEnd;

    sbipIpSecureRead = [](uint8_t pid, uint16_t start, uint8_t count, uint8_t* data) -> uint8_t {
        return knxIpSecure.propertyRead(pid, start, count, data);
    };
    sbipIpSecureWrite = [](uint8_t pid, uint16_t start, uint8_t count, const uint8_t* data) -> uint8_t {
        return knxIpSecure.propertyWrite(pid, start, count, data);
    };
    sbipIpSecureFunction = [](bool command, uint8_t pid, uint8_t* data, uint8_t length,
                              uint8_t* result, uint8_t& resultLength) {
        knxIpSecure.propertyFunction(command, pid, data, length, result, resultLength);
    };

    loadConfig();

    if (_cfg.securedFamilies != 0)
    {
        sysLog.printf("SECURE: KNXnet/IP secured families 0x%04X, %u password(s)%s%s\n",
                      (unsigned)_cfg.securedFamilies, (unsigned)_cfg.passwordCount,
                      authCodeFromEts() ? ", device authentication code" : "",
                      hasBackboneKey() ? ", backbone key" : "");
    }
}

void KnxIpSecure::loadConfig()
{
    clearConfiguration();
    _dirty = false;

    Preferences prefs;
    if (!prefs.begin(SEC_NS, true)) return;

    Config stored;
    if (prefs.getBytesLength(KEY_CFG) == sizeof(stored) &&
        prefs.getBytes(KEY_CFG, &stored, sizeof(stored)) == sizeof(stored) &&
        stored.version == CONFIG_VERSION)
    {
        _cfg = stored;
    }

    knxsec::wipe(&stored, sizeof(stored));
    prefs.end();
}

void KnxIpSecure::clearConfiguration()
{
    for (uint8_t i = 0; i < MAX_SESSIONS; i++)
        if (_sessions[i].used) sessionClose(i, knxsec::STATUS_CLOSE);

    knxsec::wipe(&_cfg, sizeof(_cfg));
    _cfg.version       = CONFIG_VERSION;
    _cfg.latencyMs     = DEFAULT_LATENCY_MS;
    _cfg.syncFraction  = DEFAULT_SYNC_FRACTION;
    _cfg.passwordCount = 1;
    memcpy(_cfg.passwords[0], EMPTY_PASSWORD_HASH, 16);

    // 3/8/3 2.5.29: the tunnelling addresses are the additional addresses,
    // all of them; the router never tunnels over its own.
    _cfg.tunnelAddrCount = KNX_TUNNELING;
    for (uint8_t i = 0; i < KNX_TUNNELING; i++)
        _cfg.tunnelAddrs[i] = i + 1;

    markDirty();
}

void KnxIpSecure::markDirty()
{
    _dirty   = true;
    _dirtyAt = millis();
}

const uint8_t* KnxIpSecure::authCode() const
{
    // 2.3.1.3.3: until ETS writes one, the device authentication code is
    // the FDSK printed on the device.
    return authCodeFromEts() ? _cfg.deviceAuth : knxSecureStore.fdsk();
}

bool KnxIpSecure::secured(KnxIpFamily family) const
{
    return (_cfg.securedFamilies >> family) & 0x01;
}

/*
 * One description of every array property, so that read and write share the
 * element bookkeeping. The single-value properties (95, 96) always hold one
 * element.
 */
bool KnxIpSecure::describe(uint8_t pid, ArrayProperty& out)
{
    static uint8_t one = 1;

    switch (pid)
    {
        case 79: out = {_cfg.tunnelAddrs, &_cfg.tunnelAddrCount, 1, KNX_TUNNELING, false}; return true;
        case 91:
            _keyCount = hasBackboneKey() ? 1 : 0;
            out = {_cfg.backboneKey, &_keyCount, 16, 1, true};
            return true;
        case 92:
            _keyCount = 1;
            out = {_cfg.deviceAuth, &_keyCount, 16, 1, true};
            return true;
        case 93: out = {&_cfg.passwords[0][0], &_cfg.passwordCount, 16, MAX_USERS, true}; return true;
        case 95: out = {(uint8_t*)&_cfg.latencyMs, &one, 2, 1, false}; return true;
        case 96: out = {&_cfg.syncFraction, &one, 1, 1, false}; return true;
        case 97: out = {&_cfg.users[0][0], &_cfg.userCount, 2, MAX_USER_SLOTS, false}; return true;
        default: return false;
    }
}

uint8_t KnxIpSecure::propertyRead(uint8_t pid, uint16_t start, uint8_t count, uint8_t* data)
{
    ArrayProperty p;
    if (!describe(pid, p)) return 0;

    if (start == 0)
    {
        data[0] = 0;
        data[1] = *p.count;
        return 1;
    }

    // 2.3.1.2.2 ff.: keys are never readable, whoever asks.
    if (p.secret) return 0;

    if (start - 1 + count > *p.count) return 0;

    if (pid == 95)
    {
        // Stored in host order, the property is big-endian.
        data[0] = (uint8_t)(_cfg.latencyMs >> 8);
        data[1] = (uint8_t)_cfg.latencyMs;
        return 1;
    }

    memcpy(data, p.data + (start - 1) * p.size, (size_t)count * p.size);
    return count;
}

uint8_t KnxIpSecure::propertyWrite(uint8_t pid, uint16_t start, uint8_t count, const uint8_t* data)
{
    ArrayProperty p;
    if (!describe(pid, p)) return 0;

    if (start == 0)
    {
        // Setting the element count; ETS clears a table this way before it
        // writes it anew.
        uint16_t elements = (uint16_t)((data[0] << 8) | data[1]);
        if (elements > p.max) return 0;

        if (pid == 91 && elements == 0)
        {
            _cfg.flags &= (uint8_t)~FLAG_BACKBONE;
            knxsec::wipe(_cfg.backboneKey, 16);
        }
        else if (pid == 92 && elements == 0)
        {
            _cfg.flags &= (uint8_t)~FLAG_AUTH;
        }
        else if (pid != 91 && pid != 92 && pid != 95 && pid != 96)
        {
            *p.count = (uint8_t)elements;
            knxsec::wipe(p.data + elements * p.size, (size_t)(p.max - elements) * p.size);
        }

        markDirty();
        return 1;
    }

    // 2.3.1.4.3: beyond the supported range is the standard error.
    if (start - 1 + count > p.max) return 0;

    if (pid == 91)
    {
        // E11: a new backbone key is a new domain, and its timer starts
        // from 0 (2.2.2.2.2).
        bool changed = !hasBackboneKey() || !knxsec::equal(_cfg.backboneKey, data, 16);
        memcpy(_cfg.backboneKey, data, 16);
        _cfg.flags |= FLAG_BACKBONE;

        if (changed)
        {
            setTimer(0);
            persistTimer(true);
            if (_routingOn) restartSync(false);
        }
    }
    else if (pid == 95)
    {
        _cfg.latencyMs = (uint16_t)((data[0] << 8) | data[1]);
    }
    else
    {
        memcpy(p.data + (start - 1) * p.size, data, (size_t)count * p.size);

        if (pid == 92) _cfg.flags |= FLAG_AUTH;
        else if (pid != 96 && start - 1 + count > *p.count) *p.count = (uint8_t)(start - 1 + count);
    }

    markDirty();

    if (pid == 91 || pid == 92 || pid == 93)
        sysLog.printf("SECURE: KNXnet/IP %s written\n",
                      pid == 91 ? "backbone key" : pid == 92 ? "device authentication code" : "password hash");

    return count;
}

void KnxIpSecure::propertyFunction(bool command, uint8_t pid, uint8_t* data, uint8_t length,
                                   uint8_t* result, uint8_t& resultLength)
{
    // 2.3.1.5: [reserved 0][service 0][family]( [security version] for a
    // write ). Answer: [return code][service]( [family][version] for a read ).
    const uint8_t SUCCESS = 0x00, INVALID = 0xF2, DATA_VOID = 0xF8;

    if (pid != 94 || length < 2)
    {
        result[0]    = INVALID;
        resultLength = 1;
        return;
    }

    uint8_t service = data[1];
    result[1]       = service;
    resultLength    = 2;

    if (service != 0)
    {
        result[0] = INVALID;
        return;
    }

    if (data[0] != 0 || length < 3)
    {
        result[0] = DATA_VOID;
        return;
    }

    uint8_t family = data[2];
    if (family != FAMILY_DEVICE_MGMT && family != FAMILY_TUNNELLING && family != FAMILY_ROUTING)
    {
        result[0] = DATA_VOID;
        return;
    }

    if (!command)
    {
        result[0]    = SUCCESS;
        result[2]    = family;
        result[3]    = secured((KnxIpFamily)family) ? 1 : 0;
        resultLength = 4;
        return;
    }

    if (length < 4 || data[3] > 1)
    {
        result[0] = DATA_VOID;
        return;
    }

    uint16_t bit    = (uint16_t)(1u << family);
    uint16_t before = _cfg.securedFamilies;

    if (data[3]) _cfg.securedFamilies |= bit;
    else _cfg.securedFamilies &= (uint16_t)~bit;

    if (before != _cfg.securedFamilies)
    {
        markDirty();
        sysLog.printf("SECURE: KNXnet/IP %s %s\n",
                      family == FAMILY_DEVICE_MGMT ? "device management"
                          : family == FAMILY_TUNNELLING ? "tunnelling" : "routing",
                      data[3] ? "secured" : "no longer secured");
    }

    result[0] = SUCCESS;
}

bool KnxIpSecure::userMayUseAddress(uint8_t user, uint8_t additional) const
{
    if (user == 1) return true;

    // PID 97 is sorted by user; each entry names a tunnelling address by its
    // 1-based index into PID 79, which in turn names an entry of PID 53
    // (0 would be the device's own address).
    for (uint8_t i = 0; i < _cfg.userCount; i++)
    {
        if (_cfg.users[i][0] != user) continue;

        uint8_t index = _cfg.users[i][1];
        if (index == 0 || index > _cfg.tunnelAddrCount) continue;

        if (_cfg.tunnelAddrs[index - 1] == additional) return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// Sessions
// ---------------------------------------------------------------------------

uint8_t KnxIpSecure::sessionCount() const
{
    uint8_t n = 0;
    for (const Session& s : _sessions)
        if (s.used) n++;
    return n;
}

bool KnxIpSecure::sessionOn(uint8_t index, int8_t tcp) const
{
    return index < MAX_SESSIONS && _sessions[index].used && _sessions[index].tcp == tcp;
}

uint8_t KnxIpSecure::sessionsOn(int8_t tcp) const
{
    uint8_t n = 0;
    for (const Session& s : _sessions)
        if (s.used && s.tcp == tcp) n++;
    return n;
}

void KnxIpSecure::tcpClosed(int8_t tcp)
{
    for (uint8_t i = 0; i < MAX_SESSIONS; i++)
        if (_sessions[i].used && _sessions[i].tcp == tcp) endSession(i, knxsec::STATUS_CLOSE);
}

void KnxIpSecure::sessionRequest(const uint8_t* frame, size_t length, int8_t tcp)
{
    using namespace knxsec;

    // 2.2.3.6.4: TCP, the route back HPAI, 46 octets - or discarded.
    static const uint8_t ROUTE_BACK[8] = {0x08, 0x02, 0, 0, 0, 0, 0, 0};

    if (tcp < 0 || length != SESSION_REQUEST_LEN || memcmp(frame + 6, ROUTE_BACK, 8) != 0)
        return;

    // 2.2.3.7.6: no free session - no answer at all.
    uint8_t index = NO_SESSION;
    for (uint8_t i = 0; i < MAX_SESSIONS; i++)
        if (!_sessions[i].used) { index = i; break; }

    if (index == NO_SESSION)
    {
        sysLog.println("SECURE: session request dropped, all sessions in use");
        return;
    }

    const uint8_t* clientPublic = frame + 14;
    uint8_t priv[32], serverPublic[32], shared[32], digest[32];

    // 2.2.3.1.1: a new key pair for every session.
    if (!knxsec::x25519Keypair(priv, serverPublic) || !knxsec::x25519(priv, clientPublic, shared))
    {
        knxsec::wipe(priv, sizeof(priv));
        sysLog.println("SECURE: key agreement failed, session request dropped");
        return;
    }

    Session& s = _sessions[index];
    memset(&s, 0, sizeof(s));

    // Unique among the open sessions, never 0 (reserved for multicast).
    for (bool clash = true; clash;)
    {
        if (++_lastSessionId == 0 || _lastSessionId == 0xFFFF) _lastSessionId = 1;

        clash = false;
        for (const Session& other : _sessions)
            if (other.used && other.id == _lastSessionId) clash = true;
    }

    sha256(shared, sizeof(shared), digest);
    memcpy(s.key, digest, 16);
    for (int i = 0; i < 32; i++)
        s.xorKeys[i] = clientPublic[i] ^ serverPublic[i];

    s.used      = true;
    s.id        = _lastSessionId;
    s.tcp       = tcp;
    s.lastValid = millis();

    uint8_t response[SESSION_RESPONSE_LEN];
    header(response, SESSION_RESPONSE, sizeof(response));
    response[6] = (uint8_t)(s.id >> 8);
    response[7] = (uint8_t)s.id;
    memcpy(response + 8, serverPublic, 32);
    sessionResponseMac(authCode(), s.id, s.xorKeys, response + 40);

    _send(tcp, response, sizeof(response));

    knxsec::wipe(priv, sizeof(priv));
    knxsec::wipe(shared, sizeof(shared));
    knxsec::wipe(digest, sizeof(digest));

    _stats.sessionsOpened++;
}

uint8_t KnxIpSecure::sessionUnwrap(const uint8_t* frame, size_t length, int8_t tcp,
                                   uint8_t* inner, size_t& innerLength)
{
    using namespace knxsec;

    Wrapper w;
    if (!parseWrapper(frame, length, w)) return NO_SESSION;

    uint8_t index = NO_SESSION;
    for (uint8_t i = 0; i < MAX_SESSIONS; i++)
        if (_sessions[i].used && _sessions[i].id == w.sessionId) { index = i; break; }

    // A session lives in the TCP connection it was made in (3/8/2 8.4.3.5).
    if (index == NO_SESSION || _sessions[index].tcp != tcp) return NO_SESSION;

    Session& s = _sessions[index];

    // 2.2.3.3: at or below the last received number is discarded.
    if (w.sequence < s.nextReceive)
    {
        _stats.replays++;
        return NO_SESSION;
    }

    if (!unwrap(s.key, frame, length, inner, innerLength))
    {
        _stats.macFailures++;
        return NO_SESSION;
    }

    s.nextReceive = w.sequence + 1;

    uint16_t service = serviceType(inner, innerLength);

    switch (service)
    {
        case SESSION_AUTHENTICATE:
            s.lastValid = millis();
            authenticate(index, inner, innerLength);
            return NO_SESSION;

        case SESSION_STATUS:
            if (innerLength != SESSION_STATUS_LEN) return NO_SESSION;

            if (inner[6] == STATUS_CLOSE)
            {
                // E03, A3
                sessionClose(index, STATUS_CLOSE);
            }
            else if (inner[6] == STATUS_KEEPALIVE)
            {
                // E04: A4 authenticated, A6 otherwise
                if (s.authenticated) s.lastValid = millis();
                else sessionClose(index, STATUS_UNAUTHENTICATED);
            }
            return NO_SESSION;

        // 2.2.1.2.2.2 and 2.2.1.4.7: nothing nested, no remote configuration.
        case SECURE_WRAPPER:
        case SESSION_REQUEST:
        case SESSION_RESPONSE:
        case TIMER_NOTIFY:
        case 0x0740: case 0x0741: case 0x0742: case 0x0743:
            return NO_SESSION;

        default:
            break;
    }

    // E05: A6 before authentication, A4 after.
    if (!s.authenticated)
    {
        sessionClose(index, STATUS_UNAUTHENTICATED);
        return NO_SESSION;
    }

    s.lastValid = millis();
    return index;
}

void KnxIpSecure::authenticate(uint8_t index, const uint8_t* inner, size_t length)
{
    using namespace knxsec;

    // 2.2.3.8.5: 24 octets and a zero reserved octet, or discarded.
    if (length != SESSION_AUTH_LEN || inner[6] != 0) return;

    Session& s    = _sessions[index];
    uint8_t  user = inner[7];

    // 2.4.1: reserved users, users without a password hash and a wrong
    // password fail; authenticating twice fails too (E01 in AUTHENTICATED).
    static const uint8_t unset[16] = {0};
    bool ok = !s.authenticated && user >= 1 && user <= 0x7F && user <= _cfg.passwordCount &&
              !equal(_cfg.passwords[user - 1], unset, 16);

    if (ok)
    {
        uint8_t expected[MAC_LEN];
        sessionAuthMac(_cfg.passwords[user - 1], user, s.xorKeys, expected);
        ok = equal(expected, inner + 8, MAC_LEN);
        wipe(expected, sizeof(expected));
    }

    if (!ok)
    {
        _stats.authFailures++;
        sysLog.printf("SECURE: session %u, user %u - authentication failed\n",
                      (unsigned)s.id, (unsigned)user);
        sessionClose(index, STATUS_AUTH_FAILED);
        return;
    }

    s.authenticated = true;
    s.user          = user;
    sendStatus(index, STATUS_AUTH_SUCCESS);

    sysLog.printf("SECURE: session %u authenticated, user %u%s\n", (unsigned)s.id, (unsigned)user,
                  user == 1 ? " (management)" : "");
}

void KnxIpSecure::sendStatus(uint8_t index, uint8_t status)
{
    uint8_t frame[knxsec::SESSION_STATUS_LEN];
    knxsec::header(frame, knxsec::SESSION_STATUS, sizeof(frame));
    frame[6] = status;
    frame[7] = 0;
    sessionSend(index, frame, sizeof(frame));
}

bool KnxIpSecure::sessionSend(uint8_t index, const uint8_t* frame, size_t length)
{
    if (index >= MAX_SESSIONS || !_sessions[index].used) return false;
    if (length + knxsec::WRAPPER_OVERHEAD > sizeof(frameBuffer)) return false;

    Session& s = _sessions[index];

    // Message tag 0 on unicast (2.2.1.3.1).
    size_t total = knxsec::wrap(s.key, s.id, s.sendSequence++, _serial, 0, frame, length, frameBuffer);
    _send(s.tcp, frameBuffer, total);
    return true;
}

void KnxIpSecure::endSession(uint8_t index, uint8_t status)
{
    if (index >= MAX_SESSIONS || !_sessions[index].used) return;

    if (_sessionEnd) _sessionEnd(index, status);

    knxsec::wipe(&_sessions[index], sizeof(Session));
}

void KnxIpSecure::sessionClose(uint8_t index, uint8_t status)
{
    if (index >= MAX_SESSIONS || !_sessions[index].used) return;

    sendStatus(index, status);
    endSession(index, status);
}

// ---------------------------------------------------------------------------
// Routing and its timer (2.2.2)
// ---------------------------------------------------------------------------

uint64_t KnxIpSecure::now() const
{
    return (uint64_t)(esp_timer_get_time() / 1000);
}

uint64_t KnxIpSecure::timerValue() const
{
    return (uint64_t)((int64_t)now() + _timerOffset);
}

void KnxIpSecure::setTimer(uint64_t value)
{
    _timerOffset = (int64_t)value - (int64_t)now();
}

uint32_t KnxIpSecure::syncTolerance() const
{
    return (uint32_t)_cfg.latencyMs * _cfg.syncFraction / 255;
}

static uint32_t uniform(uint32_t low, uint32_t high)
{
    return high > low ? low + esp_random() % (high - low + 1) : low;
}

/*
 * 2.2.4.2: persisted before a value beyond the last stored one plus the
 * interval can leave the device or be taken over from another one. One
 * write per hour of timer time normally.
 */
void KnxIpSecure::persistTimer(bool force)
{
    uint64_t value = timerValue();

    if (!force && value + TIMER_PERSIST_MARGIN < _timerPersisted + TIMER_PERSIST_INTERVAL)
        return;

    Preferences prefs;
    if (prefs.begin(SEC_NS, false))
    {
        prefs.putULong64(KEY_TIMER, value);
        prefs.end();
    }
    _timerPersisted = value;
}

/*
 * 2.2.2.3.2.8. After power-up the timer continues from the stored value plus
 * the persistence interval, and the first TIMER_NOTIFY waits a random time
 * of up to 10 s, so that a whole installation coming back does not flood the
 * backbone. After a configuration change the notify goes out at once.
 */
void KnxIpSecure::restartSync(bool powerUp)
{
    if (powerUp)
    {
        uint64_t stored = 0;
        Preferences prefs;
        if (prefs.begin(SEC_NS, true))
        {
            stored = prefs.getULong64(KEY_TIMER, 0);
            prefs.end();
        }

        setTimer(stored ? stored + TIMER_PERSIST_INTERVAL : 0);
        persistTimer(true);
    }

    _routingOn   = true;
    _authentic   = false;
    _timekeeper  = false;
    _schedUpdate = false;
    _notifyArmed = false;
    _syncWaiting = false;

    uint16_t tag;
    knxsec::random((uint8_t*)&tag, sizeof(tag));
    _syncTag = tag;

    if (powerUp)
    {
        _initialPending = true;
        _initialAt      = millis() + uniform(0, MAX_DELAY_INITIAL_NOTIFY_MS);
    }
    else
    {
        _initialPending = false;
        sendTimerNotify(_syncTag, _serial);
        firstSyncEvent();
    }

    sysLog.println("SECURE: secure routing, acquiring the multicast timer");
}

/*
 * Waiting for an authentic timer starts with the first TIMER_NOTIFY or
 * SECURE_WRAPPER sent or received and lasts maxDelayTimeFollowerUpdateNotify
 * plus twice the latency tolerance.
 */
void KnxIpSecure::firstSyncEvent()
{
    if (_authentic || _syncWaiting) return;

    uint32_t st                = syncTolerance();
    uint32_t maxFollowerUpdate = MIN_DELAY_KEEPER_UPDATE + st + st + 10 * st;

    _syncWaiting  = true;
    _syncDeadline = millis() + maxFollowerUpdate + 2u * _cfg.latencyMs;
}

void KnxIpSecure::reschedule(bool update, uint16_t tag, const uint8_t* serial)
{
    uint32_t st = syncTolerance();

    // 2.2.2.3.2.2
    const uint32_t maxKeeperPeriodic   = MIN_DELAY_KEEPER_PERIODIC + 3 * st;
    const uint32_t minFollowerPeriodic = maxKeeperPeriodic + st;
    const uint32_t maxFollowerPeriodic = minFollowerPeriodic + 10 * st;
    const uint32_t maxKeeperUpdate     = MIN_DELAY_KEEPER_UPDATE + st;
    const uint32_t minFollowerUpdate   = maxKeeperUpdate + st;
    const uint32_t maxFollowerUpdate   = minFollowerUpdate + 10 * st;

    _schedUpdate = update;
    if (update)
    {
        _updateTag = tag;
        memcpy(_updateSerial, serial, 6);
    }

    uint32_t delay = update ? (_timekeeper ? uniform(MIN_DELAY_KEEPER_UPDATE, maxKeeperUpdate)
                                           : uniform(minFollowerUpdate, maxFollowerUpdate))
                            : (_timekeeper ? uniform(MIN_DELAY_KEEPER_PERIODIC, maxKeeperPeriodic)
                                           : uniform(minFollowerPeriodic, maxFollowerPeriodic));

    _notifyAt    = millis() + delay;
    _notifyArmed = true;
}

void KnxIpSecure::sendTimerNotify(uint16_t tag, const uint8_t* serial)
{
    persistTimer(false);

    uint8_t frame[knxsec::TIMER_NOTIFY_LEN];
    knxsec::timerNotify(_cfg.backboneKey, timerValue(), serial, tag, frame);
    _send(-1, frame, sizeof(frame));
    _stats.timerNotifies++;
}

/*
 * The events E01-E08 of 2.2.2.3.2.5 and their actions from the transition
 * table 2.2.2.3.2.7. @return whether a SECURE_WRAPPER is to be accepted
 * (A2); meaningless for a notify.
 */
bool KnxIpSecure::timerCheck(uint64_t received, uint16_t tag, const uint8_t* serial, bool fromNotify)
{
    uint64_t local = timerValue();
    uint32_t st    = syncTolerance();

    if (received > local)
    {
        // E01: A1 + A9 + A3 in either state. E05: A1 + A2, A3 only when periodic.
        setTimer(received);
        persistTimer(false);

        if (fromNotify)
        {
            _timekeeper = false;
            reschedule(false);
        }
        else if (!_schedUpdate)
        {
            reschedule(false);
        }
        return true;
    }

    if (received + st > local)
    {
        // E02: A9 + A3. E06: A2, A3 only when periodic.
        if (fromNotify)
        {
            _timekeeper = false;
            reschedule(false);
        }
        else if (!_schedUpdate)
        {
            reschedule(false);
        }
        return true;
    }

    if (received + _cfg.latencyMs > local)
    {
        // E03: nothing. E07: A2.
        return true;
    }

    // E04/E08: A4 when periodic, nothing when an update is already scheduled.
    if (!_schedUpdate)
        reschedule(true, tag, serial);

    return false;
}

void KnxIpSecure::timerNotify(const uint8_t* frame, size_t length)
{
    using namespace knxsec;

    // 2.2.1.4.5: no TIMER_NOTIFY unless routing is secured; 2.2.2.4.4: 36
    // octets exactly.
    if (!_routingOn || length != TIMER_NOTIFY_LEN) return;

    uint64_t       received = get48(frame + 6);
    const uint8_t* serial   = frame + 12;
    uint16_t       tag      = (uint16_t)((frame[18] << 8) | frame[19]);

    uint8_t mac[MAC_LEN];
    timerNotifyMac(_cfg.backboneKey, received, serial, tag, mac);
    if (!equal(mac, frame + 20, MAC_LEN))
    {
        _stats.macFailures++;
        return;
    }

    if (!_authentic)
    {
        firstSyncEvent();

        // Our own serial number and tag repeated: the answer to the request
        // this device sent, so the value can be trusted at once.
        if (tag == _syncTag && memcmp(serial, _serial, 6) == 0)
        {
            if (received > timerValue()) setTimer(received);
            _authentic   = true;
            _syncWaiting = false;
            _timekeeper  = false;
            persistTimer(true);
            reschedule(false);
            sysLog.println("SECURE: multicast timer synchronised");
            return;
        }
    }

    timerCheck(received, tag, serial, true);
}

bool KnxIpSecure::routingUnwrap(const uint8_t* frame, size_t length, uint8_t* inner, size_t& innerLength)
{
    using namespace knxsec;

    Wrapper w;
    // 2.2.1.4.5: accepted if and only if routing is secured.
    if (!_routingOn || !parseWrapper(frame, length, w) || w.sessionId != 0)
        return false;

    if (!unwrap(_cfg.backboneKey, frame, length, inner, innerLength))
    {
        _stats.macFailures++;
        return false;
    }

    if (!_authentic)
    {
        // 2.2.2.3.2.8: the most recent timer value counts, the content is
        // not processed until the timer is authentic.
        firstSyncEvent();
        if (w.sequence > timerValue()) setTimer(w.sequence);
        wipe(inner, innerLength);
        return false;
    }

    if (!timerCheck(w.sequence, w.tag, w.serial, false))
    {
        _stats.routingStale++;
        return false;
    }

    uint16_t service = serviceType(inner, innerLength);
    if (service < 0x0530 || service > 0x0533)
        return false;

    _stats.routingIn++;
    return true;
}

size_t KnxIpSecure::routingWrap(const uint8_t* frame, size_t length, uint8_t* out)
{
    if (!_routingOn) return 0;

    if (!_authentic)
    {
        // A device that has to send may cut the initial delay short.
        if (_initialPending)
        {
            _initialPending = false;
            sendTimerNotify(_syncTag, _serial);
            firstSyncEvent();
        }
        return 0;
    }

    // E09: A3 when periodic.
    if (!_schedUpdate) reschedule(false);

    persistTimer(false);

    uint16_t tag;
    knxsec::random((uint8_t*)&tag, sizeof(tag));

    _stats.routingOut++;
    return knxsec::wrap(_cfg.backboneKey, 0, timerValue(), _serial, tag, frame, length, out);
}

// ---------------------------------------------------------------------------
// Housekeeping
// ---------------------------------------------------------------------------

void KnxIpSecure::loop(bool networkUp)
{
    uint32_t t = millis();

    // E06 of the session state machine.
    for (uint8_t i = 0; i < MAX_SESSIONS; i++)
    {
        Session& s = _sessions[i];
        if (!s.used) continue;

        uint32_t limit = s.authenticated ? TIMEOUT_SESSION_MS : TIMEOUT_AUTHENTICATION_MS;
        if ((uint32_t)(t - s.lastValid) > limit)
        {
            sysLog.printf("SECURE: session %u timed out\n", (unsigned)s.id);
            sessionClose(i, knxsec::STATUS_TIMEOUT);
        }
    }

    if (routingSecure() && !_routingOn && networkUp)
    {
        restartSync(_poweredUp);
        _poweredUp = false;
    }
    else if (!routingSecure() && _routingOn)
    {
        _routingOn   = false;
        _notifyArmed = false;
        persistTimer(true);
    }

    if (_routingOn)
    {
        if (_initialPending && (int32_t)(t - _initialAt) >= 0)
        {
            _initialPending = false;
            sendTimerNotify(_syncTag, _serial);
            firstSyncEvent();
        }

        if (!_authentic && _syncWaiting && (int32_t)(t - _syncDeadline) >= 0)
        {
            // The most recent value seen stands; nobody answered with a
            // newer one.
            _authentic   = true;
            _syncWaiting = false;
            persistTimer(true);
            reschedule(false);
            sysLog.println("SECURE: multicast timer taken as it is, no newer time on the backbone");
        }

        // E10: A5/A6 + A8 + A3.
        if (_authentic && _notifyArmed && (int32_t)(t - _notifyAt) >= 0)
        {
            _notifyArmed = false;

            if (_schedUpdate)
            {
                sendTimerNotify(_updateTag, _updateSerial);
            }
            else
            {
                uint16_t tag;
                knxsec::random((uint8_t*)&tag, sizeof(tag));
                sendTimerNotify(tag, _serial);
            }

            _timekeeper = true;
            reschedule(false);
        }

        persistTimer(false);
    }

    if (_dirty && (uint32_t)(t - _dirtyAt) > 1000)
    {
        _dirty = false;

        Preferences prefs;
        if (prefs.begin(SEC_NS, false))
        {
            prefs.putBytes(KEY_CFG, &_cfg, sizeof(_cfg));
            prefs.end();
        }
    }
}
