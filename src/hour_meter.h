/*
 *  hour_meter.h - Operating hours across restarts.
 *
 *  The Laufzeit on the dashboard is the time since the last start, which says
 *  nothing about how long the device has been in service.
 *
 *  Two stores, and the split is what keeps both cheap:
 *
 *    RV-3028 user RAM   hours since the last flush, one byte, written every
 *                       hour. Real RAM, kept by the same backup supply as the
 *                       clock, with no endurance limit at all.
 *    NVS                the total, written every ten days and at every start -
 *                       about 1100 writes over thirty years.
 *
 *  Losing the backup supply therefore costs at most ten days of counting, not
 *  the whole reading, and the flash never sees more than a handful of writes a
 *  year. Without an RTC there is nowhere to keep the running hours that a
 *  restart would not clear, so the counter reports itself unavailable.
 */
#pragma once

#include <stdint.h>

class Rv3028;

class HourMeter
{
public:
    /** Read the total. The RTC is picked up later, in loop(). */
    void begin();

    /** Roll the hour over when it is due. Call from the main task. */
    void loop();

    /** @return true if an RTC answered and the count is being kept */
    bool available() const { return _rtc != nullptr; }

    /** Whole hours since the last reset. */
    uint32_t hours() const { return _total + _pending; }

    /** How often the device has started since the last reset. */
    uint32_t starts() const { return _starts; }

    void reset();

private:
    void attach();
    void flush();

    Rv3028*  _rtc       = nullptr;
    uint32_t _total     = 0; //!< hours already written to NVS
    uint8_t  _pending   = 0; //!< hours held in the RTC, not yet in NVS
    uint32_t _starts    = 0;
    uint32_t _hourBase  = 0; //!< millis() the running hour counts from
    uint32_t _lastCheck = 0;
};

extern HourMeter hourMeter;
