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

    /** Switch to the provisioning access point and stay there. */
    void startAccessPoint();

    /** Erase the stored credentials and reboot into the access point. */
    void forgetCredentials();

    /** Store new credentials and schedule a reboot. */
    void applyCredentials(const String& ssid, const String& password);

    /** Ask for a reboot once the pending HTTP response has been flushed. */
    void scheduleReboot();

private:
    void handleButton();
    void handleLed();
    void handleWifiWatchdog();
    String apName() const;

    bool     _apMode         = false;
    bool     _ethMode        = false;
    bool     _pendingReboot  = false;
    uint32_t _rebootAt       = 0;
    uint32_t _bootTime       = 0;
    uint32_t _buttonDownAt   = 0;
    int      _buttonState    = HIGH;
    uint32_t _lastCheck      = 0;
    bool     _wasOnline      = false;
    uint32_t _downSince      = 0;
    uint32_t _lastKick       = 0;
};

extern NetManager netManager;
