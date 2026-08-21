/*
 *  hour_meter.h - Operating hours across restarts.
 *
 *  The Laufzeit on the dashboard is the time since the last start, which says
 *  nothing about how long the device has been in service. This one keeps
 *  counting over restarts and power cuts, and only a deliberate reset puts it
 *  back to zero.
 */
#pragma once

#include <stdint.h>

class HourMeter
{
public:
    /** Read the stored total and count this start. */
    void begin();

    /** Flush to NVS now and then. Call from the main task. */
    void loop();

    /** Seconds accumulated since the last reset, including this run. */
    uint32_t seconds() const;

    /** How often the device has started since the last reset. */
    uint32_t starts() const { return _starts; }

    void reset();

private:
    void flush();

    uint32_t _stored    = 0; //!< seconds already written to NVS
    uint32_t _base      = 0; //!< millis() the unwritten remainder counts from
    uint32_t _starts    = 0;
    uint32_t _lastFlush = 0;
};

extern HourMeter hourMeter;
