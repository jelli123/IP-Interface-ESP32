/*
 *  net_manager.cpp - WiFi lifecycle, provisioning access point and link watchdog.
 */

#include <DNSServer.h>
#include <WiFi.h>

#include "eth_interface.h"
#include "hw_config.h"
#include "improv_service.h"
#include "interface_config.h"
#include "knx_link.h"
#include "net_manager.h"

NetManager netManager;

static DNSServer   dnsServer;
static const byte  DNS_PORT = 53;

static void ledWrite(bool on)
{
    const HwProfile& hw = hwConfig.active();
    if (hw.ledPin < 0)
    {
        return;
    }
    bool level = hw.ledActiveLow ? !on : on;
    digitalWrite(hw.ledPin, level ? HIGH : LOW);
}

void NetManager::begin()
{
    _bootTime = millis();

    if (hwConfig.active().buttonPin >= 0)
    {
        pinMode(hwConfig.active().buttonPin, INPUT_PULLUP);
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
        Serial.printf("Network: Ethernet, IP %s\n", ethInterface.ipString().c_str());
        return;
    }

    WiFi.persistent(true);
    WiFi.mode(WIFI_STA);

    // Modem sleep must stay off. On the ESP32-C6 it breaks the WPA2 four way
    // handshake, and on every chip it drops multicast during the DTIM window,
    // which silently kills KNXnet/IP routing. Has to be set again after every
    // WiFi.begin().
    WiFi.setSleep(WIFI_PS_NONE);
    WiFi.setAutoReconnect(true);
    bool hasCredentials = WiFi.SSID().length() > 0;
    Serial.printf("Stored SSID: %s\n", hasCredentials ? WiFi.SSID().c_str() : "(none)");

    if (hasCredentials)
    {
        WiFi.begin();
    }
    else
    {
        Serial.println("No credentials stored - starting provisioning AP");
        startAccessPoint();
    }
}

String NetManager::apName() const
{
    String mac = WiFi.softAPmacAddress();
    mac.replace(":", "");
    return String(AP_NAME_PREFIX) + mac.substring(mac.length() - 4);
}

void NetManager::startAccessPoint()
{
    if (_apMode)
    {
        return;
    }

    // AP_STA rather than plain AP so the captive portal can still scan for
    // networks while the access point is up.
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(apName().c_str());
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    _apMode = true;

    Serial.printf("AP \"%s\" up at %s\n", apName().c_str(),
                  WiFi.softAPIP().toString().c_str());
}

void NetManager::waitForConnection(void (*keepAlive)())
{
    if (_ethMode)
    {
        return; // already up, nothing to wait for
    }

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

        handleButton();
        delay(10);
    }

    if (isOnline())
    {
        Serial.printf("WiFi up, IP %s\n", WiFi.localIP().toString().c_str());
        if (_apMode)
        {
            Serial.println("Station connected - shutting down the provisioning AP");
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_STA);
            _apMode = false;
        }
        _wasOnline = true;
    }
    else if (!_apMode)
    {
        Serial.println("WiFi did not come up - starting fallback AP");
        startAccessPoint();
    }
}

void NetManager::loop()
{
    if (_pendingReboot && (uint32_t)(millis() - _rebootAt) > 2000)
    {
        Serial.println("Rebooting");
        ESP.restart();
    }

    if (_ethMode)
    {
        ethInterface.loop();
        handleButton();
        handleLed();
        return; // no WiFi watchdog, no captive portal
    }

    improvService.loop();
    handleButton();

    if (_apMode)
    {
        dnsServer.processNextRequest();
        handleLed();
        return; // no station watchdog while providing the AP
    }

    handleWifiWatchdog();
    handleLed();
}

void NetManager::handleButton()
{
    int8_t pin = hwConfig.active().buttonPin;
    if (pin < 0)
    {
        return; // no button on this board
    }

    int state = digitalRead(pin);

    if (state == LOW && _buttonState == HIGH)
    {
        _buttonDownAt = millis();
    }
    else if (state == LOW && _buttonState == LOW)
    {
        // In Ethernet mode there is nothing to provision, so the button does
        // not open an access point.
        if (!_apMode && !_ethMode &&
            (uint32_t)(millis() - _buttonDownAt) > BUTTON_AP_HOLD_MS)
        {
            Serial.println("Button held - starting provisioning AP");
            WiFi.disconnect();
            startAccessPoint();
            if (knxLink.progMode())
            {
                knxLink.requestProgMode(false);
            }
        }
    }

    _buttonState = state;
}

/*
 * LED patterns:
 *   AP mode        double blink
 *   TP link down   fast blink
 *   online         steady on
 *   offline        off
 *
 * In Ethernet mode "online" means link plus address, so an unplugged cable
 * shows the same off state as a lost WiFi association.
 *
 * Programming mode overrides all of this - the KNX stack drives the same pin
 * directly while it is active, so we keep our hands off.
 */
void NetManager::handleLed()
{
    if (knxLink.progMode())
    {
        return;
    }

    uint32_t t = millis() % 1000;

    if (_apMode)
    {
        ledWrite(t < 100 || (t > 200 && t < 300));
    }
    else if (!knxLink.tpConnected())
    {
        ledWrite((millis() % 200) < 100);
    }
    else
    {
        ledWrite(isOnline());
    }
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
            Serial.printf("WiFi up, IP %s\n", WiFi.localIP().toString().c_str());
            _downSince = 0;
            _lastKick  = 0;
        }
        else
        {
            Serial.println("WiFi link down - watchdog armed");
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
        Serial.printf("WiFi down for %lus - forcing reconnect\n",
                      (unsigned long)(downFor / 1000));
        WiFi.setSleep(WIFI_PS_NONE);
        // reconnect() disconnects and connects without touching the stored
        // config, so it cannot collide with an in-flight core auto-reconnect.
        WiFi.reconnect();
    }
}

void NetManager::applyCredentials(const String& ssid, const String& password)
{
    Serial.printf("New WiFi credentials for SSID %s\n", ssid.c_str());
    WiFi.persistent(true);
    WiFi.setSleep(WIFI_PS_NONE);
    WiFi.begin(ssid.c_str(), password.c_str());
    scheduleReboot();
}

void NetManager::forgetCredentials()
{
    Serial.println("Erasing WiFi credentials");
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
