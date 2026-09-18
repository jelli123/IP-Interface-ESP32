/*
 *  bus_monitor.cpp - Ring of captured KNX telegrams.
 */

#include "bus_monitor.h"

#include <Arduino.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <sys/time.h>

#include "hw_config.h"
#include "knx_link.h"
#include "log_buffer.h"
#include "time_service.h"

BusMonitor busMonitor;

void (*sbipMonitorHook)(uint8_t side, bool outgoing,
                        const uint8_t* cemi, uint16_t length) = nullptr;
bool sbipMonitorSuppress = false;

namespace
{
/** Below this the ring is not worth the PSRAM block. */
const uint32_t MIN_FRAMES = 200;

/*
 * The last frames before a restart, in RTC memory.
 *
 * The ring lives in PSRAM and is allocated anew on every boot, so whatever
 * led up to a restart - an A_Restart from ETS, a crash in the middle of a
 * download - was gone exactly when it mattered. RTC memory keeps its content
 * over a software reset, a panic and a watchdog, as it does for the log.
 * It is small: 64 frames of up to 28 bytes, 2.3 KiB next to the log's 3.
 * Every frame goes in, whether or not the monitor is recording.
 */
const uint32_t TAIL_MAGIC  = 0x53424D54; // "SBMT"
const uint8_t  TAIL_FRAMES = 64;
const uint8_t  TAIL_RAW    = 28;

struct TailEntry
{
    uint32_t ms;
    uint8_t  side;
    uint8_t  outgoing;
    uint8_t  stored;
    uint8_t  length;
    uint8_t  raw[TAIL_RAW];
};

RTC_NOINIT_ATTR uint32_t  s_tailMagic;
RTC_NOINIT_ATTR uint32_t  s_tailHead;
RTC_NOINIT_ATTR uint32_t  s_tailCount;
RTC_NOINIT_ATTR uint64_t  s_tailEpochBase;
RTC_NOINIT_ATTR TailEntry s_tail[TAIL_FRAMES];

/** Offset of CTRL1 within a cEMI frame: message code, then the add-info. */
inline uint16_t ctrlOffset(const uint8_t* cemi)
{
    return 2 + cemi[1];
}
} // namespace

void BusMonitor::begin()
{
#ifdef SBIP_MONITOR_HOOK
    /*
     * Before the ring and regardless of it. Only the recording needs PSRAM;
     * the loop watch and the bus load figure are fed from the same hook and
     * have to work on a board without it.
     */
    sbipMonitorHook = &BusMonitor::hook;
#else
    // The build got here without patch_knx.py managing its anchors, so the
    // stack will never call us. Say so once instead of showing an empty list.
    sysLog.println("Monitor: stack hook missing, no telegrams will be captured");
#endif

    /*
     * PSRAM only, deliberately. The internal heap has some 200 KiB free at
     * this point - taking a slice of that would trade a diagnostic aid
     * against the tunnel connections and the web server, which is the wrong
     * way round. Without PSRAM the ETS bus monitor is still there.
     *
     * The size comes from the hardware profile, which is already loaded when
     * this runs. 48 bytes per frame and some 30 telegrams a second on a busy
     * TP1 line means the default 384 KiB hold about four minutes.
     */
    uint32_t frames = (uint32_t)hwConfig.active().monitorKib * 1024 / sizeof(Entry);

    if (frames < MIN_FRAMES)
    {
        sysLog.println("Monitor: switched off in the hardware profile");
        return;
    }

    _ring = (Entry*)heap_caps_malloc(frames * sizeof(Entry), MALLOC_CAP_SPIRAM);

    if (_ring == nullptr)
    {
        sysLog.println("Monitor: no PSRAM, bus monitor unavailable");
        return;
    }

    _capacity = frames;

    sysLog.printf("Monitor: %u frames in PSRAM (%u KiB)\n",
                  (unsigned)_capacity,
                  (unsigned)(_capacity * sizeof(Entry) / 1024));

    carryOver();
}

void BusMonitor::carryOver()
{
    bool valid = s_tailMagic == TAIL_MAGIC && s_tailHead < TAIL_FRAMES &&
                 s_tailCount <= TAIL_FRAMES;

    uint32_t n     = valid ? s_tailCount : 0;
    uint32_t first = valid ? (s_tailHead + TAIL_FRAMES - n) % TAIL_FRAMES : 0;

    if (n > 0 && _ring != nullptr)
    {
        _priorEpochBase = s_tailEpochBase;

        for (uint32_t i = 0; i < n && i < _capacity; i++)
        {
            const TailEntry& t    = s_tail[(first + i) % TAIL_FRAMES];
            Entry&           slot = _ring[_head];

            slot.ms       = t.ms;
            slot.side     = (uint8_t)((t.side & 0x03) | SIDE_PRIOR);
            slot.outgoing = t.outgoing;
            slot.stored   = (t.stored > TAIL_RAW) ? TAIL_RAW : t.stored;
            slot.length   = t.length;
            memcpy(slot.raw, t.raw, slot.stored);

            _head = (_head + 1) % _capacity;
            _count++;
            _written++;
        }

        sysLog.printf("Monitor: %u frame(s) carried over from before the restart\n",
                      (unsigned)n);
    }

    // Start over: the next boot is to see this run, not the last one.
    s_tailMagic = TAIL_MAGIC;
    s_tailHead  = 0;
    s_tailCount = 0;
}

void BusMonitor::remember(uint8_t side, bool outgoing, const uint8_t* cemi,
                          uint16_t length)
{
    if (s_tailMagic != TAIL_MAGIC || s_tailHead >= TAIL_FRAMES)
    {
        s_tailMagic = TAIL_MAGIC;
        s_tailHead  = 0;
        s_tailCount = 0;
    }

    TailEntry& t = s_tail[s_tailHead];
    t.ms       = millis();
    t.side     = side;
    t.outgoing = outgoing ? 1 : 0;
    t.stored   = (length > TAIL_RAW) ? TAIL_RAW : (uint8_t)length;
    t.length   = (length > 255) ? 255 : (uint8_t)length;
    memcpy(t.raw, cemi, t.stored);

    s_tailHead = (s_tailHead + 1) % TAIL_FRAMES;
    if (s_tailCount < TAIL_FRAMES) s_tailCount++;

    // Once per frame rather than once per boot: the clock may only become
    // valid minutes after the start, when NTP has answered.
    if (TimeService::clockValid())
    {
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        s_tailEpochBase = (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000) -
                          millis();
    }
    else
    {
        s_tailEpochBase = 0;
    }
}

void BusMonitor::hook(uint8_t side, bool outgoing, const uint8_t* cemi, uint16_t length)
{
    // The only place that has the side and the frame length together, so this
    // is where the bus load figure gets its input.
    if (side == SIDE_TP) knxLink.noteBusFrame(cemi, length);
    if (side == SIDE_TUNNEL && !outgoing) knxLink.noteTunnelFrame(cemi, length);

    busMonitor.watchForLoop(side, outgoing, cemi, length);
    if (cemi != nullptr && length >= 3) busMonitor.remember(side, outgoing, cemi, length);
    busMonitor.capture(side, outgoing, cemi, length);
}

/*
 * A second coupler on the same line, found without anyone having to read a
 * recording.
 *
 * Two devices that bridge the same TP line to the same IP network send each
 * other's traffic back. The give-away is a telegram arriving from the bus
 * that is byte for byte what we put out a moment ago, except for the source
 * address - a KNXnet/IP interface substitutes its own tunnel address. Since
 * the source differs, no filter on our own address can catch it, and ETS ends
 * up seeing two answers to every question.
 *
 * Comparing destination and payload rather than the whole frame keeps this
 * blind to the hop count, which the other device has decremented.
 */
void BusMonitor::watchForLoop(uint8_t side, bool outgoing, const uint8_t* cemi,
                              uint16_t length)
{
    uint16_t ctrl = ctrlOffset(cemi);
    if (length < ctrl + 7u) return;

    // Destination, octet count and the first two APDU bytes: enough to tell
    // one telegram from another, short enough to keep in a few bytes.
    uint32_t mark = ((uint32_t)cemi[ctrl + 4] << 24) | ((uint32_t)cemi[ctrl + 5] << 16) |
                    ((uint32_t)cemi[ctrl + 6] << 8) | cemi[length - 1];
    uint16_t source = ((uint16_t)cemi[ctrl + 2] << 8) | cemi[ctrl + 3];

    if (outgoing)
    {
        _sentMark   = mark;
        _sentSource = source;
        _sentAt     = millis();
        return;
    }

    if (side != SIDE_TP || mark != _sentMark || source == _sentSource) return;
    if (_sentAt == 0 || (millis() - _sentAt) > LOOP_WINDOW_MS) return;

    _loops++;

    if (_loopWarnedAt != 0 && (millis() - _loopWarnedAt) < 60000) return;
    _loopWarnedAt = millis();

    sysLog.printf("KNX: telegram loop - %u frame(s) we sent came back from the "
                  "bus as %u.%u.%u. A second device bridges this line to the "
                  "same IP network; ETS will see every answer twice.\n",
                  (unsigned)_loops,
                  (unsigned)(source >> 12), (unsigned)((source >> 8) & 0x0F),
                  (unsigned)(source & 0xFF));
    _loops = 0;
}

bool BusMonitor::fires(const uint8_t* cemi, uint16_t length) const
{
    uint16_t ctrl = ctrlOffset(cemi);

    switch (_trigger)
    {
    case TRG_GA:
        if (length < (uint16_t)(ctrl + 6)) return false;
        // Group addressing only, or a physical address would match by number.
        if ((cemi[ctrl + 1] & 0x80) == 0) return false;
        return (uint16_t)((cemi[ctrl + 4] << 8) | cemi[ctrl + 5]) == _triggerAddress;

    case TRG_REPEAT:
        // The bit is inverted: cleared means this is a repetition.
        return length > ctrl && (cemi[ctrl] & 0x20) == 0;

    case TRG_SECURE:
    {
        if (length < (uint16_t)(ctrl + 9)) return false;
        uint16_t apci = (uint16_t)(((cemi[ctrl + 7] & 0x03) << 8) | cemi[ctrl + 8]);
        return apci == 0x3F1; // SecureService
    }

    default:
        return true;
    }
}

void BusMonitor::capture(uint8_t side, bool outgoing, const uint8_t* cemi, uint16_t length)
{
    if (_ring == nullptr || cemi == nullptr || length < 3) return;

    State state = _state;
    if (state == ST_OFF || state == ST_FULL) return;

    if (side > SIDE_TUNNEL) return;
    if ((_sides & (1 << side)) == 0) return;

    /*
     * While armed the ring keeps running, capped at the pre-trigger count.
     * That is what makes a trigger worth having: the frames that led to the
     * event are the ones nobody can capture afterwards.
     */
    bool armed = (state == ST_ARMED);
    if (armed && _pre == 0 && !fires(cemi, length)) return;

    uint8_t stored = (length > RAW_MAX) ? RAW_MAX : (uint8_t)length;

    portENTER_CRITICAL(&_lock);

    if (_stopWhenFull && _count >= _capacity && !armed)
    {
        _missed++;
        _state = ST_FULL;
        portEXIT_CRITICAL(&_lock);
        return;
    }

    Entry& slot = _ring[_head];
    slot.ms       = millis();
    slot.side     = side;
    slot.outgoing = outgoing ? 1 : 0;
    slot.stored   = stored;
    slot.length   = (length > 255) ? 255 : (uint8_t)length;
    memcpy(slot.raw, cemi, stored);

    uint32_t limit = (armed && _pre > 0 && _pre < _capacity) ? _pre : _capacity;

    _head = (_head + 1) % _capacity;
    if (_count < limit) _count++;
    _written++;

    portEXIT_CRITICAL(&_lock);

    if (armed)
    {
        if (!fires(cemi, length)) return;

        _triggerSeq = _written - 1;
        _triggered  = true;
        _state      = ST_RUNNING;
        return;
    }

    // The post-trigger limit counts the frames after the trigger itself.
    if (_post > 0 && _triggered && (_written - _triggerSeq) > _post)
    {
        _state = ST_FULL;
    }
}

bool BusMonitor::start(uint8_t sides, Trigger trigger, uint16_t address,
                       uint32_t pre, uint32_t post, bool stopWhenFull)
{
    if (_ring == nullptr) return false;

    if (stopWhenFull && _count >= _capacity)
    {
        // Starting would end on the next frame. Say so instead of pretending.
        return false;
    }

    // Off first: a run that is still going would otherwise write into the ring
    // while the side mask and the trigger change underneath it.
    _state = ST_OFF;

    const uint8_t all = WATCH_IP | WATCH_TP | WATCH_TUNNEL;
    _sides          = (sides & all) ? (uint8_t)(sides & all) : all;
    _trigger        = trigger;
    _triggerAddress = address;
    _pre            = (pre > _capacity) ? _capacity : pre;
    _post           = post;
    _stopWhenFull   = stopWhenFull;
    _missed         = 0;
    _triggerSeq     = 0;
    _triggered      = (trigger == TRG_NOW);

    /*
     * Deliberately no clear(): stopping and resuming must not throw away what
     * was captured before, and the sequence numbers stay monotonic across the
     * pause anyway.
     */
    _state = (trigger == TRG_NOW) ? ST_RUNNING : ST_ARMED;
    return true;
}

void BusMonitor::stop()
{
    _state = ST_OFF;
}

void BusMonitor::clear()
{
    portENTER_CRITICAL(&_lock);
    _head    = 0;
    _count   = 0;
    _written = 0;
    portEXIT_CRITICAL(&_lock);

    _triggerSeq = 0;
    _triggered  = false;
}

bool BusMonitor::at(uint32_t seq, Entry& out) const
{
    if (_ring == nullptr) return false;

    portENTER_CRITICAL(&_lock);

    bool ok = (seq < _written) && (seq >= _written - _count);
    if (ok)
    {
        out = _ring[(_head + _capacity - (uint32_t)(_written - seq)) % _capacity];
    }

    portEXIT_CRITICAL(&_lock);
    return ok;
}
