/*
 *  log_buffer.cpp - Ring buffer for the serial log.
 */

#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_system.h>

#include "log_buffer.h"

LogBuffer sysLog;

/*
 * Survives a reset, not a power cycle.
 *
 * RTC slow memory is left alone by the startup code, so anything here is
 * still readable after a watchdog or a panic. 3 KiB of the 8 KiB available
 * leaves room for whatever the IDF wants. The cost per line is a few hundred
 * byte writes over the RTC bus - measured against a log that produces a
 * handful of lines per second, that is not worth a switch being off by
 * default.
 */
static const size_t   RTC_SIZE  = 3072;
static const uint32_t RTC_MAGIC = 0x53424C47; // "SBLG"

RTC_NOINIT_ATTR static uint32_t s_rtcMagic;
RTC_NOINIT_ATTR static uint32_t s_rtcHead;
RTC_NOINIT_ATTR static uint32_t s_rtcFilled;
RTC_NOINIT_ATTR static char     s_rtcBuf[RTC_SIZE];

static Preferences logPrefs;
static const char* LOG_NS   = "sbip-log";
static const char* KEY_RTC  = "rtc";

/*
 * Half a megabyte of the eight the module carries. Enough that a full ETS
 * download stays in the ring even with the KNX stack logging every telegram,
 * and still small enough to hand to a browser in one piece.
 */
static const size_t PSRAM_SIZE = 512 * 1024;

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

    /*
     * Before anything can log.
     *
     * After a power cycle these hold whatever was in the cells, and store()
     * would index the ring with a random 32 bit offset - a write far outside
     * RTC memory. The marker is also the only way to tell a reset, which
     * keeps the content, from a power-up, which does not.
     */
    bool survived = (s_rtcMagic == RTC_MAGIC && s_rtcHead < RTC_SIZE &&
                     s_rtcFilled <= RTC_SIZE);
    size_t carried = survived ? s_rtcFilled : 0;

    if (!survived)
    {
        s_rtcMagic  = RTC_MAGIC;
        s_rtcHead   = 0;
        s_rtcFilled = 0;
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

    if (logPrefs.begin(LOG_NS, false))
    {
        _toRtc = logPrefs.isKey(KEY_RTC) ? logPrefs.getBool(KEY_RTC, true) : true;
        logPrefs.end();
    }

    // Ahead of the first new line, so the window reads as one continuous log
    // across the restart instead of starting blank.
    if (carried > 0)
    {
        String previous = rtcTail();
        appendRaw(previous.c_str(), previous.length());
        appendRaw("\n----- restart -----\n", 21);
    }

    s_previousHook = esp_log_set_vprintf(idfHook);

    printf("Log: %u KiB ring in %s, reset reason %s, %u bytes carried over\n",
           (unsigned)(_size / 1024), _psram ? "PSRAM" : "internal RAM",
           resetReason(), (unsigned)carried);
}

/*
 * Only the newest lines move across.
 *
 * This runs a few dozen milliseconds into the start-up, when the log holds a
 * couple of kilobytes - copying the whole ring instead would buy nothing and
 * would keep the lock held for milliseconds. Lines written by another task
 * between the copy and the swap are lost; at this point in the boot there
 * are none worth the extra locking.
 */
bool LogBuffer::resize(size_t bytes)
{
    static const size_t CARRY_MAX = 16 * 1024;

    if (_buf == nullptr || bytes < INTERNAL_SIZE || bytes == _size)
    {
        return false;
    }

    char* fresh = (char*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);

    if (fresh == nullptr)
    {
        printf("Log: %u KiB do not fit in PSRAM, keeping %u KiB\n",
               (unsigned)(bytes / 1024), (unsigned)(_size / 1024));
        return false;
    }

    size_t carry = _filled;
    if (carry > bytes)     carry = bytes;
    if (carry > CARRY_MAX) carry = CARRY_MAX;

    size_t start = (_head + _size - carry) % _size;

    for (size_t i = 0; i < carry; i++)
    {
        fresh[i] = _buf[(start + i) % _size];
    }

    portENTER_CRITICAL(&_lock);
    char* previous = _buf;
    _buf    = fresh;
    _size   = bytes;
    _head   = (carry < bytes) ? carry : 0;
    _filled = carry;
    _psram  = true;
    portEXIT_CRITICAL(&_lock);

    free(previous);

    printf("Log: ring resized to %u KiB in PSRAM\n", (unsigned)(bytes / 1024));
    return true;
}

/*
 * Straight into the main ring: no timestamp, and not mirrored back into the
 * RTC ring the text came from.
 */void LogBuffer::appendRaw(const char* data, size_t len)
{
    if (_buf == nullptr || len == 0) return;

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
    _written += len;

    portEXIT_CRITICAL(&_lock);
}

/*
 * Named rather than numbered: this is the first line anyone reads after an
 * unexplained restart, and ESP_RST_TASK_WDT says more than a 6.
 */
const char* LogBuffer::resetReason()
{
    switch (esp_reset_reason())
    {
    case ESP_RST_POWERON:  return "power-on";
    case ESP_RST_EXT:      return "reset pin";
    case ESP_RST_SW:       return "software";
    case ESP_RST_PANIC:    return "panic";
    case ESP_RST_INT_WDT:  return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT:      return "other watchdog";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO:     return "SDIO";
    default:               return "unknown";
    }
}

void LogBuffer::keepAcrossReset(bool enable)
{
    _toRtc = enable;

    if (logPrefs.begin(LOG_NS, false))
    {
        logPrefs.putBool(KEY_RTC, enable);
        logPrefs.end();
    }
}

size_t LogBuffer::rtcUsed() const
{
    return (s_rtcMagic == RTC_MAGIC) ? s_rtcFilled : 0;
}

String LogBuffer::rtcTail() const
{
    if (s_rtcMagic != RTC_MAGIC || s_rtcFilled == 0)
    {
        return String();
    }

    size_t want  = s_rtcFilled;
    size_t start = (s_rtcHead + RTC_SIZE - want) % RTC_SIZE;

    String out;
    out.reserve(want + 1);

    for (size_t i = 0; i < want; i++)
    {
        out += s_rtcBuf[(start + i) % RTC_SIZE];
    }

    return out;
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

                if (_toRtc)
                {
                    s_rtcBuf[s_rtcHead] = stamp[j];
                    s_rtcHead = (s_rtcHead + 1) % RTC_SIZE;
                    if (s_rtcFilled < RTC_SIZE) s_rtcFilled++;
                }
            }
            _filled = (_filled + stampLen > _size) ? _size : _filled + stampLen;
            _written += stampLen;
            _atLineStart = false;
        }

        _buf[_head] = c;
        _head = (_head + 1) % _size;
        if (_filled < _size) _filled++;
        _written++;

        if (_toRtc)
        {
            s_rtcBuf[s_rtcHead] = c;
            s_rtcHead = (s_rtcHead + 1) % RTC_SIZE;
            if (s_rtcFilled < RTC_SIZE) s_rtcFilled++;
        }

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

size_t LogBuffer::copyFrom(uint64_t& at, char* out, size_t max) const
{
    if (_buf == nullptr || max == 0)
    {
        return 0;
    }

    portENTER_CRITICAL(&_lock);

    uint64_t first = _written - _filled;

    // Overtaken while reading: pick up at the oldest byte that is still here
    // rather than handing out whatever moved into that slot.
    if (at < first) at = first;

    size_t want = 0;

    if (at < _written)
    {
        uint64_t available = _written - at;
        want = (available < max) ? (size_t)available : max;

        size_t start    = (size_t)((at - first + (_head + _size - _filled)) % _size);
        size_t untilEnd = _size - start;

        if (want <= untilEnd)
        {
            memcpy(out, _buf + start, want);
        }
        else
        {
            memcpy(out, _buf + start, untilEnd);
            memcpy(out + untilEnd, _buf, want - untilEnd);
        }
    }

    portEXIT_CRITICAL(&_lock);

    at += want;
    return want;
}

void LogBuffer::clear()
{
    portENTER_CRITICAL(&_lock);
    _head        = 0;
    _filled      = 0;
    _written     = 0;
    _atLineStart = true;
    portEXIT_CRITICAL(&_lock);
}
