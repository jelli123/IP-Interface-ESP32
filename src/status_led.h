/*
 *  status_led.h - Addressable RGB status LED (WS2812 / SK6812).
 *
 *  The S3 and C6 DevKitC boards carry one of these on a single data line
 *  instead of a plain indicator LED, which is why SBIP_LED_PIN is -1 there:
 *  digitalWrite() does nothing visible on a chip that wants a timed bit
 *  stream. This drives it over the RMT peripheral, which produces the timing
 *  in hardware and needs no cycle-accurate bit banging.
 *
 *  Pin, chain length and chip type come from the hardware profile, so a board
 *  with several cascaded LEDs works without a rebuild.
 */
#pragma once

#include <Arduino.h>
#include <stdint.h>

class StatusLed
{
public:
    /** Chip type. Only the bit timing differs; both use GRB byte order. */
    enum Type : uint8_t
    {
        WS2812 = 0, //!< T0H 0.4 us, T1H 0.8 us
        SK6812 = 1  //!< T0H 0.3 us, T1H 0.6 us
    };

    /**
     * Claim the pin from the hardware profile and clear the chain.
     *
     * Does nothing when the profile has no LED pin, so every board can run
     * the same firmware.
     */
    void begin();

    /** Drives the heartbeat. Call from the main loop. */
    void loop();

    /** @return true if a chain is configured and usable */
    bool present() const { return _count > 0 && _pin >= 0; }

    /**
     * A short white flash every two seconds, as a sign of life.
     *
     * Persisted, so it survives a restart. Off by default - a blinking LED in
     * a distribution board is not everyone's idea of helpful.
     */
    void heartbeat(bool enable);
    bool heartbeat() const { return _heartbeat; }

    /**
     * Paint the whole chain in one colour.
     *
     * Blocks for roughly 30 us per LED while the RMT peripheral clocks the
     * bits out. Call from the main task only.
     */
    void show(uint8_t red, uint8_t green, uint8_t blue);

private:
    void writeChain(uint8_t red, uint8_t green, uint8_t blue);

    int8_t   _pin       = -1;
    uint8_t  _count     = 0;
    uint8_t  _type      = WS2812;
    bool     _heartbeat = false;
    bool     _lit       = false;
    uint32_t _lastBeat  = 0;
};

extern StatusLed statusLed;
