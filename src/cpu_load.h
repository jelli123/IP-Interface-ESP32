/*
 *  cpu_load.h - How busy each processor core is.
 *
 *  FreeRTOS keeps a run time counter per task, driven by esp_timer. The idle
 *  task of a core accumulates exactly the time that core had nothing to do,
 *  so the load follows from what is left of the wall clock.
 */
#pragma once

#include <stdint.h>

class CpuLoad
{
public:
    static const uint8_t MAX_CORES = 2;

    /** Find the idle tasks and take the first sample. */
    void begin();

    /** Sample once a second. Call from the main task. */
    void loop();

    uint8_t cores() const { return _cores; }

    /** Load over the last second, in per mille. */
    uint16_t permille(uint8_t core) const;

    /** Highest value seen since the last reset. */
    uint16_t peak(uint8_t core) const;

    /** Seconds since the peaks were cleared. */
    uint32_t peakAge() const;

    void resetPeaks();

private:
    uint8_t  _cores       = 1;
    void*    _idle[MAX_CORES] = {nullptr, nullptr};

    uint32_t _lastIdle[MAX_CORES] = {0, 0};
    uint16_t _load[MAX_CORES]     = {0, 0};
    uint16_t _peak[MAX_CORES]     = {0, 0};

    uint32_t _lastTotal  = 0;
    uint32_t _lastSample = 0;
    uint32_t _peaksAt    = 0;
    bool     _primed     = false;

    /*
     * Reused between samples. uxTaskGetSystemState() wants room for every
     * task at once, and asking for that from the heap once a second is a
     * needless way to fragment it.
     */
    void*        _states   = nullptr;
    unsigned int _capacity = 0;
};

extern CpuLoad cpuLoad;
