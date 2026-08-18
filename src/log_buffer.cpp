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

/*
 * Wall clock once it has been set, uptime before that. The leading + says
 * which one you are looking at, so a line from before the first NTP answer
 * cannot be mistaken for a time of day.
 */
size_t LogBuffer::makeStamp(char* out, size_t max)
{
    time_t now = time(nullptr);

    if (now > 1600000000) // any plausible date rather than 1970
    {
        struct tm local;
        localtime_r(&now, &local);
        return (size_t)snprintf(out, max, "%02d:%02d:%02d ",
                                local.tm_hour, local.tm_min, local.tm_sec);
    }

    uint32_t seconds = millis() / 1000;
    return (size_t)snprintf(out, max, "+%02u:%02u:%02u ",
                            (unsigned)((seconds / 3600) % 100),
                            (unsigned)((seconds / 60) % 60),
                            (unsigned)(seconds % 60));
}

void LogBuffer::store(const char* data, size_t len)
{
    if (_buf == nullptr || len == 0)
    {
        return;
    }

    // Built before the lock: localtime_r takes one of its own.
    char   stamp[16];
    size_t stampLen = makeStamp(stamp, sizeof(stamp));

    portENTER_CRITICAL(&_lock);

    for (size_t i = 0; i < len; i++)
    {
        char c = data[i];

        if (_atLineStart && c != '\n' && c != '\r')
        {
            for (size_t j = 0; j < stampLen; j++)
            {
                _buf[_head] = stamp[j];
                _head = (_head + 1) % _size;
            }
            _filled = (_filled + stampLen > _size) ? _size : _filled + stampLen;
            _atLineStart = false;
        }

        _buf[_head] = c;
        _head = (_head + 1) % _size;
        if (_filled < _size) _filled++;

        if (c == '\n') _atLineStart = true;
    }

    portEXIT_CRITICAL(&_lock);
}

size_t LogBuffer::tailStart(size_t want, size_t& available) const
{
    if (_buf == nullptr || _filled == 0 || want == 0)
    {
        available = 0;
        return 0;
    }

    portENTER_CRITICAL(&_lock);
    available = (want < _filled) ? want : _filled;
    size_t start = (_head + _size - available) % _size;
    portEXIT_CRITICAL(&_lock);

    return start;
}

size_t LogBuffer::readAt(size_t& pos, size_t& left, char* out, size_t max) const
{
    if (_buf == nullptr || left == 0 || max == 0)
    {
        return 0;
    }

    size_t want = (left < max) ? left : max;

    portENTER_CRITICAL(&_lock);

    size_t untilEnd = _size - pos;

    if (want <= untilEnd)
    {
        memcpy(out, _buf + pos, want);
    }
    else
    {
        memcpy(out, _buf + pos, untilEnd);
        memcpy(out + untilEnd, _buf, want - untilEnd);
    }

    portEXIT_CRITICAL(&_lock);

    pos = (pos + want) % _size;
    left -= want;

    return want;
}

void LogBuffer::clear()
{
    portENTER_CRITICAL(&_lock);
    _head        = 0;
    _filled      = 0;
    _atLineStart = true;
    portEXIT_CRITICAL(&_lock);
}
