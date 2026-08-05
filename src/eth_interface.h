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
    enum State : uint8_t
    {
        DISABLED, //!< no CS pin configured, nothing was tried
        ABSENT,   //!< configured, but no W5500 answered the probe
        NO_LINK,  //!< chip found, cable unplugged or no partner
        NO_IP,    //!< link up, waiting for DHCP
        READY     //!< link up and addressed
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

    String   ipString() const;
    String   macString() const;
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
