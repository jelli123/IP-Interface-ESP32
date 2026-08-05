/*
 *  knx_link.cpp - KNX stack wiring and TP1 link supervision.
 */

#include <Arduino.h>
#include <HardwareSerial.h>
#include <knx.h>
#include <knx/bau091A.h>
#include <knx/cemi_frame.h>
#include <knx/ip_data_link_layer.h>
#include <knx/tpuart_data_link_layer.h>

#include <cstring>

#include "eth_interface.h"
#include "hw_config.h"
#include "interface_config.h"
#include "knx_link.h"

KnxLink knxLink;

/*
 * Platform with a runtime-selectable network interface.
 *
 * esp32_platform.cpp resolves the interface at COMPILE time:
 *
 *     #ifdef KNX_IP_LAN
 *         #define KNX_NETIF ETH
 *     #else
 *         #define KNX_NETIF WiFi
 *     #endif
 *
 * That would force two firmware images and defeat the whole point of probing
 * for the W5500. The four accessors are virtual, so overriding them here is
 * enough - no patch to the stack.
 *
 * It matters: these values end up in the KNXnet/IP Search Response and
 * Connect Response (HPAI). Reporting the WiFi interface while actually
 * running on Ethernet would hand ETS an unusable endpoint - typically
 * 0.0.0.0, because WiFi is not even started in that case.
 *
 * The UDP side needs no override: Arduino-ESP32 3.x defines
 * "typedef NetworkUDP WiFiUDP", which is a plain BSD socket wrapper and not
 * bound to WiFi at all. Multicast reaches the right interface because
 * EthInterface::begin() claims the default route via ETH.setDefault().
 */
class SbipPlatform : public Esp32Platform
{
public:
    explicit SbipPlatform(HardwareSerial* serial) : Esp32Platform(serial) {}

    uint32_t currentIpAddress() override
    {
        return ethInterface.active() ? ethInterface.ipAddress()
                                     : Esp32Platform::currentIpAddress();
    }

    uint32_t currentSubnetMask() override
    {
        return ethInterface.active() ? ethInterface.subnetMask()
                                     : Esp32Platform::currentSubnetMask();
    }

    uint32_t currentDefaultGateway() override
    {
        return ethInterface.active() ? ethInterface.gateway()
                                     : Esp32Platform::currentDefaultGateway();
    }

    void macAddress(uint8_t* addr) override
    {
        if (ethInterface.active())
        {
            ethInterface.macAddress(addr);
        }
        else
        {
            Esp32Platform::macAddress(addr);
        }
    }
};

/*
 * The KNX facade. Created here instead of relying on the library's automatic
 * global instance (-DKNX_NO_AUTOMATIC_GLOBAL_INSTANCE) so the UART object and
 * the network interface can be chosen at runtime.
 *
 * The HardwareSerial instance is built in begin(), not statically: its UART
 * number is a constructor argument, and static objects are constructed before
 * setup() runs - long before the hardware profile has been read from NVS.
 * The platform is handed a null port until then, which is safe because
 * nothing touches the UART before begin().
 */
static HardwareSerial* knxSerial = nullptr;
static SbipPlatform   knxPlatform(nullptr);
static Bau091A        knxBau(knxPlatform);
KnxFacade<Esp32Platform, Bau091A> knx(knxBau);

/** Pointer used by the static activity trampoline. */
static KnxLink* s_instance = nullptr;

/** Sliding window for the bus load figure, in milliseconds. */
static const uint32_t BUS_LOAD_WINDOW_MS = 1000;

/**
 * Frames per second that count as 100 % bus load.
 *
 * TP1 runs at 9600 bit/s. A minimal L_Data frame is 9 octets plus ACK, and
 * every octet costs 13 bit times including the two fill bits, so roughly
 * 50 frames/s saturate the line. This is an indicator, not a measurement.
 */
static const uint32_t BUS_LOAD_FRAMES_PER_SEC = 50;

bool KnxLink::begin()
{
    s_instance = this;

    const HwProfile& hw = hwConfig.active();

    // The UART number only exists as a constructor argument, so the port is
    // built here, once the profile is known. Never freed - it lives as long
    // as the firmware does.
    knxSerial = new HardwareSerial((uint8_t)hw.knxUartNum);
    knxPlatform.knxUart(knxSerial);

    // Do not call knxSerial->begin() here. Esp32Platform::setupUart() is
    // invoked by TpUartDataLinkLayer::reset() and unconditionally re-opens the
    // port with 19200 baud, 8E1 and these pins, so anything configured earlier
    // is discarded. 8E1 is mandatory: the TP-UART protocol relies on the
    // parity bit for octet level error detection.
    knxPlatform.knxUartPins(hw.knxRxPin, hw.knxTxPin);

    knx.ledPin(hw.ledPin);
    knx.ledPinActiveOn(hw.ledActiveLow ? LOW : HIGH);
    knx.buttonPin(-1); // the button is handled by NetManager, not by the stack

#ifdef KNX_ACTIVITYCALLBACK
    knxBau.setActivityCallback(activityTrampoline);
#endif

    knx.readMemory();
    knx.start();

    // Give the stack a moment to run its reset handshake (U_Reset.req ->
    // U_Reset.ind) and the first U_State.req before judging the link.
    uint32_t deadline = millis() + 500;
    while ((int32_t)(millis() - deadline) < 0)
    {
        knx.loop();
        delay(5);
    }

    TpUartDataLinkLayer* tp = knxBau.getSecondaryDataLinkLayer();
    if (tp == nullptr)
    {
        strlcpy(_selfTest, "FAIL (no TP layer)", sizeof(_selfTest));
        return false;
    }

    if (!tp->isConnected())
    {
        strlcpy(_selfTest, "FAIL (no answer from SB-Interface)", sizeof(_selfTest));
        return false;
    }

    strlcpy(_selfTest, "OK", sizeof(_selfTest));
    return true;
}

void KnxLink::loop()
{
    knx.loop();

    // Apply a web requested programming mode change from the main task.
    if (_progModePending)
    {
        bool value = _progModeValue;
        _progModePending = false;
        knx.progMode(value);
    }

    TpUartDataLinkLayer* tp = knxBau.getSecondaryDataLinkLayer();
    if (tp != nullptr)
    {
        _stats.tpRxFrames    = tp->getRxProcessdFrameCounter();
        _stats.tpRxIgnored   = tp->getRxIgnoredFrameCounter();
        _stats.tpRxInvalid   = tp->getRxInvalidFrameCounter();
        _stats.tpTxFrames    = tp->getTxFrameCounter();
        _stats.tpTxProcessed = tp->getTxProcessedFrameCounter();
    }

    updateBusLoad();
    superviseTpLink();
}

/*
 * The stack polls the TP-UART with U_State.req once a second and declares the
 * link dead after 5 s without any answer. It recovers on its own as soon as
 * bytes arrive again, but if the SB-Interface was power cycled or reset it
 * comes up in a state the stack does not know about. Re-running the reset
 * handshake re-synchronises both sides.
 */
void KnxLink::superviseTpLink()
{
    if ((uint32_t)(millis() - _lastLinkCheck) < 10000)
    {
        return;
    }
    _lastLinkCheck = millis();

    TpUartDataLinkLayer* tp = knxBau.getSecondaryDataLinkLayer();
    if (tp != nullptr && !tp->isConnected())
    {
        Serial.println("TP link down - re-running the TP-UART reset handshake");
        tp->reset();
    }
}

void KnxLink::updateBusLoad()
{
    uint32_t now = millis();
    if ((uint32_t)(now - _lastBusLoadWindow) < BUS_LOAD_WINDOW_MS)
    {
        return;
    }
    _lastBusLoadWindow = now;

    uint32_t permille = (_framesInWindow * 1000) / BUS_LOAD_FRAMES_PER_SEC;
    _stats.busLoadPermille = (permille > 1000) ? 1000 : (uint16_t)permille;
    _framesInWindow = 0;
}

void KnxLink::activityTrampoline(uint8_t info)
{
    if (s_instance != nullptr)
    {
        s_instance->onActivity(info);
    }
}

/*
 * info carries the net index in the upper bits and the direction in bit 0.
 * Bau091A registers the IP layer as net 1 (primary) and the TP layer as
 * net 2 (secondary).
 */
void KnxLink::onActivity(uint8_t info)
{
#ifdef KNX_ACTIVITYCALLBACK
    uint8_t net  = info >> KNX_ACTIVITYCALLBACK_NET;
    bool    send = ((info >> KNX_ACTIVITYCALLBACK_DIR) & 0x01) == KNX_ACTIVITYCALLBACK_DIR_SEND;

    if (net == 1)
    {
        if (send)
        {
            _stats.ipTxFrames++;
        }
        else
        {
            _stats.ipRxFrames++;
        }
    }
    else
    {
        // Every TP1 frame, in either direction, occupies the line.
        _framesInWindow++;
    }
#else
    (void)info;
#endif
}

bool KnxLink::tpConnected() const
{
    TpUartDataLinkLayer* tp = knxBau.getSecondaryDataLinkLayer();
    return (tp != nullptr) && tp->isConnected();
}

bool KnxLink::configured() const
{
    return knx.configured();
}

uint16_t KnxLink::individualAddress() const
{
    return knx.individualAddress();
}

bool KnxLink::progMode() const
{
    return knx.progMode();
}

void KnxLink::requestProgMode(bool active)
{
    _progModeValue   = active;
    _progModePending = true;
}

void KnxLink::restartIpLayer()
{
    IpDataLinkLayer* ip = knxBau.getPrimaryDataLinkLayer();
    if (ip == nullptr)
    {
        return;
    }

    ip->enabled(false);
    ip->enabled(true);
    Serial.println("KNX: KNXnet/IP multicast re-armed on the active interface");
}

/*
 * Group telegrams without group objects.
 *
 * Bau091A is a coupler (BauSystemBCoupler) and has no group object table, so
 * the usual path through the application layer does not exist. DataLinkLayer::
 * dataRequest() is public though, and lands in sendTelegram() which fills in
 * the frame header and calls sendFrame() on the medium.
 *
 * Called on the TP layer it also mirrors the frame to every open tunnel
 * (sendTelegram does that for the secondary interface); called on the IP layer
 * it emits the routing multicast. Both together are what a real coupler puts
 * out for a locally generated telegram.
 *
 * Deliberately NOT dataRequestFromTunnel(): that one routes through
 * frameReceived(), which flags individualAddressDuplication() as soon as the
 * source address equals our own - which is exactly what we send here.
 */
bool KnxLink::sendGroupValue(uint16_t groupAddress, const uint8_t* payload, uint8_t length)
{
    if (groupAddress == 0 || payload == nullptr || length == 0 || length > 14)
    {
        return false;
    }

    // One octet for the APCI, then the payload. Values of 6 bit or less would
    // normally be packed into the APCI octet; no time DPT is that small, so
    // the unpacked form is always correct here.
    CemiFrame frame((uint8_t)(length + 1));
    APDU& apdu = frame.apdu();
    apdu.type(GroupValueWrite);
    memcpy(apdu.data() + 1, payload, length);

    // The frame buffer starts out zeroed, and a hop count of 0 means "do not
    // route" - routers would drop it. 6 is the KNX default.
    frame.npdu().hopCount(6);

    uint16_t source = knx.individualAddress();

    TpUartDataLinkLayer* tp = knxBau.getSecondaryDataLinkLayer();
    IpDataLinkLayer*     ip = knxBau.getPrimaryDataLinkLayer();
    bool sent = false;

    if (tp != nullptr)
    {
        tp->dataRequest(AckRequested, GroupAddress, groupAddress, source,
                        StandardFrame, LowPriority, frame.npdu());
        sent = true;
    }

    if (ip != nullptr)
    {
        ip->dataRequest(AckRequested, GroupAddress, groupAddress, source,
                        StandardFrame, LowPriority, frame.npdu());
        sent = true;
    }

    return sent;
}
