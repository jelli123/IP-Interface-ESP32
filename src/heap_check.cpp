/*
 *  heap_check.cpp - Diagnosis of heap corruption. See heap_check.h.
 */

#include "heap_check.h"

#if SBIP_HEAP_CHECK

#include <Arduino.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <string.h>

#include "log_buffer.h"

namespace
{

const uint32_t MAGIC = 0x48454150;  // "HEAP"

struct Trace
{
    uint32_t magic;
    char     intact[80];    //!< last place the heap was found intact
    char     checking[80];  //!< the check that was running
};

RTC_NOINIT_ATTR Trace trace;
bool reported = false;

void copy(char* to, const char* where, const char* suffix)
{
    snprintf(to, 80, "%s%s", where, suffix ? suffix : "");
}

void run(const char* where, const char* suffix)
{
    if (reported) return;

    copy(trace.checking, where, suffix);
    trace.magic = MAGIC;

    if (heap_caps_check_integrity(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT, false))
    {
        memcpy(trace.intact, trace.checking, sizeof(trace.intact));
        return;
    }

    reported = true;
    sysLog.printf("HEAP: corrupt at \"%s\", last intact at \"%s\"\n", trace.checking, trace.intact);
}

} // namespace

void sbipHeapCheck(const char* where)
{
    run(where, nullptr);
}

void SbipHeapGuard::check(const char* suffix)
{
    run(name, suffix);
}

void sbipHeapReport()
{
    esp_reset_reason_t reason = esp_reset_reason();
    if (trace.magic == MAGIC && reason != ESP_RST_POWERON && reason != ESP_RST_BROWNOUT)
    {
        trace.intact[sizeof(trace.intact) - 1]     = 0;
        trace.checking[sizeof(trace.checking) - 1] = 0;
        sysLog.printf("HEAP: before this start the last check was \"%s\", last intact at \"%s\"\n",
                      trace.checking, trace.intact);
    }
    memset(&trace, 0, sizeof(trace));
    sysLog.println("HEAP: checks on (SBIP_HEAP_CHECK)");
}

#endif
