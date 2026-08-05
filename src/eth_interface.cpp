/*
 *  eth_interface.cpp - Optional W5500 Ethernet, probed at boot.
 */

#include <cstring>

#include "eth_interface.h"

#if SBIP_ETH_COMPILED
#include <ETH.h>
#include <SPI.h>

#include "hw_config.h"
#endif

EthInterface ethInterface;

#if SBIP_ETH_COMPILED

/* W5500 SPI framing: 16 bit address, 8 bit control, then data.
 * Control byte = BSB[7:3] | RWB[2] | OM[1:0].
 * Common register block, read, one fixed byte -> 0x01. */
#define W5500_ADDR_VERSIONR 0x0039
#define W5500_CTRL_READ_1   0x01
#define W5500_VERSION       0x04

/** SPI clock for the probe. Deliberately slow, the driver raises it later. */
static const uint32_t PROBE_SPI_HZ = 4000000UL;

/** How long to wait for the link to come up after starting the driver. */
static const uint32_t LINK_TIMEOUT_MS = 4000UL;

/** How long to wait for DHCP once the link is up. */
static const uint32_t IP_TIMEOUT_MS = 12000UL;

/** Interval of the link supervision in loop(). */
static const uint32_t CHECK_INTERVAL_MS = 2000UL;

void EthInterface::resetChip()
{
    const HwProfile& hw = hwConfig.active();

    if (hw.ethRstPin < 0)
    {
        return; // rely on the chip's power-on reset
    }

    pinMode(hw.ethRstPin, OUTPUT);
    digitalWrite(hw.ethRstPin, LOW);
    delay(2);   // datasheet asks for >500 us
    digitalWrite(hw.ethRstPin, HIGH);
    delay(60);  // PLL lock, ~50 ms
}

/*
 * Read the version register.
 *
 * Done by hand rather than by simply calling ETH.begin() and looking at the
 * return value: the driver logs a wall of errors when the chip is missing and
 * leaves the SPI bus attached. A single register read is deterministic, silent
 * and costs microseconds - which is what makes probing on every boot
 * acceptable even on boards that never have Ethernet fitted.
 *
 * VERSIONR is a constant 0x04 on every W5500, so it doubles as a presence
 * check and as a guard against reading a different chip on the same bus.
 */
bool EthInterface::probeChip()
{
    const HwProfile& hw = hwConfig.active();

    pinMode(hw.ethCsPin, OUTPUT);
    digitalWrite(hw.ethCsPin, HIGH);

    SPI.begin(hw.ethSckPin, hw.ethMisoPin, hw.ethMosiPin, -1);

    resetChip();

    SPI.beginTransaction(SPISettings(PROBE_SPI_HZ, MSBFIRST, SPI_MODE0));
    digitalWrite(hw.ethCsPin, LOW);
    SPI.transfer((uint8_t)(W5500_ADDR_VERSIONR >> 8));
    SPI.transfer((uint8_t)(W5500_ADDR_VERSIONR & 0xFF));
    SPI.transfer(W5500_CTRL_READ_1);
    uint8_t version = SPI.transfer(0x00);
    digitalWrite(hw.ethCsPin, HIGH);
    SPI.endTransaction();

    if (version != W5500_VERSION)
    {
        // 0x00 or 0xFF simply means nothing is driving MISO.
        if (version != 0x00 && version != 0xFF)
        {
            Serial.printf("ETH: unexpected chip on SPI, VERSIONR=0x%02X\n", version);
        }
        return false;
    }
    return true;
}

bool EthInterface::begin(void (*keepAlive)())
{
    const HwProfile& hw = hwConfig.active();

    if (!hw.ethEnabled)
    {
        return false; // not fitted according to the active hardware profile
    }

    if (!probeChip())
    {
        Serial.println("ETH: no W5500 found, continuing without Ethernet");
        return false;
    }

    _present = true;
    Serial.println("ETH: W5500 found");

    // The reset pin is handed over as -1: the chip was already reset in the
    // probe, and letting the driver toggle it again only costs another 60 ms.
    if (!ETH.begin(ETH_PHY_W5500, SBIP_ETH_PHY_ADDR, hw.ethCsPin,
                   hw.ethIrqPin, -1, SPI, hw.ethSpiMhz))
    {
        Serial.println("ETH: driver failed to start");
        return false;
    }
    _started = true;

    // Wait for the link. Without a cable this is the normal outcome, so it is
    // reported rather than treated as an error.
    uint32_t deadline = millis() + LINK_TIMEOUT_MS;
    while (!ETH.linkUp() && (int32_t)(millis() - deadline) < 0)
    {
        if (keepAlive) keepAlive();
        delay(10);
    }

    if (!ETH.linkUp())
    {
        Serial.println("ETH: no link (cable unplugged?)");
        return false;
    }

    Serial.printf("ETH: link up, %u Mbit/s %s duplex\n",
                  ETH.linkSpeed(), ETH.fullDuplex() ? "full" : "half");

    deadline = millis() + IP_TIMEOUT_MS;
    while (!ETH.hasIP() && (int32_t)(millis() - deadline) < 0)
    {
        if (keepAlive) keepAlive();
        delay(10);
    }

    if (!ETH.hasIP())
    {
        Serial.println("ETH: no address from DHCP");
        return false;
    }

    /*
     * Make Ethernet the default route.
     *
     * NetworkUDP::beginMulticast() joins the group with
     * imr_interface = INADDR_ANY, which lwIP resolves to the default netif.
     * The KNX stack's routing socket therefore has to land on this interface,
     * and ESP-IDF gives the WiFi station a higher route priority than
     * Ethernet by default.
     */
    ETH.setDefault();

    _wasUp = true;
    Serial.printf("ETH: ready, IP %s\n", ETH.localIP().toString().c_str());
    return true;
}

void EthInterface::loop()
{
    if (!_started)
    {
        return;
    }
    if ((uint32_t)(millis() - _lastCheckMs) < CHECK_INTERVAL_MS)
    {
        return;
    }
    _lastCheckMs = millis();

    bool up = ETH.linkUp() && ETH.hasIP();
    if (up == _wasUp)
    {
        return;
    }
    _wasUp = up;

    if (up)
    {
        // lwIP re-runs DHCP on its own after a cable reconnect, but the
        // default route has to be claimed again.
        ETH.setDefault();
        Serial.printf("ETH: link restored, IP %s\n", ETH.localIP().toString().c_str());
    }
    else
    {
        Serial.println("ETH: link lost");
    }
}

bool EthInterface::active() const
{
    return _started && ETH.linkUp() && ETH.hasIP();
}

EthInterface::State EthInterface::state() const
{
    if (!hwConfig.active().ethEnabled) return STATE_DISABLED;
    if (!_present)     return STATE_ABSENT;
    if (!_started)     return STATE_ABSENT;
    if (!ETH.linkUp()) return STATE_NO_LINK;
    if (!ETH.hasIP())  return STATE_NO_IP;
    return STATE_READY;
}

uint32_t EthInterface::ipAddress() const  { return (uint32_t)ETH.localIP(); }
uint32_t EthInterface::subnetMask() const { return (uint32_t)ETH.subnetMask(); }
uint32_t EthInterface::gateway() const    { return (uint32_t)ETH.gatewayIP(); }

void EthInterface::macAddress(uint8_t* out) const
{
    ETH.macAddress(out);
}

String   EthInterface::ipString() const   { return ETH.localIP().toString(); }
String   EthInterface::macString() const  { return ETH.macAddress(); }
uint16_t EthInterface::linkSpeed() const  { return ETH.linkSpeed(); }
bool     EthInterface::fullDuplex() const { return ETH.fullDuplex(); }

#else /* SBIP_ETH_COMPILED */

/*
 * Ethernet compiled out entirely (-DSBIP_ETH_COMPILED=0), for builds where
 * the ~40 KB of driver and lwIP glue are not worth the flash. Everything
 * collapses to "disabled" so the rest of the firmware needs no conditional
 * compilation.
 */
bool EthInterface::begin(void (*)()) { return false; }
void EthInterface::loop() {}
bool EthInterface::active() const { return false; }

EthInterface::State EthInterface::state() const { return STATE_DISABLED; }

uint32_t EthInterface::ipAddress() const  { return 0; }
uint32_t EthInterface::subnetMask() const { return 0; }
uint32_t EthInterface::gateway() const    { return 0; }
void     EthInterface::macAddress(uint8_t* out) const { memset(out, 0, 6); }
String   EthInterface::ipString() const   { return String("0.0.0.0"); }
String   EthInterface::macString() const  { return String(""); }
uint16_t EthInterface::linkSpeed() const  { return 0; }
bool     EthInterface::fullDuplex() const { return false; }

#endif /* SBIP_ETH_COMPILED */

const char* EthInterface::stateName() const
{
    switch (state())
    {
    case STATE_DISABLED: return "disabled";
    case STATE_ABSENT:   return "absent";
    case STATE_NO_LINK:  return "no_link";
    case STATE_NO_IP:    return "no_ip";
    case STATE_READY:    return "ready";
    }
    return "?";
}
