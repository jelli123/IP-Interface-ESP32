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

/* ------------------------------------------------------------------------- *
 * Buttons, LEDs and what they are wired to
 *
 * Both lists are addressed by name rather than by index, so reordering a row
 * in the dashboard cannot silently re-point an assignment at a different
 * piece of hardware.
 * ------------------------------------------------------------------------- */

/** Usable name characters, excluding the terminator. */
static const uint8_t HW_NAME_MAX = 16;

enum : uint8_t
{
    HW_MAX_BUTTONS    = 8,
    HW_MAX_LEDS       = 8,
    HW_MAX_BTN_ASSIGN = 8,
    HW_MAX_LED_ASSIGN = 12 //!< several states may point at the same LED
};

/** When a button fires. Several rows may share a pin if the trigger differs. */
enum HwTrigger : uint8_t
{
    HW_PRESS_SHORT = 0,
    HW_PRESS_LONG,
    HW_PRESS_VERY_LONG,
    HW_PRESS_COUNT
};

/** How an LED is driven. */
enum HwLedKind : uint8_t
{
    HW_LED_PLAIN = 0, //!< one GPIO, on or off
    HW_LED_RGB,       //!< one position in an addressable chain
    HW_LED_KIND_COUNT
};

/** Bit timing of an addressable LED. */
enum HwRgbType : uint8_t
{
    HW_RGB_WS2812 = 0,
    HW_RGB_SK6812,
    HW_RGB_TYPE_COUNT
};

/**
 * A device state an LED can react to.
 *
 * Deliberately flat rather than "function with sub-states": an LED that shows
 * the programming mode AND the network AND a heartbeat is the normal case on
 * a board that only has one, and a nested model would have to special-case
 * which function may be combined with which.
 */
enum HwLedCondition : uint8_t
{
    HW_COND_PROG_MODE = 0, //!< KNX programming mode is active
    HW_COND_AP_MODE,       //!< the provisioning access point is open
    HW_COND_TP_DOWN,       //!< no TP1 connection to the SB-Interface
    HW_COND_ONLINE,        //!< network and bus are up
    HW_COND_OFFLINE,       //!< no address, or the cable is unplugged
    HW_COND_HEARTBEAT,     //!< always, while the heartbeat switch is on
    HW_COND_COUNT
};

/** How the LED is modulated while its condition holds. */
enum HwLedPattern : uint8_t
{
    HW_PAT_STEADY = 0,
    HW_PAT_BLINK_SLOW, //!< 1 Hz
    HW_PAT_BLINK_FAST, //!< 5 Hz
    HW_PAT_DOUBLE,     //!< two short pulses per second
    HW_PAT_FLASH,      //!< one short flash every two seconds
    HW_PAT_COUNT
};

/**
 * Colour of an addressable LED. Ignored by a plain one.
 *
 * A fixed palette rather than a free colour value: these chips are painfully
 * bright, so the brightness has to be capped somewhere, and a handful of
 * clearly distinguishable colours is all a 5 mm indicator can convey anyway.
 */
enum HwLedColour : uint8_t
{
    HW_COL_RED = 0,
    HW_COL_GREEN,
    HW_COL_BLUE,
    HW_COL_YELLOW,
    HW_COL_CYAN,
    HW_COL_MAGENTA,
    HW_COL_WHITE,
    HW_COL_COUNT
};

/** What a button does. */
enum HwButtonFunction : uint8_t
{
    HW_BTNF_PROG_MODE = 0, //!< toggle KNX programming mode
    HW_BTNF_FACTORY,       //!< erase every stored setting and restart
    HW_BTNF_WIFI_SETUP,    //!< open the provisioning access point
    HW_BTNF_REBOOT,        //!< restart the device
    HW_BTNF_WIFI_TOGGLE,   //!< enable or disable WiFi, then restart
    HW_BTNF_COUNT
};

struct HwButton
{
    char    name[HW_NAME_MAX + 1] = {0};
    int8_t  pin                   = -1;
    uint8_t trigger               = HW_PRESS_SHORT;
};

struct HwLed
{
    char    name[HW_NAME_MAX + 1] = {0};
    int8_t  pin                   = -1;
    uint8_t kind                  = HW_LED_PLAIN;
    bool    activeLow             = false; //!< HW_LED_PLAIN only
    uint8_t rgbType               = HW_RGB_WS2812; //!< HW_LED_RGB only
    uint8_t rgbIndex              = 0;     //!< HW_LED_RGB: position in the chain
};

/** Ties one named button to one function. */
struct HwAssignment
{
    char    target[HW_NAME_MAX + 1] = {0};
    uint8_t function                = 0;
};

/**
 * Ties one named LED to one device state.
 *
 * Several rows may name the same LED. The first one whose condition holds
 * wins, so the list order is the priority - which is the only part of this
 * the user has to keep in mind.
 */
struct HwLedAssignment
{
    char    target[HW_NAME_MAX + 1] = {0};
    uint8_t condition               = HW_COND_ONLINE;
    uint8_t colour                  = HW_COL_GREEN;
    uint8_t pattern                 = HW_PAT_STEADY;
};

/** A complete hardware description. Every pin may be -1 for "not fitted". */
struct HwProfile
{
    // KNX TP1 link to the Selfbus SB-Interface
    int8_t  knxUartNum   = 2;
    int8_t  knxRxPin     = -1;
    int8_t  knxTxPin     = -1;

    // Buttons and LEDs, plus what each one is used for
    uint8_t      buttonCount = 0;
    HwButton     buttons[HW_MAX_BUTTONS];
    uint8_t      ledCount    = 0;
    HwLed        leds[HW_MAX_LEDS];
    uint8_t         btnAssignCount = 0;
    HwAssignment    btnAssign[HW_MAX_BTN_ASSIGN];
    uint8_t         ledAssignCount = 0;
    HwLedAssignment ledAssign[HW_MAX_LED_ASSIGN];

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

    /** @return true if any LED reacts to @p condition */
    bool usesCondition(uint8_t condition) const;

    /** @return index into buttons[], or -1 when nothing is assigned */
    int8_t findButtonFor(uint8_t function) const;

    /** @return index into leds[], or -1 when @p name is unknown */
    int8_t findLed(const char* name) const;

    /** @return index into buttons[], or -1 when @p name is unknown */
    int8_t findButton(const char* name) const;
};

class HwConfig
{
public:
    /** Why the built-in defaults are in use. */
    enum Fallback : uint8_t
    {
        FB_NONE = 0,     //!< a stored profile is active
        FB_UNCONFIGURED, //!< nothing stored yet
        FB_INVALID,      //!< stored profile failed validation
        FB_CRASHLOOP     //!< stored profile did not survive two boots
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

    /**
     * Check a button or LED name.
     *
     * Accepts 1..HW_NAME_MAX characters from [A-Za-z0-9_-]. The whitelist is
     * not cosmetic: names are echoed into JSON and into the dashboard, and
     * refusing quotes, backslashes and control characters at the boundary is
     * what makes both safe without relying on every later escape being right.
     */
    static bool nameValid(const char* name);

private:
    void load();
    void loadLists(const HwProfile& fallbackProfile);
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
