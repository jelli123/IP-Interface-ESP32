/*
 *  main.cpp - Selfbus KNXnet/IP interface.
 *
 *  ESP32 + Selfbus SB-Interface (LPC1115) running the TP-UART 2 emulator
 *  from ../TPUART2-Emu. Provides KNXnet/IP routing and tunnelling, a status
 *  dashboard, WiFi provisioning and over-the-air updates.
 */

#include <Arduino.h>
#include <ESPmDNS.h>
#include <knx.h>

#include "eth_interface.h"
#include "hw_config.h"
#include "interface_config.h"
#include "improv_service.h"
#include "knx_link.h"
#include "net_manager.h"
#include "ota_service.h"
#include "time_service.h"
#include "web_server.h"

/** Passed to NetManager so the KNX stack keeps running while WiFi comes up. */
static void knxKeepAlive()
{
    knxLink.loop();
}

void setup()
{
    Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
    // Bound the write stall when the USB host disappears mid-print.
    Serial.setTxTimeoutMs(10);
#endif

    /*
     * Hardware profile before anything else.
     *
     * It brings up NVS and decides which pins the rest of the firmware uses,
     * so nothing may touch a peripheral before this returns. Also samples the
     * recovery button, which is why it runs ahead of Improv rather than
     * after: a device locked out by a bad profile has to be rescued before
     * any of the code that could crash on it starts.
     *
     * Costs about 20 ms, so the Improv window below is still comfortably
     * within the two seconds ESP Web Tools waits for.
     */
    hwConfig.begin();

    // Improv early: ESP Web Tools resets the board by opening the port and
    // then probes for roughly two seconds. Anything slower here and the
    // browser gives up before we answer.
    improvService.begin();
    improvService.serviceFor(2000);

    Serial.println();
    Serial.println("Selfbus KNX/IP Interface " FIRMWARE_VERSION);

    if (hwConfig.usingDefaults())
    {
        Serial.printf("Hardware: built-in defaults (%s)\n", hwConfig.fallbackReason());
    }

    ArduinoPlatform::SerialDebug = &Serial;

    if (hwConfig.active().ledPin >= 0)
    {
        pinMode(hwConfig.active().ledPin, OUTPUT);
    }

    /*
     * Ethernet before KNX.
     *
     * knxLink.begin() enables the stack, which creates the KNXnet/IP routing
     * socket and joins its multicast group. That join lands on whatever is
     * the default network interface at that moment, so the wired interface
     * has to exist first. No keep-alive callback here - the KNX stack is not
     * running yet, there is nothing to pump.
     *
     * Costs nothing when no W5500 is fitted: the probe is a single SPI
     * register read and bails out in microseconds.
     */
    ethInterface.begin(nullptr);

    if (!knxLink.begin())
    {
        // Not fatal. The interface still serves the dashboard, which is where
        // the failure is visible, and the link supervision keeps retrying.
        Serial.println("WARNING: no answer from the SB-Interface on the KNX UART");
    }

    netManager.begin();
    timeService.begin();
    webServerBegin();

    if (MDNS.begin(MDNS_HOSTNAME))
    {
        MDNS.addService("http", "tcp", 80);
        Serial.println("mDNS: http://" MDNS_HOSTNAME ".local");
    }

    // Blocks until the interface is up or the provisioning window closes.
    // Returns immediately in Ethernet mode.
    netManager.waitForConnection(knxKeepAlive);

    // The multicast membership was taken out before the interface had an
    // address; renew it now that it does.
    if (netManager.isOnline())
    {
        knxLink.restartIpLayer();
    }
}

void loop()
{
    knxLink.loop();
    netManager.loop();
    timeService.loop();
    otaService.loop();
    hwConfig.loop();
}
