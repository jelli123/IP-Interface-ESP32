/*
 *  bus_monitor.cpp - Ring of captured KNX telegrams.
 */

#include "bus_monitor.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include "log_buffer.h"

BusMonitor busMonitor;

void (*sbipMonitorHook)(uint8_t side, bool outgoing,
                        const uint8_t* cemi, uint16_t length) = nullptr;
bool sbipMonitorSuppress = false;

namespace
{
/*
 * 8000 frames, 384 KiB. A busy TP1 line carries some 30 telegrams a second,
 * so this holds roughly four minutes of continuous traffic - long enough to
 * catch what an ETS download or a misbehaving actuator does, and a rounding
 * error against the 2 MB the smallest PSRAM module carries.
 */
const uint32_t RING_FRAMES = 8000;

/** Offset of CTRL1 within a cEMI frame: message code, then the add-info. */
inline uint16_t ctrlOffset(const uint8_t* cemi)
{
    return 2 + cemi[1];
}
} // namespace

void BusMonitor::begin()
{
    /*
     * PSRAM only, deliberately. The ring is worth 384 KiB and the internal
     * heap has some 200 KiB free at this point - taking a slice of that would
     * trade a diagnostic aid against the tunnel connections and the web
     * server, which is the wrong way round. Without PSRAM the ETS bus monitor
     * is still there.
     */
    _ring = (Entry*)heap_caps_malloc(RING_FRAMES * sizeof(Entry), MALLOC_CAP_SPIRAM);

    if (_ring == nullptr)
    {
        sysLog.println("Monitor: no PSRAM, bus monitor unavailable");
        return;
    }

    _capacity = RING_FRAMES;

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

uint16_t BusMonitor::destinationOf(const uint8_t* cemi, uint16_t length)
{
    uint16_t ctrl = ctrlOffset(cemi);

    // CTRL1, CTRL2, source, destination
    if (length < (uint16_t)(ctrl + 6)) return 0;

    return (uint16_t)((cemi[ctrl + 4] << 8) | cemi[ctrl + 5]);
}

void BusMonitor::capture(uint8_t side, bool outgoing, const uint8_t* cemi, uint16_t length)
{
    if (_ring == nullptr || cemi == nullptr || length < 3) return;

    State state = _state;
    if (state == ST_OFF || state == ST_FULL) return;

    if ((_sides & (1 << (side & 1))) == 0) return;

    if (state == ST_ARMED)
    {
        if (destinationOf(cemi, length) != _trigger) return;
        _state = ST_RUNNING;
    }

    uint8_t stored = (length > RAW_MAX) ? RAW_MAX : (uint8_t)length;

    portENTER_CRITICAL(&_lock);

    if (_stopWhenFull && _count >= _capacity)
    {
        _missed++;
        _state = ST_FULL;
        portEXIT_CRITICAL(&_lock);
        return;
    }

    Entry& slot = _ring[_head];
    slot.ms       = millis();
    slot.side     = side & 1;
    slot.outgoing = outgoing ? 1 : 0;
    slot.stored   = stored;
    slot.length   = (length > 255) ? 255 : (uint8_t)length;
    memcpy(slot.raw, cemi, stored);

    _head = (_head + 1) % _capacity;
    if (_count < _capacity) _count++;
    _written++;

    portEXIT_CRITICAL(&_lock);
}

void BusMonitor::start(uint8_t sides, uint16_t trigger, bool stopWhenFull)
{
    if (_ring == nullptr) return;

    // Off first: a run that is still going would otherwise write into the ring
    // while it is being cleared, and into the old side mask while it changes.
    _state = ST_OFF;

    _sides        = (sides & (WATCH_IP | WATCH_TP)) ? sides : (WATCH_IP | WATCH_TP);
    _trigger      = trigger;
    _stopWhenFull = stopWhenFull;
    _missed       = 0;

    // A fresh run starts on an empty ring: mixing the frames of two runs in
    // one list makes the sequence numbers say something they do not mean.
    clear();

    _state = trigger ? ST_ARMED : ST_RUNNING;
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
