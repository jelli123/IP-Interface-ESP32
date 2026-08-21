/*
 *  hour_meter.cpp - Operating hours across restarts.
 */

#include "hour_meter.h"

#include <Arduino.h>
#include <Preferences.h>

#include "log_buffer.h"

HourMeter hourMeter;

namespace
{
Preferences hourPrefs;
const char* HOUR_NS   = "sbip-hours";
const char* KEY_SECS  = "secs";
const char* KEY_START = "starts";

/*
 * Written every quarter of an hour, not every second.
 *
 * The counter shares its flash with the WiFi credentials and the hardware
 * profile, and NVS rewrites a whole entry per put. A quarter hour costs some
 * 35000 writes a year - nothing against the endurance of the part - while a
 * power cut loses at most fifteen minutes of an hour meter. That is the right
 * way round.
 */
const uint32_t FLUSH_MS = 15UL * 60UL * 1000UL;
} // namespace

void HourMeter::begin()
{
    if (hourPrefs.begin(HOUR_NS, false))
    {
        _stored = hourPrefs.isKey(KEY_SECS) ? hourPrefs.getUInt(KEY_SECS, 0) : 0;
        _starts = hourPrefs.isKey(KEY_START) ? hourPrefs.getUInt(KEY_START, 0) : 0;

        _starts++;
        hourPrefs.putUInt(KEY_START, _starts);
        hourPrefs.end();
    }

    _base      = millis();
    _lastFlush = millis();

    sysLog.printf("Hours: %lu h total, start number %lu\n",
                  (unsigned long)(_stored / 3600), (unsigned long)_starts);
}

uint32_t HourMeter::seconds() const
{
    return _stored + (uint32_t)(millis() - _base) / 1000;
}

void HourMeter::flush()
{
    uint32_t now  = millis();
    uint32_t ran  = (uint32_t)(now - _base) / 1000;

    // Keep the sub-second remainder out of _base, or a flush every quarter
    // hour would lose up to a second each time.
    _stored += ran;
    _base   += ran * 1000;

    if (hourPrefs.begin(HOUR_NS, false))
    {
        hourPrefs.putUInt(KEY_SECS, _stored);
        hourPrefs.end();
    }
}

void HourMeter::loop()
{
    if ((uint32_t)(millis() - _lastFlush) < FLUSH_MS) return;
    _lastFlush = millis();
    flush();
}

void HourMeter::reset()
{
    _stored    = 0;
    _starts    = 0;
    _base      = millis();
    _lastFlush = millis();

    if (hourPrefs.begin(HOUR_NS, false))
    {
        hourPrefs.putUInt(KEY_SECS, 0);
        hourPrefs.putUInt(KEY_START, 0);
        hourPrefs.end();
    }

    sysLog.println("Hours: operating time counter reset");
}
