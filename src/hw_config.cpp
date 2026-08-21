/*
 *  hw_config.cpp - Hardware profile, loaded at runtime.
 */

#include <Preferences.h>
#include <driver/gpio.h>
#include <nvs_flash.h>
#include <soc/soc_caps.h>

#include "auth.h"
#include "hw_config.h"
#include "interface_config.h"
#include "json_util.h"
#include "status_led.h"

#include "log_buffer.h"
HwConfig hwConfig;

static Preferences prefs;

/** NVS namespace. Separate from the application settings on purpose. */
static const char* NS = "hwcfg";

/**
 * Layout marker for the button and LED lists.
 *
 * They are stored as raw structure arrays, so a change to any of those
 * structures makes the old bytes meaningless. Reading them anyway would hand
 * random GPIO numbers to pinMode(), which is a good deal worse than losing
 * the configuration: bump this and the lists fall back to the defaults.
 */
static const uint8_t IO_VERSION = 2;

/** Copy a name into a fixed field, always terminated. */
static void copyName(char* dst, const String& src)
{
    size_t n = src.length();
    if (n > HW_NAME_MAX) n = HW_NAME_MAX;
    memcpy(dst, src.c_str(), n);
    dst[n] = '\0';
}

/**
 * Uptime after which the profile counts as proven.
 *
 * Long enough to cover a crash during peripheral setup and the first KNX
 * traffic, short enough that a user power-cycling the device twice in a row
 * does not trip the crash-loop detection.
 */
static const uint32_t PROVEN_AFTER_MS = 20000UL;

/** Boot attempts with an unproven profile before falling back. */
static const uint8_t MAX_BOOT_ATTEMPTS = 2;

/* Fallbacks in case a future core drops the macros. */
#ifndef GPIO_IS_VALID_GPIO
#define GPIO_IS_VALID_GPIO(n) ((n) >= 0 && (n) < SOC_GPIO_PIN_COUNT)
#endif
#ifndef GPIO_IS_VALID_OUTPUT_GPIO
#define GPIO_IS_VALID_OUTPUT_GPIO(n) GPIO_IS_VALID_GPIO(n)
#endif

/* ------------------------------------------------------------------------- *
 * Lookups
 * ------------------------------------------------------------------------- */

int8_t HwProfile::findLed(const char* name) const
{
    for (uint8_t i = 0; i < ledCount && i < HW_MAX_LEDS; i++)
    {
        if (strncmp(leds[i].name, name, HW_NAME_MAX + 1) == 0) return (int8_t)i;
    }
    return -1;
}

int8_t HwProfile::findButton(const char* name) const
{
    for (uint8_t i = 0; i < buttonCount && i < HW_MAX_BUTTONS; i++)
    {
        if (strncmp(buttons[i].name, name, HW_NAME_MAX + 1) == 0) return (int8_t)i;
    }
    return -1;
}

bool HwProfile::usesCondition(uint8_t condition) const
{
    for (uint8_t i = 0; i < ledAssignCount && i < HW_MAX_LED_ASSIGN; i++)
    {
        if (ledAssign[i].condition == condition && findLed(ledAssign[i].target) >= 0)
        {
            return true;
        }
    }
    return false;
}

int8_t HwProfile::findButtonFor(uint8_t function) const
{
    for (uint8_t i = 0; i < btnAssignCount && i < HW_MAX_BTN_ASSIGN; i++)
    {
        if (btnAssign[i].function == function)
        {
            int8_t at = findButton(btnAssign[i].target);
            if (at >= 0) return at;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------------- *
 * Defaults
 * ------------------------------------------------------------------------- */

HwProfile HwConfig::defaults()
{
    HwProfile p;

    p.knxUartNum   = SBIP_KNX_UART_NUM;
    p.knxRxPin     = SBIP_KNX_RX_PIN;
    p.knxTxPin     = SBIP_KNX_TX_PIN;
    p.lpcResetPin  = SBIP_LPC_RESET_PIN;
    p.lpcIspPin    = SBIP_LPC_ISP_PIN;

    /*
     * The image ships with the layout the reference board has. Everything
     * here is a starting point the dashboard can replace.
     */
#if SBIP_LED_PIN >= 0
    {
        HwLed& led = p.leds[p.ledCount++];
        copyName(led.name, "prog");
        led.pin       = SBIP_LED_PIN;
        led.kind      = HW_LED_PLAIN;
        led.activeLow = (SBIP_LED_ACTIVE_LOW != 0);
    }
#endif

#if SBIP_RGB_PIN >= 0
    {
        HwLed& led = p.leds[p.ledCount++];
        copyName(led.name, "rgb");
        led.pin      = SBIP_RGB_PIN;
        led.kind     = HW_LED_RGB;
        led.rgbType  = SBIP_RGB_TYPE;
        led.rgbIndex = 0;
    }
#endif

    /*
     * One LED shows everything, in falling priority. On a board whose only
     * indicator is the RGB LED this is the whole user interface, so it has to
     * carry the programming mode as well - that is the one signal a KNX
     * installer actually needs while standing at the device.
     */
    if (p.ledCount > 0)
    {
        const char* first = p.leds[0].name;
        const char* last  = p.leds[p.ledCount - 1].name;

        struct Row
        {
            const char* target;
            uint8_t     condition;
            uint8_t     colour;
            uint8_t     pattern;
        };

        const Row rows[] = {
            { first, HW_COND_PROG_MODE, HW_COL_RED,    HW_PAT_BLINK_SLOW },
            { last,  HW_COND_AP_MODE,   HW_COL_BLUE,   HW_PAT_DOUBLE     },
            { last,  HW_COND_TP_DOWN,   HW_COL_YELLOW, HW_PAT_BLINK_FAST },
            { last,  HW_COND_ONLINE,    HW_COL_GREEN,  HW_PAT_STEADY     },
            { last,  HW_COND_HEARTBEAT, HW_COL_WHITE,  HW_PAT_FLASH      },
        };

        for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++)
        {
            HwLedAssignment& a = p.ledAssign[p.ledAssignCount++];
            copyName(a.target, rows[i].target);
            a.condition = rows[i].condition;
            a.colour    = rows[i].colour;
            a.pattern   = rows[i].pattern;
        }
    }

#if SBIP_BUTTON_PIN >= 0
    {
        // Two rows on one pin: a short press toggles the programming mode,
        // holding it opens the provisioning access point. That is exactly
        // what the firmware did before the lists existed.
        HwButton& shortPress = p.buttons[p.buttonCount++];
        copyName(shortPress.name, "prog");
        shortPress.pin     = SBIP_BUTTON_PIN;
        shortPress.trigger = HW_PRESS_SHORT;

        HwButton& longPress = p.buttons[p.buttonCount++];
        copyName(longPress.name, "setup");
        longPress.pin     = SBIP_BUTTON_PIN;
        longPress.trigger = HW_PRESS_LONG;

        HwAssignment& a1 = p.btnAssign[p.btnAssignCount++];
        copyName(a1.target, "prog");
        a1.function = HW_BTNF_PROG_MODE;

        HwAssignment& a2 = p.btnAssign[p.btnAssignCount++];
        copyName(a2.target, "setup");
        a2.function = HW_BTNF_WIFI_SETUP;
    }
#endif

    p.i2cEnabled   = (SBIP_I2C_ENABLED != 0) && (SBIP_I2C_SDA_PIN >= 0);
    p.i2cSdaPin    = SBIP_I2C_SDA_PIN;
    p.i2cSclPin    = SBIP_I2C_SCL_PIN;

    p.ethSckPin    = SBIP_ETH_SCK_PIN;
    p.ethMisoPin   = SBIP_ETH_MISO_PIN;
    p.ethMosiPin   = SBIP_ETH_MOSI_PIN;
    p.ethCsPin     = SBIP_ETH_CS_PIN;
    p.ethIrqPin    = SBIP_ETH_IRQ_PIN;
    p.ethRstPin    = SBIP_ETH_RST_PIN;
    p.ethSpiMhz    = SBIP_ETH_SPI_MHZ;
    p.ethEnabled   = (p.ethCsPin >= 0) && (p.ethSckPin >= 0) &&
                     (p.ethMisoPin >= 0) && (p.ethMosiPin >= 0);

    strlcpy(p.updateUrl, UPDATE_MANIFEST_URL, sizeof(p.updateUrl));
    strlcpy(p.lpcUrl, LPC_MANIFEST_URL, sizeof(p.lpcUrl));

    return p;
}

/* ------------------------------------------------------------------------- *
 * Validation
 * ------------------------------------------------------------------------- */

/*
 * Pins wired to the SPI flash (and, where applicable, PSRAM).
 *
 * These are inside the chip's valid GPIO mask, so GPIO_IS_VALID_GPIO() does
 * not catch them - but driving one kills the running firmware instantly.
 * That is exactly the mistake a hand-written profile can make, so it is
 * checked explicitly rather than left to the user.
 */
bool HwConfig::pinIsFlash(int8_t pin)
{
    if (pin < 0)
    {
        return false;
    }

#if CONFIG_IDF_TARGET_ESP32
    if (pin >= 6 && pin <= 11) return true;
#if CONFIG_SPIRAM
    // WROVER modules route PSRAM over GPIO 16/17; WROOM leaves them free.
    if (pin == 16 || pin == 17) return true;
#endif
#elif CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32S3
    if (pin >= 26 && pin <= 32) return true;
#if CONFIG_SPIRAM_MODE_OCT
    // Octal PSRAM reaches the module over these; an N8R8 loses them.
    if (pin >= 35 && pin <= 37) return true;
#endif
#elif CONFIG_IDF_TARGET_ESP32C3
    if (pin >= 11 && pin <= 17) return true;
#elif CONFIG_IDF_TARGET_ESP32C6
    if (pin >= 24 && pin <= 30) return true;
#endif

    return false;
}

bool HwConfig::pinUsable(int8_t pin, bool needsOutput)
{
    if (pin < 0)
    {
        return true; // not fitted
    }
    if (!GPIO_IS_VALID_GPIO(pin))
    {
        return false;
    }
    if (needsOutput && !GPIO_IS_VALID_OUTPUT_GPIO(pin))
    {
        return false;
    }
    return !pinIsFlash(pin);
}

bool HwConfig::nameValid(const char* name)
{
    if (name == nullptr) return false;

    size_t n = strnlen(name, HW_NAME_MAX + 1);
    if (n == 0 || n > HW_NAME_MAX) return false;

    for (size_t i = 0; i < n; i++)
    {
        char c = name[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

bool HwConfig::validate(const HwProfile& p, String& error)
{
    struct Entry
    {
        const char* name;
        int8_t      pin;
        bool        output;
        bool        required;
    };

    if (p.buttonCount > HW_MAX_BUTTONS || p.ledCount > HW_MAX_LEDS ||
        p.btnAssignCount > HW_MAX_BTN_ASSIGN || p.ledAssignCount > HW_MAX_LED_ASSIGN)
    {
        error = "too many rows";
        return false;
    }

    /* --- buttons ------------------------------------------------------- */

    for (uint8_t i = 0; i < p.buttonCount; i++)
    {
        const HwButton& b = p.buttons[i];

        if (!nameValid(b.name))
        {
            error = "button name must be 1.." + String(HW_NAME_MAX) +
                    " characters from A-Z a-z 0-9 _ -";
            return false;
        }
        if (b.trigger >= HW_PRESS_COUNT)
        {
            error = String("button ") + b.name + ": unknown trigger";
            return false;
        }
        if (b.pin < 0)
        {
            error = String("button ") + b.name + ": no GPIO selected";
            return false;
        }
        if (!GPIO_IS_VALID_GPIO(b.pin))
        {
            error = String("button ") + b.name + ": GPIO " + String(b.pin) +
                    " does not exist on this chip";
            return false;
        }
        if (pinIsFlash(b.pin))
        {
            error = String("button ") + b.name + ": GPIO " + String(b.pin) +
                    " is wired to the SPI flash";
            return false;
        }

        for (uint8_t j = i + 1; j < p.buttonCount; j++)
        {
            if (strncmp(b.name, p.buttons[j].name, HW_NAME_MAX + 1) == 0)
            {
                error = String("button name ") + b.name + " is used twice";
                return false;
            }
            // One pin may carry several rows, which is how a short press and
            // a long press on the same button end up doing different things.
            // The same trigger twice is always a mistake.
            if (b.pin == p.buttons[j].pin && b.trigger == p.buttons[j].trigger)
            {
                error = String("buttons ") + b.name + " and " + p.buttons[j].name +
                        " share GPIO " + String(b.pin) + " and the same trigger";
                return false;
            }
        }
    }

    /* --- LEDs ---------------------------------------------------------- */

    for (uint8_t i = 0; i < p.ledCount; i++)
    {
        const HwLed& l = p.leds[i];

        if (!nameValid(l.name))
        {
            error = "LED name must be 1.." + String(HW_NAME_MAX) +
                    " characters from A-Z a-z 0-9 _ -";
            return false;
        }
        if (l.kind >= HW_LED_KIND_COUNT)
        {
            error = String("LED ") + l.name + ": unknown type";
            return false;
        }
        if (l.pin < 0)
        {
            error = String("LED ") + l.name + ": no GPIO selected";
            return false;
        }
        if (!GPIO_IS_VALID_GPIO(l.pin))
        {
            error = String("LED ") + l.name + ": GPIO " + String(l.pin) +
                    " does not exist on this chip";
            return false;
        }
        if (pinIsFlash(l.pin))
        {
            error = String("LED ") + l.name + ": GPIO " + String(l.pin) +
                    " is wired to the SPI flash";
            return false;
        }
        if (!GPIO_IS_VALID_OUTPUT_GPIO(l.pin))
        {
            error = String("LED ") + l.name + ": GPIO " + String(l.pin) +
                    " is input only";
            return false;
        }
        if (l.kind == HW_LED_RGB)
        {
            if (l.rgbType >= HW_RGB_TYPE_COUNT)
            {
                error = String("LED ") + l.name + ": unknown chip type";
                return false;
            }
            if (l.rgbIndex >= 64)
            {
                error = String("LED ") + l.name + ": position must be 0..63";
                return false;
            }
        }

        for (uint8_t j = i + 1; j < p.ledCount; j++)
        {
            const HwLed& o = p.leds[j];

            if (strncmp(l.name, o.name, HW_NAME_MAX + 1) == 0)
            {
                error = String("LED name ") + l.name + " is used twice";
                return false;
            }
            if (l.pin != o.pin)
            {
                continue;
            }

            // Sharing a pin only makes sense for two positions on one
            // addressable chain - and then both rows describe the same
            // physical wire, so the chip type has to agree.
            if (l.kind != HW_LED_RGB || o.kind != HW_LED_RGB)
            {
                error = String("LEDs ") + l.name + " and " + o.name +
                        " share GPIO " + String(l.pin);
                return false;
            }
            if (l.rgbType != o.rgbType)
            {
                error = String("LEDs ") + l.name + " and " + o.name +
                        " are on one chain but have different chip types";
                return false;
            }
            if (l.rgbIndex == o.rgbIndex)
            {
                error = String("LEDs ") + l.name + " and " + o.name +
                        " use position " + String(l.rgbIndex) + " of the same chain";
                return false;
            }
        }
    }

    /* --- assignments --------------------------------------------------- */

    for (uint8_t i = 0; i < p.btnAssignCount; i++)
    {
        const HwAssignment& a = p.btnAssign[i];

        if (a.function >= HW_BTNF_COUNT)
        {
            error = "unknown button function";
            return false;
        }
        if (!nameValid(a.target) || p.findButton(a.target) < 0)
        {
            error = String("button assignment refers to unknown button");
            return false;
        }
        for (uint8_t j = i + 1; j < p.btnAssignCount; j++)
        {
            if (strncmp(a.target, p.btnAssign[j].target, HW_NAME_MAX + 1) == 0)
            {
                error = String("button ") + a.target + " has more than one function";
                return false;
            }
        }
    }

    for (uint8_t i = 0; i < p.ledAssignCount; i++)
    {
        const HwLedAssignment& a = p.ledAssign[i];

        if (a.condition >= HW_COND_COUNT)
        {
            error = "unknown LED state";
            return false;
        }
        if (a.colour >= HW_COL_COUNT)
        {
            error = "unknown LED colour";
            return false;
        }
        if (a.pattern >= HW_PAT_COUNT)
        {
            error = "unknown LED pattern";
            return false;
        }
        if (!nameValid(a.target) || p.findLed(a.target) < 0)
        {
            error = String("LED assignment refers to unknown LED");
            return false;
        }

        // Several states per LED are the point of the list, but the same
        // state twice on one LED means the lower row can never be reached.
        for (uint8_t j = i + 1; j < p.ledAssignCount; j++)
        {
            if (a.condition == p.ledAssign[j].condition &&
                strncmp(a.target, p.ledAssign[j].target, HW_NAME_MAX + 1) == 0)
            {
                error = String("LED ") + a.target + " has the same state twice";
                return false;
            }
        }
    }

    /* --- fixed peripherals, plus every pin claimed above ---------------- */

    Entry  entries[13 + HW_MAX_BUTTONS + HW_MAX_LEDS];
    size_t count = 0;

    entries[count++] = { "knx_rx",   p.knxRxPin,   false, true  };
    entries[count++] = { "knx_tx",   p.knxTxPin,   true,  true  };
    entries[count++] = { "lpc_reset", p.lpcResetPin, true, false };
    entries[count++] = { "lpc_isp",   p.lpcIspPin,   true, false };
    entries[count++] = { "i2c_sda",  p.i2cEnabled ? p.i2cSdaPin : (int8_t)-1, true, p.i2cEnabled };
    entries[count++] = { "i2c_scl",  p.i2cEnabled ? p.i2cSclPin : (int8_t)-1, true, p.i2cEnabled };
    entries[count++] = { "eth_sck",  p.ethEnabled ? p.ethSckPin  : (int8_t)-1, true, p.ethEnabled };
    entries[count++] = { "eth_miso", p.ethEnabled ? p.ethMisoPin : (int8_t)-1, false, p.ethEnabled };
    entries[count++] = { "eth_mosi", p.ethEnabled ? p.ethMosiPin : (int8_t)-1, true, p.ethEnabled };
    entries[count++] = { "eth_cs",   p.ethEnabled ? p.ethCsPin   : (int8_t)-1, true, p.ethEnabled };
    entries[count++] = { "eth_irq",  p.ethEnabled ? p.ethIrqPin  : (int8_t)-1, false, false };
    entries[count++] = { "eth_rst",  p.ethEnabled ? p.ethRstPin  : (int8_t)-1, true,  false };

    // One entry per distinct pin: the rows themselves were already checked
    // against each other, so only cross-group collisions are left to find.
    for (uint8_t i = 0; i < p.buttonCount; i++)
    {
        bool seen = false;
        for (uint8_t j = 0; j < i; j++)
        {
            if (p.buttons[j].pin == p.buttons[i].pin) seen = true;
        }
        if (!seen) entries[count++] = { p.buttons[i].name, p.buttons[i].pin, false, false };
    }
    for (uint8_t i = 0; i < p.ledCount; i++)
    {
        bool seen = false;
        for (uint8_t j = 0; j < i; j++)
        {
            if (p.leds[j].pin == p.leds[i].pin) seen = true;
        }
        if (!seen) entries[count++] = { p.leds[i].name, p.leds[i].pin, true, false };
    }

    if (p.knxUartNum < 0 || p.knxUartNum >= SOC_UART_NUM)
    {
        error = String("knx_uart out of range (0..") + String(SOC_UART_NUM - 1) + ")";
        return false;
    }

    for (size_t i = 0; i < count; i++)
    {
        const Entry& e = entries[i];

        if (e.required && e.pin < 0)
        {
            error = String(e.name) + " is required but not set";
            return false;
        }
        if (e.pin < 0)
        {
            continue;
        }
        if (!GPIO_IS_VALID_GPIO(e.pin))
        {
            error = String(e.name) + ": GPIO " + String(e.pin) + " does not exist on this chip";
            return false;
        }
        if (pinIsFlash(e.pin))
        {
            error = String(e.name) + ": GPIO " + String(e.pin) + " is wired to the SPI flash";
            return false;
        }
        if (e.output && !GPIO_IS_VALID_OUTPUT_GPIO(e.pin))
        {
            error = String(e.name) + ": GPIO " + String(e.pin) + " is input only";
            return false;
        }
    }

    // Every pin may be claimed once. A duplicate is always a mistake and
    // usually a silent one - two peripherals fighting over a pin produces
    // symptoms far away from the cause.
    for (size_t i = 0; i < count; i++)
    {
        if (entries[i].pin < 0) continue;
        for (size_t j = i + 1; j < count; j++)
        {
            if (entries[i].pin == entries[j].pin)
            {
                error = String("GPIO ") + String(entries[i].pin) + " used by both " +
                        entries[i].name + " and " + entries[j].name;
                return false;
            }
        }
    }

    if (p.ethEnabled && (p.ethSpiMhz < 1 || p.ethSpiMhz > 80))
    {
        error = "eth_spi_mhz out of range (1..80)";
        return false;
    }

    // The update client is HTTPS only, and the string also reaches the log.
    for (const char* url : {p.updateUrl, p.lpcUrl})
    {
        if (url[0] == '\0') continue;

        if (strncmp(url, "https://", 8) != 0)
        {
            error = "the update URLs must start with https://";
            return false;
        }
        for (const char* c = url; *c; c++)
        {
            if (*c < 0x21 || *c > 0x7E)
            {
                error = "the update URLs must be printable ASCII without spaces";
                return false;
            }
        }
    }

    error = "";
    return true;
}

/* ------------------------------------------------------------------------- *
 * Persistence
 *
 * The profile is stored as individual NVS keys, not as the JSON document.
 * JSON is only the transport format for the dashboard: parsing happens once,
 * while the value is being accepted, never on the boot path. A truncated or
 * corrupted blob can therefore not stop the device from starting.
 * ------------------------------------------------------------------------- */

void HwConfig::load()
{
    HwProfile d = defaults();

    prefs.begin(NS, false);
    _hasStored = prefs.getBool("set", false);

    if (_hasStored)
    {
        _stored.knxUartNum   = prefs.getChar("uart", d.knxUartNum);
        _stored.knxRxPin     = prefs.getChar("krx",  d.knxRxPin);
        _stored.knxTxPin     = prefs.getChar("ktx",  d.knxTxPin);
        _stored.lpcResetPin  = prefs.getChar("lrst", d.lpcResetPin);
        _stored.lpcIspPin    = prefs.getChar("lisp", d.lpcIspPin);
        _stored.lpcInvert    = prefs.getBool("linv", d.lpcInvert);
        _stored.i2cEnabled   = prefs.getBool("i2cen", d.i2cEnabled);
        _stored.i2cSdaPin    = prefs.getChar("sda",  d.i2cSdaPin);
        _stored.i2cSclPin    = prefs.getChar("scl",  d.i2cSclPin);
        _stored.ethEnabled   = prefs.getBool("ethen", d.ethEnabled);
        _stored.ethSckPin    = prefs.getChar("esck", d.ethSckPin);
        _stored.ethMisoPin   = prefs.getChar("emiso", d.ethMisoPin);
        _stored.ethMosiPin   = prefs.getChar("emosi", d.ethMosiPin);
        _stored.ethCsPin     = prefs.getChar("ecs",  d.ethCsPin);
        _stored.ethIrqPin    = prefs.getChar("eirq", d.ethIrqPin);
        _stored.ethRstPin    = prefs.getChar("erst", d.ethRstPin);
        _stored.ethSpiMhz    = (uint8_t)prefs.getUChar("emhz", d.ethSpiMhz);
        strlcpy(_stored.updateUrl,
                prefs.isKey("updurl") ? prefs.getString("updurl", d.updateUrl).c_str()
                                      : d.updateUrl,
                sizeof(_stored.updateUrl));
        strlcpy(_stored.lpcUrl,
                prefs.isKey("lpcurl") ? prefs.getString("lpcurl", d.lpcUrl).c_str()
                                      : d.lpcUrl,
                sizeof(_stored.lpcUrl));
        loadLists(d);
    }
    else
    {
        _stored = d;
    }

    prefs.end();
}

/*
 * The four lists are stored as raw structure arrays.
 *
 * Every blob has to be exactly the size this build expects and the layout
 * marker has to match, otherwise the whole set is discarded. Half a list is
 * worse than none: the counts and the arrays would disagree, and the result
 * would be a pin number taken from whatever happened to be in flash.
 *
 * Reads happen on the boot path, so this must not be able to fail loudly.
 * It falls back to @p d and says so.
 */
void HwConfig::loadLists(const HwProfile& d)
{
    struct Blob
    {
        const char* key;
        void*       dst;
        size_t      size;
        uint8_t*    count;
        const char* countKey;
        uint8_t     max;
    };

    const Blob blobs[] = {
        { "btns", _stored.buttons,   sizeof(_stored.buttons),   &_stored.buttonCount,    "btnc", HW_MAX_BUTTONS },
        { "leds", _stored.leds,      sizeof(_stored.leds),      &_stored.ledCount,       "ledc", HW_MAX_LEDS    },
        { "bact", _stored.btnAssign, sizeof(_stored.btnAssign), &_stored.btnAssignCount, "bacc", HW_MAX_BTN_ASSIGN },
        { "lact", _stored.ledAssign, sizeof(_stored.ledAssign), &_stored.ledAssignCount, "lacc", HW_MAX_LED_ASSIGN },
    };

    bool ok = (prefs.getUChar("iover", 0) == IO_VERSION);

    for (size_t i = 0; ok && i < sizeof(blobs) / sizeof(blobs[0]); i++)
    {
        ok = (prefs.getBytesLength(blobs[i].key) == blobs[i].size);
    }

    if (!ok)
    {
        sysLog.println("HW: button/LED lists have a foreign layout - using defaults");
        _stored.buttonCount    = d.buttonCount;
        _stored.ledCount       = d.ledCount;
        _stored.btnAssignCount = d.btnAssignCount;
        _stored.ledAssignCount = d.ledAssignCount;
        memcpy(_stored.buttons,   d.buttons,   sizeof(d.buttons));
        memcpy(_stored.leds,      d.leds,      sizeof(d.leds));
        memcpy(_stored.btnAssign, d.btnAssign, sizeof(d.btnAssign));
        memcpy(_stored.ledAssign, d.ledAssign, sizeof(d.ledAssign));
        return;
    }

    for (size_t i = 0; i < sizeof(blobs) / sizeof(blobs[0]); i++)
    {
        prefs.getBytes(blobs[i].key, blobs[i].dst, blobs[i].size);

        uint8_t n = prefs.getUChar(blobs[i].countKey, 0);
        *blobs[i].count = (n > blobs[i].max) ? blobs[i].max : n;
    }

    // Names come straight out of flash, so nothing guarantees they are
    // terminated. Everything downstream treats them as C strings.
    for (uint8_t i = 0; i < HW_MAX_BUTTONS; i++)
        _stored.buttons[i].name[HW_NAME_MAX] = '\0';
    for (uint8_t i = 0; i < HW_MAX_LEDS; i++)
        _stored.leds[i].name[HW_NAME_MAX] = '\0';
    for (uint8_t i = 0; i < HW_MAX_BTN_ASSIGN; i++)
        _stored.btnAssign[i].target[HW_NAME_MAX] = '\0';
    for (uint8_t i = 0; i < HW_MAX_LED_ASSIGN; i++)
        _stored.ledAssign[i].target[HW_NAME_MAX] = '\0';
}

void HwConfig::store(const HwProfile& p)
{
    uint32_t started = millis();

    /*
     * Own handle, not the shared one.
     *
     * This runs on the async_tcp task while HwConfig::loop() may be writing
     * the crash-loop counter from the main task. Two tasks sharing one
     * Preferences object means one can close the handle the other is using -
     * NVS itself copes with separate handles, a single object does not.
     */
    Preferences store;
    store.begin(NS, false);

    store.putChar("uart", p.knxUartNum);
    store.putChar("krx",  p.knxRxPin);
    store.putChar("ktx",  p.knxTxPin);
    store.putChar("lrst", p.lpcResetPin);
    store.putChar("lisp", p.lpcIspPin);
    store.putBool("linv", p.lpcInvert);
    store.putBool("i2cen", p.i2cEnabled);
    store.putChar("sda",  p.i2cSdaPin);
    store.putChar("scl",  p.i2cSclPin);
    store.putBool("ethen", p.ethEnabled);
    store.putChar("esck", p.ethSckPin);
    store.putChar("emiso", p.ethMisoPin);
    store.putChar("emosi", p.ethMosiPin);
    store.putChar("ecs",  p.ethCsPin);
    store.putChar("eirq", p.ethIrqPin);
    store.putChar("erst", p.ethRstPin);
    store.putUChar("emhz", p.ethSpiMhz);
    store.putString("updurl", p.updateUrl);
    store.putString("lpcurl", p.lpcUrl);

    /*
     * Blobs only when they differ.
     *
     * NVS has no way to update a blob in place: every write allocates a new
     * entry and marks the old one erased, and once a page is full that forces
     * a compaction with a flash erase. Saving an unchanged profile used to
     * churn about 750 bytes for nothing.
     */
    struct Blob
    {
        const char* key;
        const void* now;
        const void* was;
        size_t      size;
    };

    const Blob blobs[] = {
        { "btns", p.buttons,   _stored.buttons,   sizeof(p.buttons)   },
        { "leds", p.leds,      _stored.leds,      sizeof(p.leds)      },
        { "bact", p.btnAssign, _stored.btnAssign, sizeof(p.btnAssign) },
        { "lact", p.ledAssign, _stored.ledAssign, sizeof(p.ledAssign) },
    };

    bool fresh = (store.getUChar("iover", 0) != IO_VERSION);

    for (size_t i = 0; i < sizeof(blobs) / sizeof(blobs[0]); i++)
    {
        if (fresh || memcmp(blobs[i].now, blobs[i].was, blobs[i].size) != 0)
        {
            store.putBytes(blobs[i].key, blobs[i].now, blobs[i].size);
        }
    }

    store.putUChar("btnc", p.buttonCount);
    store.putUChar("ledc", p.ledCount);
    store.putUChar("bacc", p.btnAssignCount);
    store.putUChar("lacc", p.ledAssignCount);
    store.putUChar("iover", IO_VERSION);

    store.putBool("set", true);
    store.putUChar("boots", 0); // a fresh profile starts unproven
    store.end();

    _stored        = p;
    _hasStored     = true;
    _rebootPending = true;

    sysLog.printf("HW: profile written in %lu ms\n",
                  (unsigned long)(millis() - started));
}

/* ------------------------------------------------------------------------- *
 * Startup
 * ------------------------------------------------------------------------- */

void HwConfig::begin()
{
    // NVS carries the hardware profile and the WiFi credentials, so it has to
    // be up before anything else. A layout change leaves it unusable until
    // erased.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        sysLog.println("NVS: erasing and re-initialising");
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    sysLog.printf("NVS init: %s\n", err == ESP_OK ? "OK" : "FAILED");

    HwProfile d = defaults();
    load();

    if (!_hasStored)
    {
        _active   = d;
        _fallback = FB_UNCONFIGURED;
        sysLog.println("HW: using built-in defaults (nothing stored)");
        return;
    }

    /*
     * Crash-loop guard.
     *
     * store() resets the counter, every boot increments it, and loop() clears
     * it once the firmware has run for PROVEN_AFTER_MS. A profile that keeps
     * crashing before that therefore counts up and is dropped - the same
     * pattern the OTA rollback uses, and the reason a bad pin assignment
     * cannot brick the device.
     *
     * This is the only automatic way back. Holding a button during startup
     * cannot be one: the button every DevKit has is the boot strapping pin
     * (GPIO 0 on ESP32/S2/S3, GPIO 9 on C3/C6), and holding it across reset
     * puts the ROM into serial download mode instead of starting the
     * application. At runtime the factory reset function does the job.
     */
    prefs.begin(NS, false);
    uint8_t attempts = prefs.getUChar("boots", 0);
    if (attempts >= MAX_BOOT_ATTEMPTS)
    {
        prefs.putBool("set", false);
        prefs.putUChar("boots", 0);
        prefs.end();

        _active    = d;
        _hasStored = false;
        _fallback  = FB_CRASHLOOP;
        sysLog.printf("HW: stored profile failed %u boots - reverted to defaults\n",
                      attempts);
        return;
    }
    prefs.putUChar("boots", (uint8_t)(attempts + 1));
    prefs.end();

    String error;
    if (!validate(_stored, error))
    {
        _active   = d;
        _fallback = FB_INVALID;
        sysLog.printf("HW: stored profile rejected (%s) - using defaults\n", error.c_str());
        return;
    }

    _active   = _stored;
    _fallback = FB_NONE;
    sysLog.printf("HW: stored profile active (KNX UART%d rx=%d tx=%d)\n",
                  _active.knxUartNum, _active.knxRxPin, _active.knxTxPin);
}

void HwConfig::loop()
{
    /*
     * Take over a live change here, in the main task.
     *
     * StatusLed and ButtonService read the active profile from this very
     * task, so between the two statements below nothing can be halfway
     * through it.
     */
    if (_applyPending)
    {
        _applyPending = false;
        _active       = _pending;
        statusLed.reload();
        sysLog.println("HW: live profile change applied");
    }

    if (_counterCleared || millis() < PROVEN_AFTER_MS)
    {
        return;
    }
    _counterCleared = true;

    if (!_hasStored)
    {
        return;
    }

    // Own handle: store() may run on the async_tcp task at the same moment.
    Preferences counter;
    counter.begin(NS, false);
    if (counter.getUChar("boots", 0) != 0)
    {
        counter.putUChar("boots", 0);
        sysLog.println("HW: profile proven, crash-loop counter cleared");
    }
    counter.end();
}

bool HwConfig::sameWiring(const HwProfile& a, const HwProfile& b)
{
    if (a.knxUartNum != b.knxUartNum || a.knxRxPin != b.knxRxPin ||
        a.knxTxPin != b.knxTxPin ||
        a.lpcResetPin != b.lpcResetPin || a.lpcIspPin != b.lpcIspPin ||
        a.lpcInvert != b.lpcInvert ||
        a.i2cEnabled != b.i2cEnabled || a.i2cSdaPin != b.i2cSdaPin ||
        a.i2cSclPin != b.i2cSclPin ||
        a.ethEnabled != b.ethEnabled || a.ethSckPin != b.ethSckPin ||
        a.ethMisoPin != b.ethMisoPin || a.ethMosiPin != b.ethMosiPin ||
        a.ethCsPin != b.ethCsPin || a.ethIrqPin != b.ethIrqPin ||
        a.ethRstPin != b.ethRstPin || a.ethSpiMhz != b.ethSpiMhz)
    {
        return false;
    }

    if (a.buttonCount != b.buttonCount || a.ledCount != b.ledCount)
    {
        return false;
    }

    // Names take part: an assignment points at one, so a rename has to be
    // seen by whoever caches the pin list.
    for (uint8_t i = 0; i < a.buttonCount && i < HW_MAX_BUTTONS; i++)
    {
        if (a.buttons[i].pin != b.buttons[i].pin ||
            a.buttons[i].trigger != b.buttons[i].trigger ||
            strncmp(a.buttons[i].name, b.buttons[i].name, HW_NAME_MAX + 1) != 0)
        {
            return false;
        }
    }
    for (uint8_t i = 0; i < a.ledCount && i < HW_MAX_LEDS; i++)
    {
        if (a.leds[i].pin != b.leds[i].pin || a.leds[i].kind != b.leds[i].kind ||
            a.leds[i].activeLow != b.leds[i].activeLow ||
            a.leds[i].rgbType != b.leds[i].rgbType ||
            a.leds[i].rgbIndex != b.leds[i].rgbIndex ||
            strncmp(a.leds[i].name, b.leds[i].name, HW_NAME_MAX + 1) != 0)
        {
            return false;
        }
    }

    return true;
}

void HwConfig::resetToDefaults()
{
    prefs.begin(NS, false);
    prefs.clear();
    prefs.end();

    _hasStored     = false;
    _stored        = defaults();
    _rebootPending = true;
    sysLog.println("HW: stored profile cleared, defaults active after reboot");
}

const char* HwConfig::fallbackReason() const
{
    switch (_fallback)
    {
    case FB_NONE:         return "";
    case FB_UNCONFIGURED: return "unconfigured";
    case FB_INVALID:      return "invalid";
    case FB_CRASHLOOP:    return "crashloop";
    }
    return "";
}

/* ------------------------------------------------------------------------- *
 * JSON
 * ------------------------------------------------------------------------- */

/*
 * Read one list.
 *
 * A missing key keeps whatever the patch started from; a present key replaces
 * the list completely. Replacing rather than merging is what makes deleting
 * the last row possible at all.
 *
 * @return false when the array is malformed or longer than @p max
 */
template <typename T, typename Fill>
static bool readList(const String& json, const char* key, uint8_t max,
                     uint8_t& count, T* items, Fill fill, String& error)
{
    if (!jsonHasKey(json, key))
    {
        return true;
    }

    String body;
    if (!jsonGetArray(json, key, body))
    {
        error = String(key) + ": malformed array";
        return false;
    }

    String  elements[16];
    uint8_t found = 0;

    if (!jsonSplitObjects(body, elements, 16, found))
    {
        error = String(key) + ": malformed array";
        return false;
    }
    if (found > max)
    {
        error = String(key) + ": at most " + String(max) + " rows";
        return false;
    }

    for (uint8_t i = 0; i < found; i++)
    {
        items[i] = T();
        fill(items[i], elements[i]);
    }
    count = found;
    return true;
}

bool HwConfig::applyJson(const String& json, String& error)
{
    uint32_t started = millis();

    // Start from what is running, so a partial document acts as a patch.
    HwProfile p = _active;

    p.knxUartNum   = (int8_t)jsonGetInt(json, "knx_uart", p.knxUartNum);
    p.knxRxPin     = (int8_t)jsonGetInt(json, "knx_rx",   p.knxRxPin);
    p.knxTxPin     = (int8_t)jsonGetInt(json, "knx_tx",   p.knxTxPin);

    p.lpcResetPin  = (int8_t)jsonGetInt(json, "lpc_reset", p.lpcResetPin);
    p.lpcIspPin    = (int8_t)jsonGetInt(json, "lpc_isp",   p.lpcIspPin);
    p.lpcInvert    = jsonGetBool(json, "lpc_invert",       p.lpcInvert);

    p.i2cEnabled   = jsonGetBool(json, "i2c_enabled",     p.i2cEnabled);
    p.i2cSdaPin    = (int8_t)jsonGetInt(json, "i2c_sda",  p.i2cSdaPin);
    p.i2cSclPin    = (int8_t)jsonGetInt(json, "i2c_scl",  p.i2cSclPin);

    p.ethEnabled   = jsonGetBool(json, "eth_enabled",     p.ethEnabled);
    p.ethSckPin    = (int8_t)jsonGetInt(json, "eth_sck",  p.ethSckPin);
    p.ethMisoPin   = (int8_t)jsonGetInt(json, "eth_miso", p.ethMisoPin);
    p.ethMosiPin   = (int8_t)jsonGetInt(json, "eth_mosi", p.ethMosiPin);
    p.ethCsPin     = (int8_t)jsonGetInt(json, "eth_cs",   p.ethCsPin);
    p.ethIrqPin    = (int8_t)jsonGetInt(json, "eth_irq",  p.ethIrqPin);
    p.ethRstPin    = (int8_t)jsonGetInt(json, "eth_rst",  p.ethRstPin);
    p.ethSpiMhz    = (uint8_t)jsonGetInt(json, "eth_spi_mhz", p.ethSpiMhz);

    if (json.indexOf("\"update_url\"") >= 0)
    {
        strlcpy(p.updateUrl, jsonGetString(json, "update_url").c_str(),
                sizeof(p.updateUrl));
    }
    if (json.indexOf("\"lpc_url\"") >= 0)
    {
        strlcpy(p.lpcUrl, jsonGetString(json, "lpc_url").c_str(), sizeof(p.lpcUrl));
    }

    bool ok =
        readList(json, "buttons", HW_MAX_BUTTONS, p.buttonCount, p.buttons,
                 [](HwButton& b, const String& e) {
                     copyName(b.name, jsonGetString(e, "name"));
                     b.pin     = (int8_t)jsonGetInt(e, "pin", -1);
                     b.trigger = (uint8_t)jsonGetInt(e, "trigger", HW_PRESS_COUNT);
                 }, error) &&
        readList(json, "leds", HW_MAX_LEDS, p.ledCount, p.leds,
                 [](HwLed& l, const String& e) {
                     copyName(l.name, jsonGetString(e, "name"));
                     l.pin       = (int8_t)jsonGetInt(e, "pin", -1);
                     l.kind      = (uint8_t)jsonGetInt(e, "kind", HW_LED_KIND_COUNT);
                     l.activeLow = jsonGetBool(e, "active_low", false);
                     l.rgbType   = (uint8_t)jsonGetInt(e, "rgb_type", HW_RGB_WS2812);
                     l.rgbIndex  = (uint8_t)jsonGetInt(e, "rgb_index", 0);
                 }, error) &&
        readList(json, "button_assign", HW_MAX_BTN_ASSIGN, p.btnAssignCount, p.btnAssign,
                 [](HwAssignment& a, const String& e) {
                     copyName(a.target, jsonGetString(e, "target"));
                     a.function = (uint8_t)jsonGetInt(e, "function", HW_BTNF_COUNT);
                 }, error) &&
        readList(json, "led_assign", HW_MAX_LED_ASSIGN, p.ledAssignCount, p.ledAssign,
                 [](HwLedAssignment& a, const String& e) {
                     copyName(a.target, jsonGetString(e, "target"));
                     a.condition = (uint8_t)jsonGetInt(e, "condition", HW_COND_COUNT);
                     a.colour    = (uint8_t)jsonGetInt(e, "colour", HW_COL_COUNT);
                     a.pattern   = (uint8_t)jsonGetInt(e, "pattern", HW_PAT_COUNT);
                 }, error);

    if (!ok)
    {
        return false;
    }

    if (!validate(p, error))
    {
        return false;
    }

    // Checked here rather than in validate(): a stored profile that predates
    // the rule still has to boot.
    if (Auth::enabled() && p.findButtonFor(HW_BTNF_FACTORY) < 0)
    {
        error = "a password is set - keep a button assigned to the factory "
                "reset, it is the only way back in";
        return false;
    }

    /*
     * A change that leaves the wiring alone takes effect at once.
     *
     * Pins cannot be moved while the peripherals sit on them, but the state
     * an LED reacts to is looked up per iteration - so recolouring an
     * indicator should not cost a restart. The swap itself is left to
     * loop(); see the note on _pending.
     */
    bool live = sameWiring(_active, p);

    store(p);

    if (live)
    {
        _pending       = p;
        _applyPending  = true;
        _rebootPending = false;
        sysLog.println("HW: assignments updated, active without a restart");
    }
    else
    {
        sysLog.println("HW: new profile stored, reboot to activate");
    }

    sysLog.printf("HW: apply took %lu ms\n", (unsigned long)(millis() - started));

    return true;
}

String HwConfig::profileToJson(const HwProfile& p)
{
    String j = "{";
    j += "\"knx_uart\":" + String(p.knxUartNum) + ",";
    j += "\"knx_rx\":" + String(p.knxRxPin) + ",";
    j += "\"knx_tx\":" + String(p.knxTxPin) + ",";
    j += "\"lpc_reset\":" + String(p.lpcResetPin) + ",";
    j += "\"lpc_isp\":" + String(p.lpcIspPin) + ",";
    j += "\"lpc_invert\":" + String(p.lpcInvert ? "true" : "false") + ",";

    // Names passed validate(), so they hold nothing that needs escaping.
    // jsonEscape() runs anyway - the day someone adds a way in that skips
    // validation, the output should still be a document rather than a hole.
    j += "\"buttons\":[";
    for (uint8_t i = 0; i < p.buttonCount && i < HW_MAX_BUTTONS; i++)
    {
        if (i) j += ",";
        j += "{\"name\":\"" + jsonEscape(p.buttons[i].name) + "\"";
        j += ",\"pin\":" + String(p.buttons[i].pin);
        j += ",\"trigger\":" + String(p.buttons[i].trigger) + "}";
    }
    j += "],\"leds\":[";
    for (uint8_t i = 0; i < p.ledCount && i < HW_MAX_LEDS; i++)
    {
        if (i) j += ",";
        j += "{\"name\":\"" + jsonEscape(p.leds[i].name) + "\"";
        j += ",\"pin\":" + String(p.leds[i].pin);
        j += ",\"kind\":" + String(p.leds[i].kind);
        j += ",\"active_low\":" + String(p.leds[i].activeLow ? "true" : "false");
        j += ",\"rgb_type\":" + String(p.leds[i].rgbType);
        j += ",\"rgb_index\":" + String(p.leds[i].rgbIndex) + "}";
    }
    j += "],\"button_assign\":[";
    for (uint8_t i = 0; i < p.btnAssignCount && i < HW_MAX_BTN_ASSIGN; i++)
    {
        if (i) j += ",";
        j += "{\"target\":\"" + jsonEscape(p.btnAssign[i].target) + "\"";
        j += ",\"function\":" + String(p.btnAssign[i].function) + "}";
    }
    j += "],\"led_assign\":[";
    for (uint8_t i = 0; i < p.ledAssignCount && i < HW_MAX_LED_ASSIGN; i++)
    {
        if (i) j += ",";
        j += "{\"target\":\"" + jsonEscape(p.ledAssign[i].target) + "\"";
        j += ",\"condition\":" + String(p.ledAssign[i].condition);
        j += ",\"colour\":" + String(p.ledAssign[i].colour);
        j += ",\"pattern\":" + String(p.ledAssign[i].pattern) + "}";
    }
    j += "],";

    j += "\"i2c_enabled\":" + String(p.i2cEnabled ? "true" : "false") + ",";
    j += "\"i2c_sda\":" + String(p.i2cSdaPin) + ",";
    j += "\"i2c_scl\":" + String(p.i2cSclPin) + ",";
    j += "\"eth_enabled\":" + String(p.ethEnabled ? "true" : "false") + ",";
    j += "\"eth_sck\":" + String(p.ethSckPin) + ",";
    j += "\"eth_miso\":" + String(p.ethMisoPin) + ",";
    j += "\"eth_mosi\":" + String(p.ethMosiPin) + ",";
    j += "\"eth_cs\":" + String(p.ethCsPin) + ",";
    j += "\"eth_irq\":" + String(p.ethIrqPin) + ",";
    j += "\"eth_rst\":" + String(p.ethRstPin) + ",";
    j += "\"eth_spi_mhz\":" + String(p.ethSpiMhz) + ",";
    j += "\"update_url\":\"" + jsonEscape(String(p.updateUrl)) + "\",";
    j += "\"lpc_url\":\"" + jsonEscape(String(p.lpcUrl)) + "\"";
    j += "}";
    return j;
}

String HwConfig::toJson() const
{
    String j = "{";
    j += "\"active\":" + profileToJson(_active) + ",";
    j += "\"stored\":" + profileToJson(_stored) + ",";
    j += "\"defaults\":" + profileToJson(defaults()) + ",";
    j += "\"has_stored\":" + String(_hasStored ? "true" : "false") + ",";
    j += "\"using_defaults\":" + String(usingDefaults() ? "true" : "false") + ",";
    j += "\"fallback\":\"" + String(fallbackReason()) + "\",";
    j += "\"reboot_pending\":" + String(_rebootPending ? "true" : "false") + ",";
    j += "\"chip\":\"" + String(ESP.getChipModel()) + "\",";
    j += "\"gpio_count\":" + String(SOC_GPIO_PIN_COUNT) + ",";
    j += "\"uart_count\":" + String(SOC_UART_NUM) + ",";

    /*
     * The pins a button or an LED may actually use.
     *
     * Sent so the dashboard can offer a list instead of a free number field.
     * The firmware still checks everything it is given - this only keeps the
     * user from picking something that was never going to be accepted.
     */
    j += "\"gpio_in\":[";
    bool first = true;
    for (int pin = 0; pin < SOC_GPIO_PIN_COUNT; pin++)
    {
        if (!pinUsable((int8_t)pin, false)) continue;
        if (!first) j += ",";
        j += String(pin);
        first = false;
    }
    j += "],\"gpio_out\":[";
    first = true;
    for (int pin = 0; pin < SOC_GPIO_PIN_COUNT; pin++)
    {
        if (!pinUsable((int8_t)pin, true)) continue;
        if (!first) j += ",";
        j += String(pin);
        first = false;
    }
    j += "]";
    j += "}";
    return j;
}
