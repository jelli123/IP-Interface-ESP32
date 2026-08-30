/*
 *  net_manager.h - WiFi lifecycle, provisioning access point and link watchdog.
 */
#pragma once

#include <Arduino.h>
#include <stdint.h>

/**
 * Owns everything network related except the web server itself.
 *
 * Interface selection happens once, in begin(): if the optional W5500 came
 * up, WiFi is not started at all and none of the WiFi machinery below runs.
 * Otherwise the usual WiFi path applies, with three ways to get credentials
 * into the device:
 *   - Improv over the USB serial port, during the first IMPROV_WINDOW_MS
 *   - the captive portal of the provisioning access point
 *   - whatever is already stored in NVS from a previous run
 */
class NetManager
{
public:
    /** Initialise NVS and, unless Ethernet is up, the WiFi subsystem. */
    void begin();

    /**
     * Block until the station is connected or the provisioning window closes.
     *
     * @param keepAlive called repeatedly so the KNX stack stays responsive
     */
    void waitForConnection(void (*keepAlive)());

    /** Drive the captive portal, the watchdog, the button and the LED. */
    void loop();

    bool isApMode() const { return _apMode; }

    /** @return true if the KNX traffic runs over the W5500 rather than WiFi */
    bool isEthernetMode() const { return _ethMode; }

    /** "ethernet" or "wifi", for the dashboard. */
    const char* activeInterface() const { return _ethMode ? "ethernet" : "wifi"; }

    /** @return true if the active interface is associated and holds an IP */
    bool isOnline() const;

    /** SSID of the access point we provide, or of the network we joined. */
    String currentSsid() const;
    String currentIp() const;
    String currentMac() const;
    String currentNetmask() const;
    String currentGateway() const;
    String currentDns() const;

    /**
     * Switch the active interface to a fixed address.
     *
     * Used for a configuration programmed by ETS, which takes precedence over
     * DHCP. Ignored in access point mode, where the device owns its own
     * address anyway.
     *
     * @return true if the interface accepted the configuration
     */
    bool applyStaticIp(uint32_t ip, uint32_t mask, uint32_t gw);

    /** @return true if the address came from ETS rather than from DHCP. */
    bool addressFromEts() const { return _etsAddress; }

    /** Switch to the provisioning access point and stay there. */
    void startAccessPoint();

    /** Erase the stored credentials and reboot into the access point. */
    void forgetCredentials();

    /** Store new credentials and schedule a reboot. */
    void applyCredentials(const String& ssid, const String& password);

    /** Ask for a reboot once the pending HTTP response has been flushed. */
    void scheduleReboot();

    /**
     * Erase the whole NVS partition, then restart.
     *
     * Hardware profile, WiFi credentials, KNX configuration, operating hours
     * and passwords - everything. Runs on the main task for the same reason
     * the button-triggered reset does: afterwards no open Preferences handle
     * describes anything that still exists.
     */
    void scheduleFactoryReset();

    /**
     * Whether the WiFi radio may come up at all.
     *
     * Applied during begin() only. Turning the radio off while the access
     * point or the KNX multicast socket is running means a mode change at
     * the worst possible moment, which this firmware has already crashed on
     * once - so the switch takes effect on the next boot.
     */
    void setWifiEnabled(bool enable);
    bool wifiEnabled() const { return _wifiEnabled; }

    /**
     * Let WiFi take over when the Ethernet link goes away, and hand back when
     * it returns.
     *
     * The change of interface happens through a **restart**, not by switching
     * live. The decision in begin() is the one that has been proven on this
     * device, and moving the netif under a running KNX multicast socket is
     * what this firmware has already crashed on twice. A device whose network
     * just disappeared is serving nobody, so a few seconds of restart cost
     * nothing against the risk.
     *
     * Only switchable off where a W5500 was found - without one there is no
     * Ethernet to fall back from, and the setting would only be a way to lock
     * oneself out.
     */
    void setWifiFallback(bool enable);
    bool wifiFallback() const { return _wifiFallback; }

    /**
     * Whether switching WiFi off is allowed at all.
     *
     * True once the W5500 has been found. A cable or an address is
     * deliberately not required - an unplugged but present chip still leaves
     * a way in, whereas no chip at all would make the device unreachable for
     * good. begin() re-enables the radio when this is false, so a profile
     * change or a removed board cannot lock anyone out either.
     */
    bool wifiCanBeDisabled() const;

    /**
     * Name this device answers to, also its mDNS host name.
     *
     * Worth setting when an installation holds more than one router: it is
     * what tells them apart in the log and under .local. Falls back to the
     * compiled-in default when nothing is stored. Applied at startup, so a
     * change needs a restart.
     */
    String deviceName() const;

    /** @return false when the name is empty or holds anything but [A-Za-z0-9-] */
    bool setDeviceName(const String& name);

private:
    bool startStation();
    void handleWifiWatchdog();

    /** Restart into the other interface once reality and mode disagree. */
    void superviseFailover();

    /** @return true if an SSID is stored, without touching the radio */
    static bool hasCredentials();

    String apName() const;

    bool     _apMode         = false;
    bool     _ethMode        = false;
    bool     _wifiEnabled    = true;
    bool     _wifiFallback   = true;
    bool     _pendingReboot  = false;
    bool     _pendingErase   = false;
    uint32_t _rebootAt       = 0;
    uint32_t _bootTime       = 0;
    uint32_t _lastCheck      = 0;
    bool     _wasOnline      = false;
    bool     _etsAddress     = false;
    uint32_t _downSince      = 0;
    uint32_t _lastKick       = 0;
    uint32_t _switchSince    = 0; //!< mode and reality have disagreed since
};

extern NetManager netManager;
