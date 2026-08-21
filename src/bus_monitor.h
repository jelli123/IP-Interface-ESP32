/*
 *  bus_monitor.h - Ring of captured KNX telegrams.
 *
 *  The patched data link layer calls sbipMonitorHook for every frame that
 *  crosses a medium, in either direction and on either side. That runs on the
 *  main task, inside knx.loop(), where the TP-UART is waiting for its answer -
 *  so capture() does nothing but a bounds check and a memcpy of the raw cEMI.
 *  Turning those bytes into addresses and payload is the web handler's job.
 */
#pragma once

#include <stdint.h>

#include <freertos/FreeRTOS.h>

/*
 * Defined here rather than in the stack: scripts/patch_knx.py only adds the
 * extern declaration and the three call sites, so a patch that no longer
 * applies costs the monitor its input instead of breaking the build.
 */
extern void (*sbipMonitorHook)(uint8_t side, bool outgoing,
                               const uint8_t* cemi, uint16_t length);
extern bool sbipMonitorSuppress;

class BusMonitor
{
public:
    /** Matches the stack's network layer entity index. */
    enum Side : uint8_t
    {
        SIDE_IP = 0,
        SIDE_TP = 1
    };

    enum Sides : uint8_t
    {
        WATCH_IP = 1 << SIDE_IP,
        WATCH_TP = 1 << SIDE_TP
    };

    enum State : uint8_t
    {
        ST_OFF = 0,  //!< not recording
        ST_ARMED,    //!< waiting for the trigger
        ST_RUNNING,
        ST_FULL      //!< stopped itself, the ring or the post count ran out
    };

    /**
     * What starts the recording.
     *
     * The ETS bus monitor offers more, and most of it cannot be had here: the
     * SB-Interface acknowledges autonomously and drops invalid frames before
     * the stack sees them, so acknowledged, not acknowledged, negatively
     * acknowledged and invalid never reach us. See the README.
     */
    enum Trigger : uint8_t
    {
        TRG_NOW = 0,  //!< start straight away
        TRG_GA,       //!< a telegram to a group address
        TRG_REPEAT,   //!< a repeated frame
        TRG_SECURE    //!< a KNX Data Secure frame
    };

    /**
     * Longest cEMI prefix kept per frame.
     *
     * A standard L_Data frame with the full 16 octet APDU is 25 bytes, so
     * nothing that fits on TP1 in one telegram is ever cut. Only extended
     * frames are, and those carry a length field that says so.
     */
    static const uint8_t RAW_MAX = 40;

    struct Entry
    {
        uint32_t ms;       //!< millis() when the frame was seen
        uint8_t  side;
        uint8_t  outgoing;
        uint8_t  stored;   //!< bytes in raw
        uint8_t  length;   //!< cEMI length before truncation
        uint8_t  raw[RAW_MAX];
    };

    /** Allocate the ring. Without PSRAM the monitor stays unavailable. */
    void begin();

    bool     available() const { return _ring != nullptr; }
    uint32_t capacity() const { return _capacity; }
    State    state() const { return _state; }

    /** Frames captured since the last clear(), counting those overwritten. */
    uint32_t written() const { return _written; }
    uint32_t count() const { return _count; }

    /** Sequence number of the oldest frame still held. */
    uint32_t oldest() const { return _written - _count; }

    uint8_t  sides() const { return _sides; }
    Trigger  trigger() const { return _trigger; }
    uint16_t triggerAddress() const { return _triggerAddress; }
    uint32_t preTrigger() const { return _pre; }
    uint32_t postTrigger() const { return _post; }
    bool     stopWhenFull() const { return _stopWhenFull; }

    /** Sequence number of the frame that fired the trigger, 0 when none did. */
    uint32_t triggerSeq() const { return _triggerSeq; }

    /** Has the trigger fired in this run? */
    bool triggered() const { return _triggered; }

    /** Frames seen while the ring was already full and set to stop. */
    uint32_t missed() const { return _missed; }

    /**
     * Begin recording, or wait for the trigger before counting from it.
     *
     * Keeps what is already in the ring: stopping and resuming has to be
     * possible without losing the reason you stopped for.
     *
     * @param pre   frames kept from before the trigger, 0 for none
     * @param post  frames after the trigger before it stops, 0 for no limit
     * @return false when the ring is full and set to stop - clear it first
     */
    bool start(uint8_t sides, Trigger trigger, uint16_t address,
               uint32_t pre, uint32_t post, bool stopWhenFull);
    void stop();

    /** Drop everything captured so far. Recording state is untouched. */
    void clear();

    /**
     * Copy one frame by absolute sequence number.
     *
     * @return false once @p seq has been overwritten or not reached yet
     */
    bool at(uint32_t seq, Entry& out) const;

private:
    static void hook(uint8_t side, bool outgoing, const uint8_t* cemi, uint16_t length);
    void capture(uint8_t side, bool outgoing, const uint8_t* cemi, uint16_t length);

    /** Does this raw cEMI frame fire the configured trigger? */
    bool fires(const uint8_t* cemi, uint16_t length) const;

    Entry*   _ring     = nullptr;
    uint32_t _capacity = 0;
    uint32_t _head     = 0;
    uint32_t _count    = 0;
    uint32_t _written  = 0;
    uint32_t _missed   = 0;

    volatile State _state = ST_OFF;
    uint8_t  _sides         = WATCH_IP | WATCH_TP;
    Trigger  _trigger       = TRG_NOW;
    uint16_t _triggerAddress = 0;
    uint32_t _pre           = 0;
    uint32_t _post          = 0;
    uint32_t _triggerSeq    = 0;
    bool     _triggered     = false;
    bool     _stopWhenFull  = false;

    mutable portMUX_TYPE _lock = portMUX_INITIALIZER_UNLOCKED;
};

extern BusMonitor busMonitor;
