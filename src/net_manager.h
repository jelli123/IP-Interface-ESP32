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
     * Whether switching WiFi off is allowed at all.
     *
     * True once the W5500 has been found. A cable or an address is
     * deliberately not required - an unplugged but present chip still leaves
     * a way in, whereas no chip at all would make the device unreachable for
     * good. begin() re-enables the radio when this is false, so a profile
     * change or a removed board cannot lock anyone out either.
     */
    bool wifiCanBeDisabled() const;

private:
    void handleWifiWatchdog();
    String apName() const;

    bool     _apMode         = false;
    bool     _ethMode        = false;
    bool     _wifiEnabled    = true;
    bool     _pendingReboot  = false;
    uint32_t _rebootAt       = 0;
    uint32_t _bootTime       = 0;
    uint32_t _lastCheck      = 0;
    bool     _wasOnline      = false;
    bool     _etsAddress     = false;
    uint32_t _downSince      = 0;
    uint32_t _lastKick       = 0;
};

extern NetManager netManager;
