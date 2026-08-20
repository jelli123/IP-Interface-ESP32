/*
 *  cpu_load.cpp - How busy each processor core is.
 *
 *  The run time counter is esp_timer in microseconds, truncated to 32 bit,
 *  so it wraps roughly every 71 minutes. Every figure here is a difference
 *  between two samples a second apart, and unsigned arithmetic carries that
 *  across the wrap on its own.
 *
 *  The main loop is measured separately, see pass() - the idle task of the
 *  core it runs on never gets scheduled, so that core reads 100 % forever.
 */

#include "cpu_load.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/idf_additions.h>

CpuLoad cpuLoad;

namespace
{
const uint32_t SAMPLE_MS = 1000;

/** Room for tasks created between counting them and asking for their state. */
const unsigned int SPARE = 6;
} // namespace

void CpuLoad::begin()
{
#ifdef CONFIG_FREERTOS_NUMBER_OF_CORES
    _cores = CONFIG_FREERTOS_NUMBER_OF_CORES;
#else
    _cores = 1;
#endif
    if (_cores > MAX_CORES) _cores = MAX_CORES;

    for (uint8_t c = 0; c < _cores; c++)
    {
        _idle[c] = (void*)xTaskGetIdleTaskHandleForCore(c);
    }

    _lastSample = millis();
    _peaksAt    = millis();
    _lastPass   = micros();
}

void CpuLoad::pass()
{
    uint32_t now = micros();
    uint32_t dt  = now - _lastPass;
    _lastPass    = now;

    if (_passes == 0 || dt < _passMin) _passMin = dt;
    if (dt > _loopMaxPeak) _loopMaxPeak = dt;

    _passes++;
    _passSpan += dt;
}

void CpuLoad::loop()
{
    uint32_t now = millis();
    if ((uint32_t)(now - _lastSample) < SAMPLE_MS) return;
    _lastSample = now;

    /*
     * What the main loop did with its second.
     *
     * Every turn costs the polling itself even when there is nothing to do,
     * and the shortest turn of the window is exactly that price. Only the
     * time the other turns needed on top of it is work.
     */
    if (_passes > 0 && _passSpan > 0)
    {
        uint64_t floorTime = (uint64_t)_passes * _passMin;
        uint64_t work      = (floorTime >= _passSpan) ? 0 : (_passSpan - floorTime);

        _loopLoad = (uint16_t)((work * 1000u) / _passSpan);
        if (_loopLoad > _loopPeak) _loopPeak = _loopLoad;
    }
    _passes   = 0;
    _passSpan = 0;

    unsigned int needed = uxTaskGetNumberOfTasks() + SPARE;
    if (needed > _capacity)
    {
        void* grown = realloc(_states, needed * sizeof(TaskStatus_t));
        if (grown == nullptr) return;
        _states   = grown;
        _capacity = needed;
    }

    TaskStatus_t* states = (TaskStatus_t*)_states;
    uint32_t      total  = 0;
    UBaseType_t   count  = uxTaskGetSystemState(states, _capacity, &total);

    uint32_t elapsed = total - _lastTotal;
    _lastTotal       = total;

    for (uint8_t c = 0; c < _cores; c++)
    {
        uint32_t idle = _lastIdle[c];

        for (UBaseType_t i = 0; i < count; i++)
        {
            if ((void*)states[i].xHandle == _idle[c])
            {
                idle = states[i].ulRunTimeCounter;
                break;
            }
        }

        uint32_t busy = 0;
        if (elapsed > 0)
        {
            uint32_t idleDelta = idle - _lastIdle[c];

            // Accounting granularity lets the idle share overshoot the window
            // by a few microseconds; that is not negative load.
            busy = (idleDelta >= elapsed) ? 0
                                          : ((elapsed - idleDelta) * 1000u) / elapsed;
        }

        _lastIdle[c] = idle;
        _load[c]     = (uint16_t)(busy > 1000 ? 1000 : busy);

        if (_load[c] > _peak[c]) _peak[c] = _load[c];
    }

    // The first window reaches back to boot, where the startup burst would
    // otherwise sit in the peak for as long as the device runs.
    if (!_primed)
    {
        _primed = true;
        resetPeaks();
    }
}

uint16_t CpuLoad::permille(uint8_t core) const
{
    return (core < _cores) ? _load[core] : 0;
}

uint16_t CpuLoad::peak(uint8_t core) const
{
    return (core < _cores) ? _peak[core] : 0;
}

uint32_t CpuLoad::peakAge() const
{
    return (millis() - _peaksAt) / 1000;
}

void CpuLoad::resetPeaks()
{
    for (uint8_t c = 0; c < MAX_CORES; c++) _peak[c] = _load[c];
    _loopPeak    = _loopLoad;
    _loopMaxPeak = 0;
    _peaksAt     = millis();
}
