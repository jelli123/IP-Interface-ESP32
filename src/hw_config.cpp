/*
 *  hw_config.cpp - Hardware profile, loaded at runtime.
 */

#include <Preferences.h>
#include <driver/gpio.h>
#include <nvs_flash.h>
#include <soc/soc_caps.h>

#include "hw_config.h"
#include "interface_config.h"
#include "json_util.h"

HwConfig hwConfig;

static Preferences prefs;

/** NVS namespace. Separate from the application settings on purpose. */
static const char* NS = "hwcfg";

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
 * Defaults
 * ------------------------------------------------------------------------- */

HwProfile HwConfig::defaults()
{
    HwProfile p;

    p.knxUartNum   = SBIP_KNX_UART_NUM;
    p.knxRxPin     = SBIP_KNX_RX_PIN;
    p.knxTxPin     = SBIP_KNX_TX_PIN;

    p.ledPin       = SBIP_LED_PIN;
    p.ledActiveLow = (SBIP_LED_ACTIVE_LOW != 0);
    p.buttonPin    = SBIP_BUTTON_PIN;

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
    // Quad flash/PSRAM. Octal variants additionally use 33..37, which cannot
    // be detected here - check the module datasheet before using them.
    if (pin >= 26 && pin <= 32) return true;
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

bool HwConfig::validate(const HwProfile& p, String& error)
{
    struct Entry
    {
        const char* name;
        int8_t      pin;
        bool        output;
        bool        required;
    };

    const Entry entries[] = {
        { "knx_rx",   p.knxRxPin,   false, true  },
        { "knx_tx",   p.knxTxPin,   true,  true  },
        { "led",      p.ledPin,     true,  false },
        { "button",   p.buttonPin,  false, false },
        { "i2c_sda",  p.i2cEnabled ? p.i2cSdaPin : (int8_t)-1, true, p.i2cEnabled },
        { "i2c_scl",  p.i2cEnabled ? p.i2cSclPin : (int8_t)-1, true, p.i2cEnabled },
        { "eth_sck",  p.ethEnabled ? p.ethSckPin  : (int8_t)-1, true, p.ethEnabled },
        { "eth_miso", p.ethEnabled ? p.ethMisoPin : (int8_t)-1, false, p.ethEnabled },
        { "eth_mosi", p.ethEnabled ? p.ethMosiPin : (int8_t)-1, true, p.ethEnabled },
        { "eth_cs",   p.ethEnabled ? p.ethCsPin   : (int8_t)-1, true, p.ethEnabled },
        { "eth_irq",  p.ethEnabled ? p.ethIrqPin  : (int8_t)-1, false, false },
        { "eth_rst",  p.ethEnabled ? p.ethRstPin  : (int8_t)-1, true,  false },
    };

    const size_t count = sizeof(entries) / sizeof(entries[0]);

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

    prefs.begin(NS, true);
    _hasStored = prefs.getBool("set", false);

    if (_hasStored)
    {
        _stored.knxUartNum   = prefs.getChar("uart", d.knxUartNum);
        _stored.knxRxPin     = prefs.getChar("krx",  d.knxRxPin);
        _stored.knxTxPin     = prefs.getChar("ktx",  d.knxTxPin);
        _stored.ledPin       = prefs.getChar("led",  d.ledPin);
        _stored.ledActiveLow = prefs.getBool("ledlo", d.ledActiveLow);
        _stored.buttonPin    = prefs.getChar("btn",  d.buttonPin);
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
    }
    else
    {
        _stored = d;
    }

    prefs.end();
}

void HwConfig::store(const HwProfile& p)
{
    prefs.begin(NS, false);
    prefs.putChar("uart", p.knxUartNum);
    prefs.putChar("krx",  p.knxRxPin);
    prefs.putChar("ktx",  p.knxTxPin);
    prefs.putChar("led",  p.ledPin);
    prefs.putBool("ledlo", p.ledActiveLow);
    prefs.putChar("btn",  p.buttonPin);
    prefs.putBool("i2cen", p.i2cEnabled);
    prefs.putChar("sda",  p.i2cSdaPin);
    prefs.putChar("scl",  p.i2cSclPin);
    prefs.putBool("ethen", p.ethEnabled);
    prefs.putChar("esck", p.ethSckPin);
    prefs.putChar("emiso", p.ethMisoPin);
    prefs.putChar("emosi", p.ethMosiPin);
    prefs.putChar("ecs",  p.ethCsPin);
    prefs.putChar("eirq", p.ethIrqPin);
    prefs.putChar("erst", p.ethRstPin);
    prefs.putUChar("emhz", p.ethSpiMhz);
    prefs.putBool("set", true);
    prefs.putUChar("boots", 0); // a fresh profile starts unproven
    prefs.end();

    _stored        = p;
    _hasStored     = true;
    _rebootPending = true;
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
        Serial.println("NVS: erasing and re-initialising");
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    Serial.printf("NVS init: %s\n", err == ESP_OK ? "OK" : "FAILED");

    HwProfile d = defaults();
    load();

    if (!_hasStored)
    {
        _active   = d;
        _fallback = FB_UNCONFIGURED;
        Serial.println("HW: using built-in defaults (nothing stored)");
        return;
    }

    /*
     * Escape hatch.
     *
     * Uses the COMPILE-TIME button pin, never the stored one: if the stored
     * profile is what locked the user out, its button pin cannot be trusted
     * either. Only meaningful when the image was built with a button.
     */
#if SBIP_BUTTON_PIN >= 0
    pinMode(SBIP_BUTTON_PIN, INPUT_PULLUP);
    delay(5); // let the pull-up settle before sampling
    if (digitalRead(SBIP_BUTTON_PIN) == LOW)
    {
        _active   = d;
        _fallback = FB_BUTTON;
        Serial.println("HW: button held at boot - using built-in defaults");
        return;
    }
#endif

    /*
     * Crash-loop guard.
     *
     * store() resets the counter, every boot increments it, and loop() clears
     * it once the firmware has run for PROVEN_AFTER_MS. A profile that keeps
     * crashing before that therefore counts up and is dropped - the same
     * pattern the OTA rollback uses, and the reason a bad pin assignment
     * cannot brick the device.
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
        Serial.printf("HW: stored profile failed %u boots - reverted to defaults\n",
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
        Serial.printf("HW: stored profile rejected (%s) - using defaults\n", error.c_str());
        return;
    }

    _active   = _stored;
    _fallback = FB_NONE;
    Serial.printf("HW: stored profile active (KNX UART%d rx=%d tx=%d)\n",
                  _active.knxUartNum, _active.knxRxPin, _active.knxTxPin);
}

void HwConfig::loop()
{
    if (_counterCleared || millis() < PROVEN_AFTER_MS)
    {
        return;
    }
    _counterCleared = true;

    if (!_hasStored)
    {
        return;
    }

    prefs.begin(NS, false);
    if (prefs.getUChar("boots", 0) != 0)
    {
        prefs.putUChar("boots", 0);
        Serial.println("HW: profile proven, crash-loop counter cleared");
    }
    prefs.end();
}

void HwConfig::resetToDefaults()
{
    prefs.begin(NS, false);
    prefs.clear();
    prefs.end();

    _hasStored     = false;
    _stored        = defaults();
    _rebootPending = true;
    Serial.println("HW: stored profile cleared, defaults active after reboot");
}

const char* HwConfig::fallbackReason() const
{
    switch (_fallback)
    {
    case FB_NONE:         return "";
    case FB_UNCONFIGURED: return "unconfigured";
    case FB_INVALID:      return "invalid";
    case FB_CRASHLOOP:    return "crashloop";
    case FB_BUTTON:       return "button";
    }
    return "";
}

/* ------------------------------------------------------------------------- *
 * JSON
 * ------------------------------------------------------------------------- */

bool HwConfig::applyJson(const String& json, String& error)
{
    // Start from what is running, so a partial document acts as a patch.
    HwProfile p = _active;

    p.knxUartNum   = (int8_t)jsonGetInt(json, "knx_uart", p.knxUartNum);
    p.knxRxPin     = (int8_t)jsonGetInt(json, "knx_rx",   p.knxRxPin);
    p.knxTxPin     = (int8_t)jsonGetInt(json, "knx_tx",   p.knxTxPin);

    p.ledPin       = (int8_t)jsonGetInt(json, "led",      p.ledPin);
    p.ledActiveLow = jsonGetBool(json, "led_active_low",  p.ledActiveLow);
    p.buttonPin    = (int8_t)jsonGetInt(json, "button",   p.buttonPin);

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

    if (!validate(p, error))
    {
        return false;
    }

    store(p);
    Serial.println("HW: new profile stored, reboot to activate");
    return true;
}

String HwConfig::profileToJson(const HwProfile& p)
{
    String j = "{";
    j += "\"knx_uart\":" + String(p.knxUartNum) + ",";
    j += "\"knx_rx\":" + String(p.knxRxPin) + ",";
    j += "\"knx_tx\":" + String(p.knxTxPin) + ",";
    j += "\"led\":" + String(p.ledPin) + ",";
    j += "\"led_active_low\":" + String(p.ledActiveLow ? "true" : "false") + ",";
    j += "\"button\":" + String(p.buttonPin) + ",";
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
    j += "\"eth_spi_mhz\":" + String(p.ethSpiMhz);
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
    j += "\"uart_count\":" + String(SOC_UART_NUM);
    j += "}";
    return j;
}
