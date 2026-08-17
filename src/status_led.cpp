/*
 *  status_led.cpp - Addressable RGB status LED over the RMT peripheral.
 */

#include <Arduino.h>
#include <Preferences.h>
#include <esp32-hal-rmt.h>

#include "hw_config.h"
#include "status_led.h"

StatusLed statusLed;

static Preferences ledPrefs;
static const char* LED_NS   = "sbip-led";
static const char* KEY_BEAT = "beat";

/*
 * RMT resolution. One tick is 0.1 us, which is what the WS2812 family needs:
 * its bits are 1.25 us long and told apart by how long the line stays high.
 */
static const uint32_t RMT_HZ = 10000000;

/** Flash length and how far apart the flashes are. */
static const uint32_t BEAT_PERIOD_MS = 2000;
static const uint32_t BEAT_ON_MS     = 40;

/*
 * Deliberately not full brightness. These chips are painfully bright at 255,
 * and the point here is a sign of life, not illumination.
 */
static const uint8_t BEAT_LEVEL = 40;

void StatusLed::begin()
{
    const HwProfile& hw = hwConfig.active();

    _pin   = hw.rgbPin;
    _count = (hw.rgbPin >= 0) ? hw.rgbCount : 0;
    _type  = hw.rgbType;

    if (ledPrefs.begin(LED_NS, true))
    {
        _heartbeat = ledPrefs.getBool(KEY_BEAT, false);
        ledPrefs.end();
    }

    if (!present())
    {
        return;
    }

    if (!rmtInit(_pin, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, RMT_HZ))
    {
        Serial.printf("RGB LED: RMT init failed for GPIO %d\n", _pin);
        _count = 0;
        return;
    }

    Serial.printf("RGB LED: %u x %s on GPIO %d, heartbeat %s\n",
                  (unsigned)_count, (_type == SK6812) ? "SK6812" : "WS2812",
                  _pin, _heartbeat ? "on" : "off");

    // A chain powers up in a random state often enough to be worth clearing.
    show(0, 0, 0);
}

void StatusLed::heartbeat(bool enable)
{
    _heartbeat = enable;

    if (ledPrefs.begin(LED_NS, false))
    {
        ledPrefs.putBool(KEY_BEAT, enable);
        ledPrefs.end();
    }

    if (!enable && _lit)
    {
        show(0, 0, 0);
        _lit = false;
    }
}

void StatusLed::loop()
{
    if (!present() || !_heartbeat)
    {
        return;
    }

    uint32_t now     = millis();
    uint32_t elapsed = now - _lastBeat;

    if (!_lit && elapsed >= BEAT_PERIOD_MS)
    {
        show(BEAT_LEVEL, BEAT_LEVEL, BEAT_LEVEL);
        _lit      = true;
        _lastBeat = now;
    }
    else if (_lit && elapsed >= BEAT_ON_MS)
    {
        show(0, 0, 0);
        _lit = false;
    }
}

void StatusLed::show(uint8_t red, uint8_t green, uint8_t blue)
{
    if (!present())
    {
        return;
    }
    writeChain(red, green, blue);
}

/*
 * One RMT transaction for the whole chain.
 *
 * Cascaded LEDs are not addressed: each one keeps the first 24 bits it sees
 * and passes the rest on. Sending 24 bits per LED in separate transactions
 * would therefore paint the first LED over and over, because the gap between
 * two transactions is longer than the reset time.
 */
void StatusLed::writeChain(uint8_t red, uint8_t green, uint8_t blue)
{
    const uint16_t symbols = (uint16_t)_count * 24;

    rmt_data_t* data = (rmt_data_t*)malloc(symbols * sizeof(rmt_data_t));

    if (data == nullptr)
    {
        return;
    }

    // Both chips read green first. The difference is how long a bit stays
    // high; each family is happier with its own timing even though the
    // tolerances overlap.
    const uint16_t zeroHigh = (_type == SK6812) ? 3 : 4;
    const uint16_t oneHigh  = (_type == SK6812) ? 6 : 8;
    const uint16_t period   = 12; // 1.25 us, rounded to whole ticks

    const uint8_t colour[3] = {green, red, blue};
    uint16_t      index     = 0;

    for (uint8_t led = 0; led < _count; led++)
    {
        for (uint8_t byte = 0; byte < 3; byte++)
        {
            for (int8_t bit = 7; bit >= 0; bit--)
            {
                bool     one  = (colour[byte] >> bit) & 0x01;
                uint16_t high = one ? oneHigh : zeroHigh;

                data[index].level0    = 1;
                data[index].duration0 = high;
                data[index].level1    = 0;
                data[index].duration1 = period - high;
                index++;
            }
        }
    }

    rmtWrite(_pin, data, symbols, RMT_WAIT_FOR_EVER);
    free(data);
}
