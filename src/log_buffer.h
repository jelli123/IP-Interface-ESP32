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

    /**
     * Move the ring into a PSRAM block of @p bytes, carrying the newest lines.
     *
     * begin() has to run before anything can log, which is before the
     * hardware profile is known - so the size the user chose can only be
     * applied afterwards. Fails without PSRAM and when the block does not
     * fit; the ring then stays as it was.
     */
    bool resize(size_t bytes);

    /** Bytes the ring can hold. */
    size_t capacity() const { return _size; }

    /** Bytes currently held. */
    size_t used() const { return _filled; }

    /** True when the ring lives in PSRAM rather than in internal RAM. */
    bool inPsram() const { return _psram; }

    /**
     * Start position of the newest @p want bytes.
     *
     * @param available receives how many bytes are really there
     * @return an opaque position for readAt()
     */
    size_t tailStart(size_t want, size_t& available) const;

    /**
     * Copy up to @p max bytes from @p pos, advancing @p pos and @p left.
     *
     * Reading in pieces keeps a full dump off the heap. The ring keeps
     * filling meanwhile, so a dump that is overtaken by new output shows the
     * seam - which beats holding 64 KiB twice to avoid it.
     */
    size_t readAt(size_t& pos, size_t& left, char* out, size_t max) const;

    /**
     * Bytes ever written, counting from the first one.
     *
     * The ring holds [oldest(), written()). Positions stay meaningful after
     * older text has been dropped, which is what lets the dashboard scroll
     * through the buffer instead of only reading its end.
     */
    uint64_t written() const { return _written; }

    /** Position of the oldest byte still held. */
    uint64_t oldest() const { return _written - _filled; }

    /**
     * Copy from an absolute position, clamped to what is still there.
     *
     * @param at  advanced by the number of bytes copied
     * @return bytes copied, 0 once @p at has caught up with written()
     */
    size_t copyFrom(uint64_t& at, char* out, size_t max) const;

    void clear();

    /**
     * Also keep the newest lines in RTC slow memory, which a reset does not
     * clear - the only way to see what happened just before a watchdog or a
     * panic restarted the device. Persisted; a power cycle loses it anyway.
     */
    /** Why the device last restarted, as a word rather than a number. */
    static const char* resetReason();

    void keepAcrossReset(bool enable);
    bool keepAcrossReset() const { return _toRtc; }

    /** Bytes currently held in the reset-proof ring. */
    size_t rtcUsed() const;

    /** The reset-proof ring, oldest first. Small enough to hand over whole. */
    String rtcTail() const;

    size_t write(uint8_t value) override;
    size_t write(const uint8_t* data, size_t len) override;

    // Stream, only so the KNX stack accepts this as its debug output. It
    // never reads, and there is nothing here to read.
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }

private:
    void store(const char* data, size_t len);
    void appendRaw(const char* data, size_t len);

    static size_t makeStamp(char* out, size_t max);
    static int    idfHook(const char* format, va_list args);

    char*  _buf    = nullptr;
    size_t _size   = 0;
    size_t _head   = 0; //!< next write position
    size_t _filled = 0;
    uint64_t _written = 0;
    bool   _psram  = false;
    bool   _atLineStart = true;
    bool   _toRtc  = true;

    mutable portMUX_TYPE _lock = portMUX_INITIALIZER_UNLOCKED;
};

/**
 * The firmware's log sink.
 *
 * Writes through to Serial, so a console still shows everything live. Used
 * instead of Serial everywhere the firmware reports something.
 */
extern LogBuffer sysLog;
