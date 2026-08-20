/*
 *  cpu_load.h - How busy each processor core is.
 *
 *  FreeRTOS keeps a run time counter per task, driven by esp_timer. The idle
 *  task of a core accumulates exactly the time that core had nothing to do,
 *  so the load follows from what is left of the wall clock.
 *
 *  That works for core 0 only. The Arduino main task is pinned to core 1 and
 *  polls without ever blocking, so the idle task there never runs and the
 *  core reads 100 % for as long as the device is powered. True, but empty of
 *  information - hence the second measurement below, which asks how much of
 *  the main loop's time is work rather than polling.
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

    /**
     * One turn of the main loop. Call at the top of loop().
     *
     * Costs a single timer read; everything derived from it is worked out
     * once a second in loop().
     */
    void pass();

    uint8_t cores() const { return _cores; }

    /** Load over the last second, in per mille. */
    uint16_t permille(uint8_t core) const;

    /** Highest value seen since the last reset. */
    uint16_t peak(uint8_t core) const;

    /** Main loop utilisation over the last second, in per mille. */
    uint16_t loopPermille() const { return _loopLoad; }

    /** Highest main loop utilisation since the last reset. */
    uint16_t loopPeak() const { return _loopPeak; }

    /** Longest single turn of the main loop since the last reset, in us. */
    uint32_t loopMaxUs() const { return _loopMaxPeak; }

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
     * Main loop accounting, all in microseconds.
     *
     * A turn that finds nothing to do still costs something - the polling
     * itself. The shortest turn of a window is taken as that floor, and only
     * what the other turns spend on top of it counts as work.
     */
    uint32_t _lastPass    = 0;
    uint32_t _passes      = 0;
    uint32_t _passSpan    = 0;
    uint32_t _passMin     = 0;
    uint16_t _loopLoad    = 0;
    uint16_t _loopPeak    = 0;
    uint32_t _loopMaxPeak = 0;

    /*
     * Reused between samples. uxTaskGetSystemState() wants room for every
     * task at once, and asking for that from the heap once a second is a
     * needless way to fragment it.
     */
    void*        _states   = nullptr;
    unsigned int _capacity = 0;
};

extern CpuLoad cpuLoad;
