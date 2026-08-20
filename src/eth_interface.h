/*
 *  eth_interface.h - Optional W5500 Ethernet, probed at boot.
 *
 *  The W5500 is used as a plain MAC/PHY: the ESP-IDF driver puts it into
 *  MACRAW mode and lwIP does the whole IP stack. The chip's own hardwired
 *  TCP/IP engine stays unused - that is what makes it behave like any other
 *  network interface and keeps KNXnet/IP routing multicast working.
 */
#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "interface_config.h"

class EthInterface
{
public:
    /*
     * Prefixed because Arduino-ESP32 defines a bare `DISABLED` macro in
     * esp32-hal-gpio.h (`#define DISABLED 0x00`). An unprefixed enumerator of
     * that name is rewritten by the preprocessor and the header stops parsing.
     * The wire format is unaffected - stateName() still reports "disabled".
     */
    enum State : uint8_t
    {
        STATE_DISABLED, //!< no CS pin configured, nothing was tried
        STATE_ABSENT,   //!< configured, but no W5500 answered the probe
        STATE_NO_LINK,  //!< chip found, cable unplugged or no partner
        STATE_NO_IP,    //!< link up, waiting for DHCP
        STATE_READY     //!< link up and addressed
    };

    /**
     * Probe for a W5500 and, if found, start the driver and wait for an
     * address.
     *
     * @param keepAlive called repeatedly during the wait so the KNX stack
     *                  stays responsive; may be nullptr
     * @return true if the interface came up and can carry KNX traffic
     */
    bool begin(void (*keepAlive)() = nullptr);

    /** Link supervision. Call from the main loop. */
    void loop();

    /** @return true if a W5500 answered the version register probe */
    bool chipPresent() const { return _present; }

    /** @return true if the interface holds an address and is usable */
    bool active() const;

    State       state() const;
    const char* stateName() const;

    uint32_t ipAddress() const;
    uint32_t subnetMask() const;
    uint32_t gateway() const;
    void     macAddress(uint8_t* out) const;

    /**
     * Switch to a fixed address, for a configuration programmed by ETS.
     *
     * The gateway doubles as the DNS server: KNX has no field for one, and
     * the gateway is right in most networks.
     *
     * @return true if the interface accepted the configuration
     */
    bool configure(uint32_t ip, uint32_t mask, uint32_t gw);

    String   ipString() const;
    String   macString() const;
    String   dnsString() const;
    uint16_t linkSpeed() const;
    bool     fullDuplex() const;

private:
    static bool probeChip();
    static void resetChip();

    bool     _present     = false;
    bool     _started     = false;
    bool     _wasUp       = false;
    uint32_t _lastCheckMs = 0;
};

extern EthInterface ethInterface;
