/*
 *  ets_access.cpp - Which path may reprogram the device.
 */

#include <Arduino.h>
#include <Preferences.h>

#include "ets_access.h"
#include "log_buffer.h"

EtsAccess etsAccess;

/*
 * Asked by the KNX stack before it delivers a management frame to itself,
 * see patch 17 in scripts/patch_knx.py. Left unset, everything is let in as
 * upstream does.
 */
bool (*sbipManagementHook)(uint8_t path, uint16_t source, bool individual) = nullptr;

static bool managementHook(uint8_t path, uint16_t source, bool individual)
{
    return etsAccess.permits(path, source, individual);
}

static const char* ETS_NS       = "sbip-ets";
static const char* KEY_ALLOW    = "allow";
static const char* KEY_REMEMBER = "remember";

/** A tool that is still talking keeps a temporary unlock open this long. */
static const uint32_t UNLOCK_GRACE_MS = 60UL * 1000UL;

/** One summary line per interval, however many frames were refused. */
static const uint32_t LOG_INTERVAL_MS = 10000;

static String address(uint16_t pa)
{
    return String(pa >> 12) + "." + String((pa >> 8) & 0x0F) + "." + String(pa & 0xFF);
}

const char* EtsAccess::pathName(uint8_t path)
{
    switch (path)
    {
    case PATH_ROUTING: return "routing";
    case PATH_TP:      return "TP";
    case PATH_TUNNEL:  return "tunnel";
    default:           return "?";
    }
}

void EtsAccess::begin()
{
    Preferences prefs;

    if (prefs.begin(ETS_NS, true))
    {
        _allowed    = prefs.getUChar(KEY_ALLOW, ALLOW_ALL) & ALLOW_ALL;
        _remembered = prefs.getUChar(KEY_REMEMBER, ALLOW_ALL) & ALLOW_ALL;
        prefs.end();
    }

    sbipManagementHook = &managementHook;

    if (_allowed != ALLOW_ALL)
    {
        logSetting("stored setting");
    }
}

void EtsAccess::loop()
{
    uint32_t until = _unlockUntil;

    if (until != 0 && (int32_t)(millis() - until) >= 0)
    {
        _unlockUntil = 0;
        sysLog.println("ETS access: temporary unlock over");
        logSetting("back to");
    }

    if (_logSeen != 0 && (uint32_t)(millis() - _logAt) >= LOG_INTERVAL_MS)
    {
        sysLog.printf("ETS access: %u more management frame(s) refused\n",
                      (unsigned)_logSeen);
        _logSeen = 0;
        _logAt   = millis();
    }
}

void EtsAccess::logSetting(const char* by) const
{
    uint8_t mask = _allowed;

    if (mask == 0)
    {
        sysLog.printf("ETS access (%s): locked on every path\n", by);
        return;
    }

    sysLog.printf("ETS access (%s): TP %s, tunnel %s, routing %s\n", by,
                  (mask & ALLOW_TP)      ? "open" : "locked",
                  (mask & ALLOW_TUNNEL)  ? "open" : "locked",
                  (mask & ALLOW_ROUTING) ? "open" : "locked");
}

void EtsAccess::store(uint8_t mask, bool remember)
{
    mask &= ALLOW_ALL;
    _allowed = mask;

    if (remember && mask != 0)
    {
        _remembered = mask;
    }

    Preferences prefs;

    if (prefs.begin(ETS_NS, false))
    {
        prefs.putUChar(KEY_ALLOW, mask);
        prefs.putUChar(KEY_REMEMBER, _remembered);
        prefs.end();
    }
    else
    {
        sysLog.println("ETS access: could not store the setting, it is lost on restart");
    }
}

void EtsAccess::setAllowed(uint8_t mask)
{
    store(mask, true);
    logSetting("dashboard");
}

void EtsAccess::unlock(bool enable)
{
    if (!enable)
    {
        if (_unlockUntil != 0)
        {
            _unlockUntil = 0;
            sysLog.println("ETS access: temporary unlock ended early");
            logSetting("back to");
        }
        return;
    }

    // 0 means "not running", so a deadline that lands on it moves by one.
    _unlockUntil = (millis() + UNLOCK_MS) | 1;
    sysLog.printf("ETS access: every path open for %u minutes\n",
                  (unsigned)(UNLOCK_MS / 60000UL));
}

uint32_t EtsAccess::unlockRemaining() const
{
    uint32_t until = _unlockUntil;

    if (until == 0) return 0;

    int32_t left = (int32_t)(until - millis());
    return left > 0 ? ((uint32_t)left + 999) / 1000 : 0;
}

uint32_t EtsAccess::lastRefusedAge() const
{
    if (_refused == 0) return UINT32_MAX;
    return (millis() - _lastAt) / 1000;
}

void EtsAccess::toggleTp()
{
    store(_allowed ^ ALLOW_TP, false);
    logSetting("button");
}

void EtsAccess::toggleNet()
{
    uint8_t mask = _allowed;

    if (mask & ALLOW_NET)
    {
        _remembered = mask;
        mask &= (uint8_t)~ALLOW_NET;
    }
    else
    {
        uint8_t net = _remembered & ALLOW_NET;
        mask |= net ? net : (uint8_t)ALLOW_NET;
    }

    store(mask, false);
    logSetting("button");
}

void EtsAccess::toggleAll()
{
    uint8_t mask = _allowed;

    if (mask != 0)
    {
        _remembered = mask;
        mask = 0;
    }
    else
    {
        mask = _remembered ? _remembered : (uint8_t)ALLOW_ALL;
    }

    store(mask, false);
    logSetting("button");
}

bool EtsAccess::permits(uint8_t path, uint16_t source, bool individual)
{
    static const uint8_t BIT[PATH_COUNT] = {ALLOW_ROUTING, ALLOW_TP, ALLOW_TUNNEL};

    if (path >= PATH_COUNT) return true;

    if (_allowed & BIT[path]) return true;

    if (_unlockUntil != 0)
    {
        // A tool at work keeps the door open until it is done.
        if (individual)
        {
            uint32_t atLeast = millis() + UNLOCK_GRACE_MS;

            if ((int32_t)(atLeast - _unlockUntil) > 0)
            {
                _unlockUntil = atLeast | 1;
            }
        }
        return true;
    }

    // Broadcasts are refused without a word: every device on the line
    // answers an address read with one, and none of that is an attempt.
    if (!individual) return false;

    _refused    = _refused + 1;
    _lastPath   = path;
    _lastSource = source;
    _lastAt     = millis();

    if (_logAt != 0 && (uint32_t)(millis() - _logAt) < LOG_INTERVAL_MS)
    {
        _logSeen++;
        return false;
    }

    if (source != 0)
    {
        sysLog.printf("ETS access: management from %s %s refused\n",
                      pathName(path), address(source).c_str());
    }
    else
    {
        sysLog.printf("ETS access: property write from a %s client refused\n",
                      pathName(path));
    }

    _logAt = millis();
    return false;
}
