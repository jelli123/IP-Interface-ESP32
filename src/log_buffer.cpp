/*
 *  log_buffer.cpp - Ring buffer for the serial log.
 */

#include <esp_heap_caps.h>
#include <esp_log.h>

#include "log_buffer.h"

LogBuffer sysLog;

/** Generous, because PSRAM has room to spare. */
static const size_t PSRAM_SIZE = 64 * 1024;

/** Without PSRAM the heap is the limit, so keep it to the recent past. */
static const size_t INTERNAL_SIZE = 4 * 1024;

/** Longest single ESP-IDF line taken over; the rest is cut. */
static const size_t IDF_LINE_MAX = 256;

static vprintf_like_t s_previousHook = nullptr;

void LogBuffer::begin()
{
    if (_buf != nullptr)
    {
        return;
    }

    _buf = (char*)heap_caps_malloc(PSRAM_SIZE, MALLOC_CAP_SPIRAM);

    if (_buf != nullptr)
    {
        _size  = PSRAM_SIZE;
        _psram = true;
    }
    else
    {
        _buf  = (char*)malloc(INTERNAL_SIZE);
        _size = (_buf != nullptr) ? INTERNAL_SIZE : 0;
    }

    if (_buf == nullptr)
    {
        Serial.println("Log: no memory for the ring buffer");
        return;
    }

    s_previousHook = esp_log_set_vprintf(idfHook);

    printf("Log: %u KiB ring in %s\n", (unsigned)(_size / 1024),
           _psram ? "PSRAM" : "internal RAM");
}

/*
 * Captures the ESP-IDF log without swallowing it: the previous hook still
 * writes to the console, we only take a copy on the way past.
 */
int LogBuffer::idfHook(const char* format, va_list args)
{
    char    line[IDF_LINE_MAX];
    va_list copy;

    va_copy(copy, args);
    int written = vsnprintf(line, sizeof(line), format, copy);
    va_end(copy);

    if (written > 0)
    {
        size_t len = (size_t)written;
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        sysLog.store(line, len);
    }

    return (s_previousHook != nullptr) ? s_previousHook(format, args) : 0;
}

size_t LogBuffer::write(uint8_t value)
{
    char c = (char)value;
    store(&c, 1);
    return Serial.write(value);
}

size_t LogBuffer::write(const uint8_t* data, size_t len)
{
    store((const char*)data, len);
    return Serial.write(data, len);
}

void LogBuffer::store(const char* data, size_t len)
{
    if (_buf == nullptr || len == 0)
    {
        return;
    }

    // A single write longer than the ring can only leave its tail behind.
    if (len > _size)
    {
        data += len - _size;
        len = _size;
    }

    portENTER_CRITICAL(&_lock);

    size_t untilEnd = _size - _head;

    if (len <= untilEnd)
    {
        memcpy(_buf + _head, data, len);
    }
    else
    {
        memcpy(_buf + _head, data, untilEnd);
        memcpy(_buf, data + untilEnd, len - untilEnd);
    }

    _head   = (_head + len) % _size;
    _filled = (_filled + len > _size) ? _size : _filled + len;

    portEXIT_CRITICAL(&_lock);
}

String LogBuffer::tail(size_t max) const
{
    if (_buf == nullptr || _filled == 0 || max == 0)
    {
        return String();
    }

    size_t want = (max < _filled) ? max : _filled;

    // Allocated up front: growing a String inside the critical section would
    // call malloc with interrupts disabled.
    char* copy = (char*)malloc(want + 1);

    if (copy == nullptr)
    {
        return String();
    }

    portENTER_CRITICAL(&_lock);

    size_t start    = (_head + _size - want) % _size;
    size_t untilEnd = _size - start;

    if (want <= untilEnd)
    {
        memcpy(copy, _buf + start, want);
    }
    else
    {
        memcpy(copy, _buf + start, untilEnd);
        memcpy(copy + untilEnd, _buf, want - untilEnd);
    }

    portEXIT_CRITICAL(&_lock);

    copy[want] = '\0';
    String out(copy);
    free(copy);

    return out;
}

void LogBuffer::clear()
{
    portENTER_CRITICAL(&_lock);
    _head   = 0;
    _filled = 0;
    portEXIT_CRITICAL(&_lock);
}
