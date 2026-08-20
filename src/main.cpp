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
#include "button_service.h"
#include "cpu_load.h"
#include "hw_config.h"
#include "interface_config.h"
#include "improv_service.h"
#include "knx_link.h"
#include "lpc_isp.h"
#include "net_manager.h"
#include "ota_service.h"
#include "status_led.h"
#include "time_service.h"
#include "web_server.h"

#include "log_buffer.h"
/*
 * Passed to NetManager so the device stays alive during the provisioning
 * window. That window can last two minutes, and loop() has not started yet -
 * without this the status LED would sit dark exactly while someone is
 * standing in front of the device trying to set it up.
 */
static void provisioningKeepAlive()
{
    knxLink.loop();
    statusLed.loop();
}

void setup()
{
    Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
    // Bound the write stall when the USB host disappears mid-print.
    Serial.setTxTimeoutMs(10);
#endif

    // Before anything reports anything, so the dashboard can show the whole
    // startup afterwards.
    sysLog.begin();

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

    // Straight after the profile and before anything else touches a pin: the
    // two ISP lines have to reach their idle state early, or a board with
    // inverters holds the LPC in reset for the whole of our own startup.
    lpcIsp.begin();

    // Both need the profile for their pins, and come before the noisy parts
    // so the indicators work while the rest boots.
    statusLed.begin();
    buttonService.begin();

    // Improv early: ESP Web Tools resets the board by opening the port and
    // then probes for roughly two seconds. Anything slower here and the
    // browser gives up before we answer.
    improvService.begin();
    improvService.serviceFor(2000);

    sysLog.println();
    sysLog.println("Selfbus KNX/IP Interface " FIRMWARE_VERSION);
    sysLog.printf("Device: %s\n", netManager.deviceName().c_str());

    if (hwConfig.usingDefaults())
    {
        sysLog.printf("Hardware: built-in defaults (%s)\n", hwConfig.fallbackReason());
    }

    ArduinoPlatform::SerialDebug = &sysLog;

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
        sysLog.println("WARNING: no answer from the SB-Interface on the KNX UART");
    }

    {
        String knxName = knxLink.friendlyName();
        sysLog.printf("KNX: discovery name \"%s\"\n",
                      knxName.length() ? knxName.c_str() : "(empty)");
    }

    /*
     * A fixed address entered in the ETS device properties wins over DHCP.
     * Only readable once the KNX stack has restored its memory, hence here
     * and not in netManager.begin().
     */
    uint32_t etsIp = 0, etsMask = 0, etsGateway = 0;

    if (knxLink.etsIpConfig(etsIp, etsMask, etsGateway))
    {
        netManager.applyStaticIp(etsIp, etsMask, etsGateway);
    }

    timeService.begin();
    webServerBegin();

    String host = netManager.deviceName();

    if (MDNS.begin(host.c_str()))
    {
        MDNS.addService("http", "tcp", 80);
        sysLog.printf("mDNS: http://%s.local\n", host.c_str());
    }

    // Blocks until the interface is up or the provisioning window closes.
    // Returns immediately in Ethernet mode.
    netManager.waitForConnection(provisioningKeepAlive);

    // The multicast membership was taken out before the interface had an
    // address; renew it now that it does.
    if (netManager.isOnline())
    {
        knxLink.restartIpLayer();
    }

    // Last: the first sample would otherwise cover the whole of startup.
    cpuLoad.begin();
}

void loop()
{
    // First, so the turn it measures is the whole turn.
    cpuLoad.pass();

    knxLink.loop();
    netManager.loop();
    timeService.loop();
    otaService.loop();
    hwConfig.loop();
    buttonService.loop();
    statusLed.loop();
    cpuLoad.loop();
}
