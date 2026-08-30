/*
 *  bus_monitor.cpp - Ring of captured KNX telegrams.
 */

#include "bus_monitor.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include "hw_config.h"
#include "log_buffer.h"

BusMonitor busMonitor;

void (*sbipMonitorHook)(uint8_t side, bool outgoing,
                        const uint8_t* cemi, uint16_t length) = nullptr;
bool sbipMonitorSuppress = false;

namespace
{
/** Below this the ring is not worth the PSRAM block. */
const uint32_t MIN_FRAMES = 200;

/** Offset of CTRL1 within a cEMI frame: message code, then the add-info. */
inline uint16_t ctrlOffset(const uint8_t* cemi)
{
    return 2 + cemi[1];
}
} // namespace

void BusMonitor::begin()
{
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

#ifdef SBIP_MONITOR_HOOK
    sbipMonitorHook = &BusMonitor::hook;
    sysLog.printf("Monitor: %u frames in PSRAM (%u KiB)\n",
                  (unsigned)_capacity,
                  (unsigned)(_capacity * sizeof(Entry) / 1024));
#else
    // The build got here without patch_knx.py managing its anchors, so the
    // stack will never call us. Say so once instead of showing an empty list.
    sysLog.println("Monitor: stack hook missing, no telegrams will be captured");
#endif
}

void BusMonitor::hook(uint8_t side, bool outgoing, const uint8_t* cemi, uint16_t length)
{
    busMonitor.capture(side, outgoing, cemi, length);
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
