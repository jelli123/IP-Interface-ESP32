/*
 *  status_led.cpp - Status LEDs, plain GPIO and addressable chains.
 */

#include <Arduino.h>
#include <Preferences.h>
#include <esp32-hal-rmt.h>

#include "hw_config.h"
#include "knx_link.h"
#include "net_manager.h"
#include "status_led.h"

StatusLed statusLed;

static Preferences ledPrefs;
static const char* LED_NS   = "sbip-led";
static const char* KEY_BEAT = "beat";

/*
 * RMT resolution. One tick is 0.1 us, which is what the WS2812 family needs:
 * its bits are 1.25 us long and told apart by how long the line stays high.
 */
static const uint32_t RMT_HZ     = 10000000;
static const uint16_t BIT_PERIOD = 12; //!< 1.25 us in ticks

/** Flash length and how far apart the flashes are. */
static const uint32_t FLASH_PERIOD_MS = 2000;
static const uint32_t FLASH_ON_MS     = 40;

/*
 * Deliberately not full brightness. These chips are painfully bright at 255,
 * and the point here is an indicator, not illumination.
 */
static const uint8_t L = 48;

/** Palette, indexed by HwLedColour. */
static const uint8_t PALETTE[HW_COL_COUNT][3] = {
    { L, 0, 0 }, // red
    { 0, L, 0 }, // green
    { 0, 0, L }, // blue
    { L, L, 0 }, // yellow
    { 0, L, L }, // cyan
    { L, 0, L }, // magenta
    { L, L, L }, // white
};

int8_t StatusLed::chainFor(int8_t pin) const
{
    for (uint8_t i = 0; i < _chainCount; i++)
    {
        if (_chains[i].pin == pin) return (int8_t)i;
    }
    return -1;
}

void StatusLed::begin()
{
    const HwProfile& hw = hwConfig.active();

    _ledCount     = hw.ledCount;
    _hasHeartbeat = hw.usesCondition(HW_COND_HEARTBEAT);

    if (ledPrefs.begin(LED_NS, true))
    {
        _heartbeat = ledPrefs.getBool(KEY_BEAT, false);
        ledPrefs.end();
    }

    for (uint8_t i = 0; i < hw.ledCount && i < HW_MAX_LEDS; i++)
    {
        const HwLed& led = hw.leds[i];

        if (led.kind == HW_LED_PLAIN)
        {
            pinMode(led.pin, OUTPUT);
            digitalWrite(led.pin, led.activeLow ? HIGH : LOW);
            continue;
        }

        // Group addressable LEDs by data pin: rows that share a pin are
        // positions on one physical chain and must be sent together.
        int8_t at = chainFor(led.pin);

        if (at < 0)
        {
            if (_chainCount >= MAX_CHAINS)
            {
                Serial.printf("LED %s: no free RMT chain, ignored\n", led.name);
                continue;
            }
            at = (int8_t)_chainCount++;
            _chains[at].pin  = led.pin;
            _chains[at].type = led.rgbType;
        }

        if (led.rgbIndex >= _chains[at].length)
        {
            _chains[at].length = (uint8_t)(led.rgbIndex + 1);
        }
    }

    for (uint8_t i = 0; i < _chainCount; i++)
    {
        Chain& c = _chains[i];

        c.colours = (uint8_t*)calloc((size_t)c.length * 3, 1);

        if (c.colours == nullptr || !rmtInit(c.pin, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, RMT_HZ))
        {
            Serial.printf("RGB chain on GPIO %d: setup failed\n", c.pin);
            free(c.colours);
            c.colours = nullptr;
            c.length  = 0;
            continue;
        }

        Serial.printf("RGB chain: %u x %s on GPIO %d\n", (unsigned)c.length,
                      (c.type == HW_RGB_SK6812) ? "SK6812" : "WS2812", c.pin);

        // A chain powers up in a random state often enough to be worth
        // clearing before anything else writes to it.
        c.dirty = true;
    }

    flush();

    Serial.printf("LEDs: %u configured, %u states, heartbeat %s\n",
                  (unsigned)_ledCount, (unsigned)hw.ledAssignCount,
                  _hasHeartbeat ? (_heartbeat ? "on" : "off") : "unassigned");
}

void StatusLed::heartbeat(bool enable)
{
    _heartbeat = enable;

    if (ledPrefs.begin(LED_NS, false))
    {
        ledPrefs.putBool(KEY_BEAT, enable);
        ledPrefs.end();
    }
}

bool StatusLed::conditionHolds(uint8_t condition) const
{
    switch (condition)
    {
    case HW_COND_PROG_MODE: return knxLink.progMode();
    case HW_COND_AP_MODE:   return netManager.isApMode();
    case HW_COND_TP_DOWN:   return !knxLink.tpConnected();
    case HW_COND_ONLINE:    return netManager.isOnline() && knxLink.tpConnected();
    case HW_COND_OFFLINE:   return !netManager.isOnline() && !netManager.isApMode();
    case HW_COND_HEARTBEAT: return _heartbeat;
    default:                return false;
    }
}

bool StatusLed::patternOn(uint8_t pattern, uint32_t now)
{
    uint32_t phase = now % 1000;

    switch (pattern)
    {
    case HW_PAT_STEADY:     return true;
    case HW_PAT_BLINK_SLOW: return phase < 500;
    case HW_PAT_BLINK_FAST: return (now % 200) < 100;
    case HW_PAT_DOUBLE:     return (phase < 100) || (phase > 200 && phase < 300);
    case HW_PAT_FLASH:      return (now % FLASH_PERIOD_MS) < FLASH_ON_MS;
    default:                return false;
    }
}

/*
 * Per LED, the first assignment whose condition holds.
 *
 * The list order is the priority, which is the whole user facing model: a row
 * further up hides everything below it for that LED. No implicit ranking
 * between "functions" - what the user sees is what they arranged.
 */
void StatusLed::loop()
{
    const HwProfile& hw  = hwConfig.active();
    uint32_t         now = millis();

    for (uint8_t i = 0; i < hw.ledCount && i < HW_MAX_LEDS; i++)
    {
        bool painted = false;

        for (uint8_t j = 0; j < hw.ledAssignCount && j < HW_MAX_LED_ASSIGN; j++)
        {
            const HwLedAssignment& a = hw.ledAssign[j];

            if (strncmp(a.target, hw.leds[i].name, HW_NAME_MAX + 1) != 0 ||
                !conditionHolds(a.condition))
            {
                continue;
            }

            if (patternOn(a.pattern, now) && a.colour < HW_COL_COUNT)
            {
                paint((int8_t)i, PALETTE[a.colour][0], PALETTE[a.colour][1],
                      PALETTE[a.colour][2]);
            }
            else
            {
                paint((int8_t)i, 0, 0, 0);
            }

            painted = true;
            break;
        }

        if (!painted)
        {
            paint((int8_t)i, 0, 0, 0);
        }
    }

    flush();
}

/*
 * Set one LED's colour.
 *
 * A plain LED has no colour, so anything non-black turns it on. That keeps
 * the callers above free of special cases: they describe what should be
 * visible, not how the hardware achieves it.
 */
void StatusLed::paint(int8_t led, uint8_t red, uint8_t green, uint8_t blue)
{
    const HwProfile& hw = hwConfig.active();

    if (led < 0 || (uint8_t)led >= hw.ledCount || (uint8_t)led >= HW_MAX_LEDS)
    {
        return;
    }

    const HwLed& l = hw.leds[led];

    if (l.kind == HW_LED_PLAIN)
    {
        bool on = (red | green | blue) != 0;
        digitalWrite(l.pin, (on != l.activeLow) ? HIGH : LOW);
        return;
    }

    int8_t at = chainFor(l.pin);
    if (at < 0 || _chains[at].colours == nullptr || l.rgbIndex >= _chains[at].length)
    {
        return;
    }

    uint8_t* px = &_chains[at].colours[(size_t)l.rgbIndex * 3];

    if (px[0] == green && px[1] == red && px[2] == blue)
    {
        return; // unchanged, no need to re-clock the chain
    }

    px[0] = green;
    px[1] = red;
    px[2] = blue;
    _chains[at].dirty = true;
}

void StatusLed::flush()
{
    for (uint8_t i = 0; i < _chainCount; i++)
    {
        if (_chains[i].dirty && _chains[i].colours != nullptr)
        {
            writeChain(_chains[i]);
            _chains[i].dirty = false;
        }
    }
}

/*
 * One RMT transaction for the whole chain.
 *
 * Sending 24 bits per LED in separate transactions would paint the first LED
 * over and over, because the gap between two transactions is longer than the
 * reset time that tells the chain a new frame has started.
 */
void StatusLed::writeChain(Chain& chain)
{
    const uint16_t symbols = (uint16_t)chain.length * 24;

    rmt_data_t* data = (rmt_data_t*)malloc((size_t)symbols * sizeof(rmt_data_t));

    if (data == nullptr)
    {
        return;
    }

    // Both chips read green first. The difference is how long a bit stays
    // high; each family is happier with its own timing even though the
    // tolerances overlap.
    const uint16_t zeroHigh = (chain.type == HW_RGB_SK6812) ? 3 : 4;
    const uint16_t oneHigh  = (chain.type == HW_RGB_SK6812) ? 6 : 8;

    uint16_t index = 0;

    for (uint16_t byte = 0; byte < (uint16_t)chain.length * 3; byte++)
    {
        for (int8_t bit = 7; bit >= 0; bit--)
        {
            bool     one  = (chain.colours[byte] >> bit) & 0x01;
            uint16_t high = one ? oneHigh : zeroHigh;

            data[index].level0    = 1;
            data[index].duration0 = high;
            data[index].level1    = 0;
            data[index].duration1 = BIT_PERIOD - high;
            index++;
        }
    }

    rmtWrite(chain.pin, data, symbols, RMT_WAIT_FOR_EVER);
    free(data);
}
