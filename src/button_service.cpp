/*
 *  button_service.cpp - Buttons from the hardware profile.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <nvs_flash.h>

#include "button_service.h"
#include "hw_config.h"
#include "interface_config.h"
#include "knx_link.h"
#include "net_manager.h"

#include "log_buffer.h"
ButtonService buttonService;

/** Contact bounce settles well inside this. */
static const uint32_t DEBOUNCE_MS = 30;

/** A press shorter than this is noise rather than intent. */
static const uint32_t SHORT_MIN_MS = 40;

/** Upper bound for a short press, so holding does not also fire it. */
static const uint32_t SHORT_MAX_MS = 1000;

static const uint32_t LONG_MS      = 2000;
static const uint32_t VERY_LONG_MS = 6000;

int8_t ButtonService::pinSlot(int8_t gpio)
{
    for (uint8_t i = 0; i < _pinCount; i++)
    {
        if (_pins[i].gpio == gpio) return (int8_t)i;
    }
    return -1;
}

void ButtonService::begin()
{
    const HwProfile& hw = hwConfig.active();

    for (uint8_t i = 0; i < hw.buttonCount && i < HW_MAX_BUTTONS; i++)
    {
        int8_t gpio = hw.buttons[i].pin;

        if (gpio < 0 || pinSlot(gpio) >= 0)
        {
            continue; // already claimed by another row on the same pin
        }

        pinMode(gpio, INPUT_PULLUP);
        _pins[_pinCount].gpio = gpio;
        _pinCount++;
    }

    // Let the pull-ups settle before the first sample, otherwise a floating
    // input reads as pressed and fires something on the spot.
    if (_pinCount > 0)
    {
        delay(5);

        for (uint8_t i = 0; i < _pinCount; i++)
        {
            _pins[i].raw     = (digitalRead(_pins[i].gpio) == LOW);
            _pins[i].pressed = _pins[i].raw;
        }
    }

    sysLog.printf("Buttons: %u rows on %u pin%s\n", (unsigned)hw.buttonCount,
                  (unsigned)_pinCount, (_pinCount == 1) ? "" : "s");
}

void ButtonService::loop()
{
    uint32_t now = millis();

    for (uint8_t i = 0; i < _pinCount; i++)
    {
        Pin& p = _pins[i];
        bool raw = (digitalRead(p.gpio) == LOW);

        if (raw != p.raw)
        {
            p.raw       = raw;
            p.changedAt = now;
            continue;
        }
        if ((uint32_t)(now - p.changedAt) < DEBOUNCE_MS)
        {
            continue;
        }

        if (raw && !p.pressed)
        {
            p.pressed   = true;
            p.pressedAt = now;
            p.fired     = 0;
        }
        else if (!raw && p.pressed)
        {
            uint32_t held = now - p.pressedAt;
            p.pressed = false;

            // Short presses can only be judged on release - until then the
            // press might still turn into a long one.
            if (held >= SHORT_MIN_MS && held < SHORT_MAX_MS && p.fired == 0)
            {
                fire(i, HW_PRESS_SHORT);
            }
        }
        else if (raw && p.pressed)
        {
            uint32_t held = now - p.pressedAt;

            if (held >= VERY_LONG_MS && !(p.fired & (1 << HW_PRESS_VERY_LONG)))
            {
                fire(i, HW_PRESS_VERY_LONG);
            }
            else if (held >= LONG_MS && !(p.fired & (1 << HW_PRESS_LONG)))
            {
                fire(i, HW_PRESS_LONG);
            }
        }
    }
}

void ButtonService::fire(uint8_t slot, uint8_t trigger)
{
    const HwProfile& hw = hwConfig.active();

    _pins[slot].fired |= (uint8_t)(1 << trigger);

    for (uint8_t i = 0; i < hw.buttonCount && i < HW_MAX_BUTTONS; i++)
    {
        const HwButton& b = hw.buttons[i];

        if (b.pin != _pins[slot].gpio || b.trigger != trigger)
        {
            continue;
        }

        for (uint8_t j = 0; j < hw.btnAssignCount && j < HW_MAX_BTN_ASSIGN; j++)
        {
            if (strncmp(hw.btnAssign[j].target, b.name, HW_NAME_MAX + 1) == 0)
            {
                dispatch(hw.btnAssign[j].function, b.name);
            }
        }
    }
}

void ButtonService::dispatch(uint8_t function, const char* name)
{
    switch (function)
    {
    case HW_BTNF_PROG_MODE:
        sysLog.printf("Button %s: programming mode\n", name);
        knxLink.requestProgMode(!knxLink.progMode());
        break;

    case HW_BTNF_WIFI_SETUP:
        // Nothing to provision over a cable, and the KNX routing socket is
        // bound to that interface - opening an access point would only make
        // the device unreachable for a while.
        if (netManager.isEthernetMode() || netManager.isApMode())
        {
            sysLog.printf("Button %s: WiFi setup not applicable\n", name);
            break;
        }
        sysLog.printf("Button %s: starting provisioning AP\n", name);
        WiFi.disconnect();
        netManager.startAccessPoint();
        if (knxLink.progMode())
        {
            knxLink.requestProgMode(false);
        }
        break;

    case HW_BTNF_REBOOT:
        sysLog.printf("Button %s: restarting\n", name);
        netManager.scheduleReboot();
        break;

    case HW_BTNF_WIFI_TOGGLE:
        // Without a second interface this would take the device off the
        // network for good - there is no button that brings it back, because
        // the profile itself lives behind the web interface.
        if (netManager.wifiEnabled() && !netManager.wifiCanBeDisabled())
        {
            sysLog.printf("Button %s: WiFi stays on, no Ethernet chip found\n", name);
            break;
        }
        // Applied on the next boot. Switching the radio at runtime means a
        // mode change while the AP or the KNX multicast socket is up, which
        // this firmware has already been bitten by once.
        sysLog.printf("Button %s: WiFi %s after restart\n", name,
                      netManager.wifiEnabled() ? "off" : "on");
        netManager.setWifiEnabled(!netManager.wifiEnabled());
        netManager.scheduleReboot();
        break;

    case HW_BTNF_FACTORY:
        /*
         * Everything goes: hardware profile, WiFi credentials, KNX
         * configuration and every other namespace. Erasing the whole NVS
         * partition is the only way to be sure nothing survives that would
         * put the device back into the state the user is trying to leave.
         *
         * Restarting right here rather than through scheduleReboot(): after
         * the erase every open Preferences handle points at a partition that
         * no longer holds what it thinks, and the two second delay would be
         * two seconds of code running on top of that.
         */
        sysLog.printf("Button %s: factory reset\n", name);
        Serial.flush();
        nvs_flash_erase();
        delay(100);
        ESP.restart();
        break;

    default:
        break;
    }
}
