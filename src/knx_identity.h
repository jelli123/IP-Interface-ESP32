/*
 *  knx_identity.h - What this device claims to be towards ETS.
 *
 *  The values in include/interface_config.h are only the built-in defaults.
 *  What the firmware actually presents is stored in NVS and can be replaced
 *  through the dashboard, either field by field or by loading the knxprod
 *  that is meant to commission this device - the browser reads the file, the
 *  device only ever sees the handful of numbers it contains.
 *
 *  Read once during startup, before knx.start(): the values end up in the
 *  device object and in the application program object, both of which ETS
 *  reads during a download. Every change therefore needs a reboot.
 */
#pragma once

#include <Arduino.h>
#include <stdint.h>

/** Usable product name characters, excluding the terminator. */
static const uint8_t KNX_ID_NAME_MAX = 32;

/** PID_ORDER_INFO is ten octets - nothing longer fits, see applyIdentity(). */
static const uint8_t KNX_ID_ORDER_MAX = 10;

/** Octets of PID_HARDWARE_TYPE. Mirrors LEN_HARDWARE_TYPE of the stack. */
static const uint8_t KNX_ID_HW_TYPE_LEN = 6;

/**
 * The only mask version this image can honestly claim.
 *
 * Not a setting: the mask is a class in the stack, Bau091A, not a number in
 * a property. Writing a different value into PID_DEVICE_DESCRIPTOR would be
 * a lie ETS then acts on - it picks the management procedures for a download
 * from the mask. A knxprod for anything else is therefore refused rather
 * than adapted.
 */
static const uint16_t KNX_ID_MASK = 0x091A;

/** A complete device identity. */
struct KnxIdentity
{
    /** Shown in the dashboard and in the log. Not sent anywhere. */
    char     name[KNX_ID_NAME_MAX + 1] = {0};

    /** PID_SERIAL_NUMBER prefix and first half of PID_PROG_VERSION. */
    uint16_t manufacturer  = 0;

    /** ApplicationNumber and ApplicationVersion of the product data. */
    uint16_t appNumber     = 0;
    uint8_t  appVersion    = 0;

    /** PID_VERSION. Part of the flash image check, see invalidatesImage(). */
    uint16_t deviceVersion = 0;

    /** PID_DEVICE_DESCRIPTOR. Only KNX_ID_MASK is accepted. */
    uint16_t maskVersion   = KNX_ID_MASK;

    /**
     * AdditionalAddressesCount of the product data.
     *
     * The compiled KNX_TUNNELING is the number of tunnels the stack can
     * really serve; this is the number the product data manages. Fewer is
     * allowed and warned about, more is refused - see validate().
     */
    uint8_t  tunnels       = 1;

    /** PID_HARDWARE_TYPE. Part of the flash image check. */
    uint8_t  hardwareType[KNX_ID_HW_TYPE_LEN] = {0};

    /** PID_ORDER_INFO, empty leaves the property at zero. */
    char     orderInfo[KNX_ID_ORDER_MAX + 1] = {0};
};

class KnxIdentityStore
{
public:
    /** Why the built-in defaults are in use. */
    enum Fallback : uint8_t
    {
        FB_NONE = 0,     //!< a stored identity is active
        FB_UNCONFIGURED, //!< nothing stored yet
        FB_INVALID       //!< stored identity failed validation
    };

    /**
     * Load the stored identity and decide what to use.
     *
     * Must run before the KNX stack is started. No crash-loop guard as the
     * hardware profile has: an identity is a handful of bounded numbers that
     * no peripheral is driven from, so the worst a bad one can do is make
     * ETS refuse the download - which is visible and reversible from the
     * dashboard, unlike a GPIO that reboots the device.
     */
    void begin();

    /** The identity the firmware is running on. */
    const KnxIdentity& active() const { return _active; }

    /** The identity in NVS. Equals active() unless it was refused. */
    const KnxIdentity& stored() const { return _stored; }

    /** The identity compiled into this image. */
    static KnxIdentity defaults();

    bool        hasStored() const { return _hasStored; }
    bool        usingDefaults() const { return _fallback != FB_NONE; }
    Fallback    fallback() const { return _fallback; }
    const char* fallbackReason() const;

    /** True while a stored identity waits for the reboot that activates it. */
    bool rebootPending() const { return _rebootPending; }

    /**
     * Validate an identity.
     *
     * @param id    the identity to check
     * @param error receives a readable reason on failure
     * @return true if it can be presented as it stands
     */
    static bool validate(const KnxIdentity& id, String& error);

    /**
     * Replace the stored identity from a JSON document.
     *
     * Missing fields keep the value of the running identity, so a partial
     * document is a valid patch - the knxprod import sends only what it
     * could read out of the file.
     *
     * @param json  the document
     * @param error receives a readable reason on failure
     * @return true if the identity was accepted and stored
     */
    bool applyJson(const String& json, String& error);

    /**
     * Drop the stored identity and go back to the built-in defaults.
     *
     * @return false when the namespace could not be cleared
     */
    bool resetToDefaults();

    /**
     * True when swapping @p from for @p to makes the ETS download invalid.
     *
     * Memory::readMemory() compares manufacturer, hardware type and version
     * against the image in the knxcfg partition and discards everything when
     * they differ - filter table, group addresses and the individual address
     * included. The caller has to say so before storing, and saves the
     * address over the change.
     */
    static bool invalidatesImage(const KnxIdentity& from, const KnxIdentity& to);

    /** True when two identities describe the same device. */
    static bool same(const KnxIdentity& a, const KnxIdentity& b);

    /** Current state as a JSON object, for the dashboard. */
    String toJson() const;

    /** Serialise one identity as the document applyJson() accepts. */
    static String identityToJson(const KnxIdentity& id);

private:
    void load();
    bool store(const KnxIdentity& id);

    KnxIdentity _active;
    KnxIdentity _stored;

    Fallback _fallback      = FB_UNCONFIGURED;
    bool     _hasStored     = false;
    bool     _rebootPending = false;
};

extern KnxIdentityStore knxIdentity;
