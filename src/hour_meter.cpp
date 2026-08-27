/*
 *  hour_meter.cpp - Operating hours across restarts.
 */

#include "hour_meter.h"

#include <Arduino.h>
#include <Preferences.h>

#include "log_buffer.h"
#include "rv3028.h"
#include "time_service.h"

HourMeter hourMeter;

namespace
{
Preferences hourPrefs;
const char* HOUR_NS   = "sbip-hours";
const char* KEY_HOURS = "hours";
const char* KEY_START = "starts";

/** Byte 0 of the user RAM. Byte 1 is left alone for whatever comes next. */
const uint8_t RAM_SLOT = 0;

/*
 * Ten days between flushes: 255 fits in the byte with room to spare, thirty
 * years of it are some 1100 writes, and a dead backup cell costs ten days of
 * a reading that counts in years.
 */
const uint8_t FLUSH_HOURS = 240;

const uint32_t HOUR_MS  = 3600UL * 1000UL;
const uint32_t CHECK_MS = 60UL * 1000UL;
} // namespace

void HourMeter::begin()
{
    if (hourPrefs.begin(HOUR_NS, false))
    {
        _total  = hourPrefs.isKey(KEY_HOURS) ? hourPrefs.getUInt(KEY_HOURS, 0) : 0;
        _starts = hourPrefs.isKey(KEY_START) ? hourPrefs.getUInt(KEY_START, 0) : 0;

        _starts++;
        hourPrefs.putUInt(KEY_START, _starts);
        hourPrefs.end();
    }

    _hourBase  = millis();
    _lastCheck = millis();
}

/*
 * The RTC is probed by TimeService well after this class starts, and a module
 * fitted later is picked up without a reboot - so keep asking rather than
 * deciding once.
 */
void HourMeter::attach()
{
    Rv3028* rtc = timeService.rtc();
    if (rtc == nullptr) return;

    uint8_t pending = 0;
    if (!rtc->readRam(RAM_SLOT, pending)) return;

    _rtc = rtc;

    /*
     * A fresh chip or a flat backup cell takes the RAM with it, and what the
     * chip powers up with is not specified - 0xFF would add ten days to the
     * total at every single restart. Two things rule that out:
     *
     *   the power-on reset flag, which the chip sets whenever the supply
     *   including the backup dropped out and only clears when the clock is
     *   written, and
     *
     *   a value above the flush limit, which cannot arise in normal operation
     *   because flush() runs at exactly that point.
     */
    bool held = rtc->timeValid();

    if (!held || pending > FLUSH_HOURS)
    {
        if (pending != 0)
        {
            sysLog.printf("Hours: RTC RAM not trustworthy (%u h, %s), discarded\n",
                          (unsigned)pending, held ? "out of range" : "power was lost");
        }
        pending = 0;
        rtc->writeRam(RAM_SLOT, 0);
    }

    _pending  = pending;
    _hourBase = millis();

    // Straight into NVS: what the RAM carried has now survived a restart, and
    // from here a dead backup cell can only cost the hours after this point.
    flush();

    sysLog.printf("Hours: %lu h total, start number %lu\n",
                  (unsigned long)hours(), (unsigned long)_starts);
}

void HourMeter::flush()
{
    // Nothing to carry over means nothing to write - a device whose RTC RAM
    // was lost must not touch the stored total just by starting up.
    if (_pending == 0) return;

    _total  += _pending;
    _pending = 0;

    if (hourPrefs.begin(HOUR_NS, false))
    {
        hourPrefs.putUInt(KEY_HOURS, _total);
        hourPrefs.end();
    }

    if (_rtc != nullptr) _rtc->writeRam(RAM_SLOT, 0);
}

void HourMeter::loop()
{
    if ((uint32_t)(millis() - _lastCheck) < CHECK_MS) return;
    _lastCheck = millis();

    if (_rtc == nullptr)
    {
        attach();
        return;
    }

    uint32_t due = (uint32_t)(millis() - _hourBase) / HOUR_MS;
    if (due == 0) return;

    // Advance the base by whole hours only, or the remainder would be dropped
    // every time and the counter would run slow.
    _hourBase += due * HOUR_MS;

    uint32_t grown = _pending + due;
    _pending = (grown > 255) ? 255 : (uint8_t)grown;

    _rtc->writeRam(RAM_SLOT, _pending);

    if (_pending >= FLUSH_HOURS) flush();
}

void HourMeter::reset()
{
    _total     = 0;
    _pending   = 0;
    _starts    = 0;
    _hourBase  = millis();
    _lastCheck = millis();

    if (hourPrefs.begin(HOUR_NS, false))
    {
        hourPrefs.putUInt(KEY_HOURS, 0);
        hourPrefs.putUInt(KEY_START, 0);
        hourPrefs.end();
    }

    if (_rtc != nullptr) _rtc->writeRam(RAM_SLOT, 0);

    sysLog.println("Hours: operating time counter reset");
}
