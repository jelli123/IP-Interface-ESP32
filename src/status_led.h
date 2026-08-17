/*
 *  status_led.h - Status LEDs from the hardware profile.
 *
 *  Two kinds of LED live behind one interface. A plain one is a GPIO that is
 *  either on or off. An addressable one is a position in a WS2812 / SK6812
 *  chain, where a single data line carries every LED on it.
 *
 *  That difference matters more than it looks: a chain cannot be addressed
 *  per LED. Each chip keeps the first colour it sees and passes the rest on,
 *  so the whole chain has to be sent in one go whenever anything on it
 *  changes. Everything here is therefore built around a per-chain frame
 *  buffer that is flushed once per update.
 */
#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "hw_config.h"

class StatusLed
{
public:
    /**
     * Claim the pins from the hardware profile and switch everything off.
     *
     * Safe to call on a board that has no LEDs configured.
     */
    void begin();

    /** Drives the assigned functions. Call from the main loop. */
    void loop();

    /** @return true if at least one LED is configured */
    bool present() const { return _ledCount > 0; }

    /** @return true if an LED carries the heartbeat function */
    bool hasHeartbeat() const { return _heartbeatLed >= 0; }

    /**
     * A short white flash every two seconds, as a sign of life.
     *
     * Persisted, so it survives a restart. Off by default - a blinking LED in
     * a distribution board is not everyone's idea of helpful.
     */
    void heartbeat(bool enable);
    bool heartbeat() const { return _heartbeat; }

private:
    /** One addressable chain, identified by its data pin. */
    struct Chain
    {
        int8_t   pin     = -1;
        uint8_t  type    = HW_RGB_WS2812;
        uint8_t  length  = 0;
        uint8_t* colours = nullptr; //!< length * 3 bytes, GRB order
        bool     dirty   = false;
    };

    void   paint(int8_t led, uint8_t red, uint8_t green, uint8_t blue);
    void   flush();
    void   writeChain(Chain& chain);
    int8_t chainFor(int8_t pin) const;

    static const uint8_t MAX_CHAINS = 4;

    Chain   _chains[MAX_CHAINS];
    uint8_t _chainCount = 0;
    uint8_t _ledCount   = 0;

    int8_t _progLed      = -1;
    int8_t _heartbeatLed = -1;

    bool     _heartbeat = false;
    bool     _beatLit   = false;
    uint32_t _lastBeat  = 0;
    bool     _progLit   = false;
    uint32_t _lastProg  = 0;
};

extern StatusLed statusLed;
