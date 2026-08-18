/*
 *  log_buffer.h - Keeps the serial log so the dashboard can show it.
 *
 *  A device in a distribution board has no serial console attached. Everything
 *  the firmware prints is therefore also kept in a ring buffer, which lives in
 *  PSRAM when the module has some - 64 KB of history costs nothing there,
 *  while the same amount of internal RAM would be a fifth of the heap.
 *
 *  Two sources feed it: everything written through sysLog, and the ESP-IDF
 *  log (the "[E][Preferences.cpp:47]" lines), which is captured by installing
 *  a vprintf hook.
 */
#pragma once

#include <Arduino.h>
#include <stdint.h>

class LogBuffer : public Stream
{
public:
    /** Allocate the ring and start capturing the ESP-IDF log. */
    void begin();

    /** Bytes the ring can hold. */
    size_t capacity() const { return _size; }

    /** Bytes currently held. */
    size_t used() const { return _filled; }

    /** True when the ring lives in PSRAM rather than in internal RAM. */
    bool inPsram() const { return _psram; }

    /**
     * Copy out the newest @p max bytes, oldest first.
     *
     * The caller decides how much it can afford to hold - the whole ring is
     * more than a web response should carry.
     */
    String tail(size_t max) const;

    void clear();

    size_t write(uint8_t value) override;
    size_t write(const uint8_t* data, size_t len) override;

    // Stream, only so the KNX stack accepts this as its debug output. It
    // never reads, and there is nothing here to read.
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }

private:
    void store(const char* data, size_t len);

    static int idfHook(const char* format, va_list args);

    char*  _buf    = nullptr;
    size_t _size   = 0;
    size_t _head   = 0; //!< next write position
    size_t _filled = 0;
    bool   _psram  = false;

    mutable portMUX_TYPE _lock = portMUX_INITIALIZER_UNLOCKED;
};

/**
 * The firmware's log sink.
 *
 * Writes through to Serial, so a console still shows everything live. Used
 * instead of Serial everywhere the firmware reports something.
 */
extern LogBuffer sysLog;
