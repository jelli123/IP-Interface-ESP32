/*
 *  net_manager.cpp - WiFi lifecycle, provisioning access point and link watchdog.
 */

#include <DNSServer.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_wifi.h>

#include "eth_interface.h"
#include "button_service.h"
#include "hw_config.h"
#include "improv_service.h"
#include "interface_config.h"
#include "knx_link.h"
#include "net_manager.h"

#include "log_buffer.h"
NetManager netManager;

static DNSServer   dnsServer;
static const byte  DNS_PORT = 53;

/*
 * Credentials are kept here rather than left to the WiFi driver.
 *
 * The driver's own NVS copy is written through esp_wifi_set_config(), which
 * only reaches flash while the storage mode happens to be WIFI_STORAGE_FLASH.
 * That mode is entangled with WiFi.persistent() - evaluated exactly once in
 * wifiLowLevelInit() - and every WiFi.mode() writes through it, so switching
 * the radio off on the way to the provisioning AP silently erased the
 * credentials that had just been entered.
 *
 * A namespace of our own has none of that coupling: it is written when we say
 * so and read back verbatim.
 */
static Preferences  netPrefs;
static const char*  NET_NS   = "sbip-net";
static const char*  KEY_SSID = "ssid";
static const char*  KEY_PASS = "pass";
static const char*  KEY_WIFI = "wifien";
static const char*  KEY_NAME = "devname";

/** Host names have no room for much else. NAME_MAX is taken by limits.h. */
static const size_t DEVICE_NAME_MAX = 31;

String NetManager::deviceName() const
{
    netPrefs.begin(NET_NS, false);
    String name = netPrefs.isKey(KEY_NAME) ? netPrefs.getString(KEY_NAME, "") : String();
    netPrefs.end();

    return name.length() ? name : String(MDNS_HOSTNAME);
}

bool NetManager::setDeviceName(const String& name)
{
    if (name.length() == 0 || name.length() > DEVICE_NAME_MAX)
    {
        return false;
    }

    // Letters, digits and hyphens only: the name becomes a host name, and a
    // space or a dot there produces something no resolver will find.
    for (size_t i = 0; i < name.length(); i++)
    {
        char c = name[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-';
        if (!ok) return false;
    }

    if (name[0] == '-' || name[name.length() - 1] == '-')
    {
        return false;
    }

    netPrefs.begin(NET_NS, false);
    netPrefs.putString(KEY_NAME, name);
    netPrefs.end();

    return true;
}

void NetManager::setWifiEnabled(bool enable)
{
    _wifiEnabled = enable;
    netPrefs.begin(NET_NS, false);
    netPrefs.putBool(KEY_WIFI, enable);
    netPrefs.end();
}

bool NetManager::wifiCanBeDisabled() const
{
    // Hardware, not connectivity: ethInterface.begin() has already run by the
    // time anyone asks, and a detected chip is what guarantees there is a
    // second way in at all.
    return ethInterface.chipPresent();
}

void NetManager::begin()
{
    _bootTime = millis();

    netPrefs.begin(NET_NS, false);
    _wifiEnabled = netPrefs.getBool(KEY_WIFI, true);
    netPrefs.end();

    /*
     * Self-healing.
     *
     * The radio may have been switched off while a W5500 was fitted. If that
     * board is gone, or its chip no longer answers, WiFi is the only way
     * left - and the device would come up with no interface at all. Turning
     * it back on and storing that keeps the state honest instead of leaving
     * a setting that silently does not apply.
     */
    if (!_wifiEnabled && !wifiCanBeDisabled())
    {
        sysLog.println("WiFi was switched off but no Ethernet chip answered - "
                       "re-enabling to keep the device reachable");
        setWifiEnabled(true);
    }

    // NVS was already initialised by HwConfig::begin(), which has to run
    // first anyway to know which pins to use.

    /*
     * Ethernet wins when it is there.
     *
     * WiFi stays completely off in that case, for two reasons beyond saving
     * power: the KNX routing socket joins its multicast group on the default
     * interface only (NetworkUDP uses INADDR_ANY), and a second interface
     * would make that choice depend on route priorities. And with a working
     * wired link there is nothing left to provision, so the open access point
     * would only be an unnecessary attack surface.
     */
    if (ethInterface.active())
    {
        _ethMode   = true;
        _wasOnline = true;
        WiFi.mode(WIFI_OFF);
        sysLog.printf("Network: Ethernet, IP %s\n", ethInterface.ipString().c_str());
        return;
    }

    if (!_wifiEnabled)
    {
        sysLog.println("WiFi is switched off in the settings");
        return;
    }

    if (!startStation())
    {
        sysLog.println("No credentials stored - starting provisioning AP");
        startAccessPoint();
    }
}

/*
 * Reads the credentials from our own namespace rather than asking the driver.
 * WiFi.SSID() would need the station up first, and bringing it up only to
 * find nothing stored is what forced the mode juggling that kept losing them.
 */
bool NetManager::startStation()
{
    netPrefs.begin(NET_NS, false);
    // isKey() first: getString() on a missing key logs an ESP-IDF error.
    String ssid = netPrefs.isKey(KEY_SSID) ? netPrefs.getString(KEY_SSID, "") : String();
    String pass = netPrefs.isKey(KEY_PASS) ? netPrefs.getString(KEY_PASS, "") : String();
    netPrefs.end();

    sysLog.printf("Stored SSID: %s\n", ssid.length() ? ssid.c_str() : "(none)");

    if (ssid.length() == 0)
    {
        return false;
    }

    WiFi.persistent(false); // our namespace is the source of truth
    WiFi.mode(WIFI_STA);

    // Modem sleep must stay off. On the ESP32-C6 it breaks the WPA2 four way
    // handshake, and on every chip it drops multicast during the DTIM window,
    // which silently kills KNXnet/IP routing. Has to be set again after every
    // WiFi.begin().
    WiFi.setSleep(WIFI_PS_NONE);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid.c_str(), pass.c_str());

    return true;
}

String NetManager::apName() const
{
    /*
     * Straight from the eFuse, not from the driver.
     *
     * WiFi.macAddress() only answers once the driver has been initialised,
     * and the access point is started before that whenever nothing is stored
     * - begin() returns early in that case and never reaches WiFi.mode().
     * The driver then reports 00:00:00:00:00:00 and every device in the room
     * calls itself "SB-IP AP 0000". esp_read_mac() works at any time.
     */
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    char suffix[5];
    snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);

    return String(AP_NAME_PREFIX) + suffix;
}

void NetManager::startAccessPoint()
{
    if (_apMode)
    {
        return;
    }

    /*
     * Plain AP, not AP_STA.
     *
     * With the station half enabled next to the access point, its power save
     * cycle starts on an interface that never connects to anything. The
     * wakeup collides with the access point coming up and takes the device
     * down before it ever reaches the loop:
     *
     *   Network Event: 110 - STA_START
     *   Guru Meditation Error: Cache disabled but cached memory region accessed
     *   Core 0: esp_phy_enable <- pm_wake_up <- pm_disconnected_wake
     *
     * Setting WIFI_PS_NONE afterwards does not help - the event is already on
     * its way by then. Nothing is lost either way: WiFiScanClass::scanNetworks()
     * calls WiFi.enableSTA(true) itself, so the captive portal can still scan
     * for networks.
     */
    if (!WiFi.mode(WIFI_AP))
    {
        sysLog.println("AP: switching to WIFI_AP failed");
        return;
    }
    WiFi.setSleep(WIFI_PS_NONE);

    // Once - apName() asks the driver for the MAC on every call.
    const String name = apName();

    if (!WiFi.softAP(name.c_str()))
    {
        sysLog.printf("AP: softAP(\"%s\") failed\n", name.c_str());
        return;
    }

    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    _apMode = true;

    sysLog.printf("AP \"%s\" up at %s\n", name.c_str(),
                  WiFi.softAPIP().toString().c_str());
}

void NetManager::waitForConnection(void (*keepAlive)())
{
    if (_ethMode)
    {
        return; // already up, nothing to wait for
    }

    /*
     * One retry before giving up to the access point.
     *
     * The first association after provisioning fails often enough to be
     * annoying - the radio has just been switched from access point to
     * station, and the driver occasionally never completes the handshake.
     * Auto-reconnect does not cover that case, because from its point of
     * view there was never a connection to lose.
     */
    static const uint32_t RETRY_AFTER_MS = 15000;

    bool     retried  = false;
    int      lastSeen = -1;
    uint32_t started  = millis();

    while (!isOnline() && (uint32_t)(millis() - _bootTime) < IMPROV_WINDOW_MS)
    {
        improvService.loop();

        // Keep pumping the KNX stack. The TP-UART emulator answers a
        // U_State.req every second; without knx.loop() the stack's 5 s
        // staleness check declares the link dead and drops every L_Data.req.
        if (keepAlive != nullptr)
        {
            keepAlive();
        }

        if (_apMode)
        {
            dnsServer.processNextRequest();
        }
        else
        {
            int status = WiFi.status();

            if (status != lastSeen)
            {
                lastSeen = status;
                sysLog.printf("WiFi status %d after %lu ms\n", status,
                              (unsigned long)(millis() - started));
            }

            if (!retried && status != WL_CONNECTED &&
                (uint32_t)(millis() - started) > RETRY_AFTER_MS)
            {
                retried = true;
                sysLog.println("WiFi did not associate - trying once more");
                WiFi.disconnect(false, true);
                delay(100);
                startStation();
            }
        }

        buttonService.loop();
        delay(10);
    }

    if (isOnline())
    {
        sysLog.printf("WiFi up, IP %s\n", WiFi.localIP().toString().c_str());
        if (_apMode)
        {
            sysLog.println("Station connected - shutting down the provisioning AP");
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_STA);
            WiFi.setSleep(WIFI_PS_NONE); // the mode switch turns it back on
            _apMode = false;
        }
        _wasOnline = true;
    }
    else if (!_apMode)
    {
        sysLog.println("WiFi did not come up - starting fallback AP");
        startAccessPoint();
    }
}

void NetManager::loop()
{
    if (_pendingReboot && (uint32_t)(millis() - _rebootAt) > 2000)
    {
        sysLog.println("Rebooting");
        ESP.restart();
    }

    if (_ethMode)
    {
        ethInterface.loop();
        return; // no WiFi watchdog, no captive portal
    }

    improvService.loop();

    if (_apMode)
    {
        dnsServer.processNextRequest();
        return; // no station watchdog while providing the AP
    }

    handleWifiWatchdog();
}

bool NetManager::isOnline() const
{
    if (_ethMode)
    {
        return ethInterface.active();
    }

    // WL_CONNECTED alone is not enough: a station can stay associated while
    // its DHCP lease is gone, reporting 0.0.0.0 and dropping every packet.
    return (WiFi.status() == WL_CONNECTED) && ((uint32_t)WiFi.localIP() != 0);
}

void NetManager::handleWifiWatchdog()
{
    if ((uint32_t)(millis() - _lastCheck) < WIFI_CHECK_INTERVAL_MS)
    {
        return;
    }
    _lastCheck = millis();

    bool online = isOnline();

    if (online != _wasOnline)
    {
        _wasOnline = online;
        if (online)
        {
            sysLog.printf("WiFi up, IP %s\n", WiFi.localIP().toString().c_str());
            _downSince = 0;
            _lastKick  = 0;
        }
        else
        {
            sysLog.println("WiFi link down - watchdog armed");
            _downSince = millis();
        }
    }

    if (online)
    {
        return;
    }

    if (_downSince == 0)
    {
        _downSince = millis();
    }

    uint32_t downFor = millis() - _downSince;
    if (downFor >= WIFI_WATCHDOG_GRACE_MS &&
        (_lastKick == 0 || (uint32_t)(millis() - _lastKick) >= WIFI_WATCHDOG_RETRY_MS))
    {
        _lastKick = millis();
        sysLog.printf("WiFi down for %lus - forcing reconnect\n",
                      (unsigned long)(downFor / 1000));
        WiFi.setSleep(WIFI_PS_NONE);
        // reconnect() disconnects and connects without touching the stored
        // config, so it cannot collide with an in-flight core auto-reconnect.
        WiFi.reconnect();
    }
}

void NetManager::applyCredentials(const String& ssid, const String& password)
{
    sysLog.printf("New WiFi credentials for SSID %s\n", ssid.c_str());

    if (!netPrefs.begin(NET_NS, false))
    {
        sysLog.println("WiFi: could not open the credential store");
        return; // no reboot - the AP stays up so the user can retry
    }

    netPrefs.putString(KEY_SSID, ssid);
    netPrefs.putString(KEY_PASS, password);

    // Read back before promising anything: a full NVS would fail silently.
    String check = netPrefs.getString(KEY_SSID, "");
    netPrefs.end();

    if (check != ssid)
    {
        sysLog.println("WiFi: credentials did not survive the write");
        return;
    }

    sysLog.println("WiFi: credentials stored, rebooting");
    scheduleReboot();
}

void NetManager::forgetCredentials()
{
    sysLog.println("Erasing WiFi credentials");

    netPrefs.begin(NET_NS, false);
    netPrefs.clear();
    netPrefs.end();

    // The driver keeps a copy of its own from earlier firmware versions.
    WiFi.persistent(true);
    WiFi.disconnect(false, true);

    scheduleReboot();
}

void NetManager::scheduleReboot()
{
    _pendingReboot = true;
    _rebootAt      = millis();
}

String NetManager::currentSsid() const
{
    if (_ethMode)
    {
        return String("Ethernet");
    }
    if (_apMode)
    {
        return apName();
    }
    return (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : String("N/A");
}

String NetManager::currentIp() const
{
    if (_ethMode)
    {
        return ethInterface.ipString();
    }
    return _apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
}

String NetManager::currentMac() const
{
    if (_ethMode)
    {
        return ethInterface.macString();
    }
    return _apMode ? WiFi.softAPmacAddress() : WiFi.macAddress();
}

bool NetManager::applyStaticIp(uint32_t ip, uint32_t mask, uint32_t gw)
{
    if (_apMode) return false;

    IPAddress address(ip);
    IPAddress netmask(mask);
    IPAddress gateway(gw);

    // No DNS from ETS - keep the gateway, which is right in most networks and
    // harmless where it is not: the device only resolves the update host.
    bool ok = _ethMode ? ethInterface.configure(ip, mask, gw)
                       : WiFi.config(address, gateway, netmask, gateway);

    if (!ok)
    {
        sysLog.printf("ETS address %s rejected by the interface\n",
                      address.toString().c_str());
        return false;
    }

    _etsAddress = true;
    sysLog.printf("ETS address applied: %s/%s via %s\n",
                  address.toString().c_str(), netmask.toString().c_str(),
                  gateway.toString().c_str());
    return true;
}

String NetManager::currentNetmask() const
{    if (_ethMode)
    {
        return IPAddress(ethInterface.subnetMask()).toString();
    }
    return _apMode ? WiFi.softAPSubnetMask().toString() : WiFi.subnetMask().toString();
}

String NetManager::currentGateway() const
{
    if (_ethMode)
    {
        return IPAddress(ethInterface.gateway()).toString();
    }
    // In AP mode we are the gateway ourselves.
    return _apMode ? WiFi.softAPIP().toString() : WiFi.gatewayIP().toString();
}

String NetManager::currentDns() const
{
    if (_ethMode)
    {
        return ethInterface.dnsString();
    }
    return _apMode ? String("0.0.0.0") : WiFi.dnsIP().toString();
}
