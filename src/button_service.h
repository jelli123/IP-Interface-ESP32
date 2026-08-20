/*
 *  button_service.h - Buttons from the hardware profile.
 *
 *  Each row in the profile is one (pin, trigger) pair with a name, and an
 *  assignment ties that name to a function. Several rows may share a pin,
 *  which is how a short press and a long press on the same button end up
 *  doing different things.
 *
 *  Long and very long presses fire while the button is still held, not on
 *  release. Anything that erases settings should tell the user it happened
 *  before they let go, otherwise the only feedback is a device that has
 *  already forgotten its configuration.
 */
#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "hw_config.h"

class ButtonService
{
public:
    /** Configure the pins listed in the hardware profile. */
    void begin();

    /** Samples the buttons and runs what they are assigned to. */
    void loop();

private:
    /** Debounced state of one physical pin. */
    struct Pin
    {
        int8_t   gpio      = -1;
        bool     pressed   = false;
        bool     raw       = false;
        uint32_t changedAt = 0;
        uint32_t pressedAt = 0;
        uint8_t  fired     = 0; //!< bit per trigger already handled
    };

    void   dispatch(uint8_t function, const char* name);
    int8_t pinSlot(int8_t gpio);
    void   fire(uint8_t slot, uint8_t trigger);

    Pin     _pins[HW_MAX_BUTTONS];
    uint8_t _pinCount = 0;
};

extern ButtonService buttonService;
