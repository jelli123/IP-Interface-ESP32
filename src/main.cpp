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
#include <Network.h>

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
     * Bring up esp_netif/lwIP before anything can open a socket.
     *
     * Arduino-ESP32 3.x initialises the TCP/IP stack lazily - whichever comes
     * first, WiFi.mode() or ETH.begin(), does it. On a board without a W5500
     * neither of them runs before knxLink.begin() below, because netManager
     * only starts afterwards. The KNXnet/IP multicast socket then reaches an
     * uninitialised lwIP, sys_mutex_lock() asserts on a null semaphore inside
     * tcpip_send_msg_wait_sem() and the device reboots in a loop.
     */
    Network.begin();

    /*
     * Ethernet before everything else.
     *
     * When a W5500 answers, this claims the default route, and NetManager
     * then leaves WiFi switched off entirely. No keep-alive callback here -
     * the KNX stack is not running yet, there is nothing to pump.
     *
     * Costs nothing when no W5500 is fitted: the probe is a single SPI
     * register read and bails out in microseconds.
     */
    ethInterface.begin(nullptr);

    /*
     * Network interface before the KNX stack.
     *
     * knx.start() creates the KNXnet/IP routing socket and joins its
     * multicast group right away. Started earlier, that join has no interface
     * to bind to and lwIP refuses it:
     *
     *   setup multicast addr: 224.0.23.12 port: 3671 ip: 0.0.0.0
     *   [E][NetworkUdp.cpp:133] beginMulticast(): could not join igmp: 125
     *
     * What follows is a socket that exists but cannot receive, so every
     * knx.loop() logs a parsePacket() error, and reconfiguring the netif
     * underneath it while bringing up the access point took the whole device
     * down with an InstructionFetchError.
     *
     * NetManager returns immediately in Ethernet mode, and in the AP case the
     * interface owns 192.168.4.1 by the time the join happens. Only a WiFi
     * station is still without an address here, which is what the
     * restartIpLayer() below is for.
     */
    netManager.begin();

    if (!knxLink.begin())
    {
        // Not fatal. The interface still serves the dashboard, which is where
        // the failure is visible, and the link supervision keeps retrying.
        Serial.println("WARNING: no answer from the SB-Interface on the KNX UART");
    }

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
