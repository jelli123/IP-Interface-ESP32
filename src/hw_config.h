/*
 *  hw_config.h - Hardware profile, loaded at runtime.
 *
 *  The values from platformio.ini are only the built-in defaults. What the
 *  firmware actually uses is stored in NVS and can be replaced through the
 *  dashboard, so one image supports several boards and assembly variants.
 *
 *  Pin assignments are read exactly once, during startup - peripherals cannot
 *  be moved while they are running. Every change therefore needs a reboot.
 */
#pragma once

#include <Arduino.h>
#include <stdint.h>

/** A complete hardware description. Every pin may be -1 for "not fitted". */
struct HwProfile
{
    // KNX TP1 link to the Selfbus SB-Interface
    int8_t  knxUartNum   = 2;
    int8_t  knxRxPin     = -1;
    int8_t  knxTxPin     = -1;

    // Programming LED and button
    int8_t  ledPin       = -1;
    bool    ledActiveLow = false;
    int8_t  buttonPin    = -1;

    // RV-3028-C7 real time clock
    bool    i2cEnabled   = false;
    int8_t  i2cSdaPin    = -1;
    int8_t  i2cSclPin    = -1;

    // W5500 Ethernet
    bool    ethEnabled   = false;
    int8_t  ethSckPin    = -1;
    int8_t  ethMisoPin   = -1;
    int8_t  ethMosiPin   = -1;
    int8_t  ethCsPin     = -1;
    int8_t  ethIrqPin    = -1;
    int8_t  ethRstPin    = -1;
    uint8_t ethSpiMhz    = 20;
};

class HwConfig
{
public:
    /** Why the built-in defaults are in use. */
    enum Fallback : uint8_t
    {
        FB_NONE = 0,   //!< a stored profile is active
        FB_UNCONFIGURED, //!< nothing stored yet
        FB_INVALID,    //!< stored profile failed validation
        FB_CRASHLOOP,  //!< stored profile did not survive two boots
        FB_BUTTON      //!< button held during startup
    };

    /**
     * Initialise NVS, load the stored profile and decide what to use.
     *
     * Must run before anything touches a pin. Also arms the crash-loop
     * counter, which loop() clears once the firmware has proven itself.
     */
    void begin();

    /** Clears the crash-loop counter after a successful run. */
    void loop();

    /** The profile the firmware is actually running on. */
    const HwProfile& active() const { return _active; }

    /** The profile compiled into this image. */
    static HwProfile defaults();

    bool     usingDefaults() const { return _fallback != FB_NONE; }
    Fallback fallback() const { return _fallback; }
    const char* fallbackReason() const;

    /** True while a stored profile waits for the reboot that activates it. */
    bool rebootPending() const { return _rebootPending; }

    /**
     * Validate a profile.
     *
     * Checks GPIO numbers against the chip, rejects pins reserved for the
     * SPI flash, requires output capability where needed and refuses
     * duplicates.
     *
     * @param profile the profile to check
     * @param error   receives a readable reason on failure
     * @return true if the profile is safe to use
     */
    static bool validate(const HwProfile& profile, String& error);

    /**
     * Replace the stored profile from a JSON document.
     *
     * Missing fields keep the value of the currently active profile, so a
     * partial document is a valid patch.
     *
     * @param json  the document
     * @param error receives a readable reason on failure
     * @return true if the profile was accepted and stored
     */
    bool applyJson(const String& json, String& error);

    /** Drop the stored profile and go back to the built-in defaults. */
    void resetToDefaults();

    /** Current state as a JSON object, for the dashboard. */
    String toJson() const;

    /** Serialise a profile as the document that applyJson() accepts. */
    static String profileToJson(const HwProfile& profile);

private:
    void load();
    void store(const HwProfile& profile);

    static bool pinIsFlash(int8_t pin);
    static bool pinUsable(int8_t pin, bool needsOutput);

    HwProfile _active;
    HwProfile _stored;
    Fallback  _fallback       = FB_UNCONFIGURED;
    bool      _hasStored      = false;
    bool      _rebootPending  = false;
    bool      _counterCleared = false;
};

extern HwConfig hwConfig;
