/*
 *  knx_link.cpp - KNX stack wiring and TP1 link supervision.
 */

#include <Arduino.h>
#include <HardwareSerial.h>
#include <Preferences.h>
#include <esp_partition.h>
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

#include "log_buffer.h"
KnxLink knxLink;

/*
 * Set by scripts/patch_knx.py inside RouterObject::isGroupAddressInFilterTable().
 * Declared here so a build without the patch fails to link rather than
 * silently ignoring the setting.
 */
extern bool sbipRouteUnfiltered;

static Preferences  knxPrefs;
static const char*  KNX_NS       = "sbip-knx";
static const char*  KEY_ROUTEALL = "routeall";

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

    /*
     * KNX non volatile memory, backed by the dedicated knxcfg partition.
     *
     * Esp32Platform stores it in an NVS blob through the Arduino EEPROM
     * library. That works for a kilobyte, but a filter table for main groups
     * 0-31 alone is 8 KiB, and NVS keeps a second copy while rewriting - the
     * 20 KiB nvs partition cannot do that next to the WiFi credentials and
     * the hardware profile. An own partition sidesteps the whole question.
     *
     * The header matters: adding a partition to the table does not erase the
     * flash behind it. knxcfg reuses the address the coredump partition had,
     * so without this check the stack restores its objects from an old core
     * dump - which is ELF formatted and produced a null property table and a
     * LoadProhibited panic. A size change has the same effect, hence the
     * length is part of the header.
     */
    uint8_t* getEepromBuffer(uint32_t size) override
    {
        if (_knxMemory != nullptr) return _knxMemory;

        _knxPartition = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "knxcfg");

        if (_knxPartition == nullptr || _knxPartition->size < size + HEADER_SIZE)
        {
            sysLog.println("KNX: knxcfg partition missing or too small");
            return nullptr;
        }

        _knxMemory = (uint8_t*)malloc(size);

        if (_knxMemory == nullptr)
        {
            sysLog.printf("KNX: could not allocate %u bytes of config memory\n",
                          (unsigned)size);
            return nullptr;
        }

        _knxSize = size;

        uint8_t header[HEADER_SIZE] = {0};
        bool    ours = false;

        if (esp_partition_read(_knxPartition, 0, header, sizeof(header)) == ESP_OK)
        {
            uint32_t storedSize = (uint32_t)header[8] | ((uint32_t)header[9] << 8) |
                                  ((uint32_t)header[10] << 16) |
                                  ((uint32_t)header[11] << 24);

            ours = memcmp(header, MAGIC, sizeof(MAGIC)) == 0 && storedSize == size;
        }

        if (ours && esp_partition_read(_knxPartition, HEADER_SIZE, _knxMemory,
                                       size) == ESP_OK)
        {
            return _knxMemory;
        }

        // Blank or foreign content. 0xFF is what an unprogrammed device looks
        // like, so the stack starts from scratch instead of from rubbish.
        sysLog.println("KNX: knxcfg holds no valid configuration, starting empty");
        memset(_knxMemory, 0xFF, size);
        return _knxMemory;
    }

    void commitToEeprom() override
    {
        if (_knxMemory == nullptr || _knxPartition == nullptr) return;

        uint8_t header[HEADER_SIZE] = {0};
        memcpy(header, MAGIC, sizeof(MAGIC));
        header[8]  = (uint8_t)(_knxSize & 0xFF);
        header[9]  = (uint8_t)((_knxSize >> 8) & 0xFF);
        header[10] = (uint8_t)((_knxSize >> 16) & 0xFF);
        header[11] = (uint8_t)((_knxSize >> 24) & 0xFF);

        // erase_range insists on whole 4 KiB sectors; anything else fails with
        // ESP_ERR_INVALID_SIZE and the configuration is silently lost.
        const uint32_t SECTOR = 4096;
        uint32_t erase = (_knxSize + HEADER_SIZE + SECTOR - 1) / SECTOR * SECTOR;

        if (erase > _knxPartition->size)
        {
            sysLog.println("KNX: knxcfg partition too small to write");
            return;
        }

        esp_err_t err = esp_partition_erase_range(_knxPartition, 0, erase);

        if (err == ESP_OK)
            err = esp_partition_write(_knxPartition, 0, header, sizeof(header));

        if (err == ESP_OK)
            err = esp_partition_write(_knxPartition, HEADER_SIZE, _knxMemory, _knxSize);

        if (err != ESP_OK)
        {
            sysLog.printf("KNX: writing the knxcfg partition failed: %s\n",
                          esp_err_to_name(err));
        }
    }

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

private:
    // Erase granularity is 4 KiB, so the header costs nothing beyond a
    // rounding of the erase range.
    static const uint32_t HEADER_SIZE = 16;
    static constexpr const char MAGIC[8] = {'S', 'B', 'I', 'P', 'K', 'N', 'X', '1'};

    const esp_partition_t* _knxPartition = nullptr;
    uint8_t*               _knxMemory    = nullptr;
    uint32_t               _knxSize      = 0;
};

constexpr const char SbipPlatform::MAGIC[8];

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
/*
 * Bau091A keeps its interface objects private and getInterfaceObject()
 * protected. Deriving is enough to reach them - no library patch needed.
 */
class SbipBau : public Bau091A
{
public:
    using Bau091A::Bau091A;

    InterfaceObject* interfaceObject(ObjectType type, uint16_t instance)
    {
        return getInterfaceObject(type, instance);
    }
};

static SbipBau        knxBau(knxPlatform);
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

/*
 * Tell ETS what this device claims to be.
 *
 * Must run before knx.start(): the values end up in the device object and in
 * the application program object, both of which ETS reads during a download.
 */
void KnxLink::applyIdentity()
{
    // ETS manages exactly as many tunnel addresses as the product data
    // declares, so a mismatch would leave addresses unaccounted for.
    static_assert(KNX_TUNNELING == SBIP_KNX_TUNNELS,
                  "KNX_TUNNELING must match the selected product profile - "
                  "see the [knx_product] section in platformio.ini");

    DeviceObject& dev = knxBau.deviceObject();

    dev.manufacturerId(SBIP_KNX_MANUFACTURER_ID);
    dev.version(SBIP_KNX_DEVICE_VERSION);

    const uint8_t hardwareType[LEN_HARDWARE_TYPE] = {SBIP_KNX_HARDWARE_TYPE};
    dev.hardwareType(hardwareType);

    // PID_PROG_VERSION is manufacturer, application number and application
    // version in five octets. Written through the public property API so no
    // private stack member is needed.
    uint8_t progVersion[5] = {
        (uint8_t)(SBIP_KNX_MANUFACTURER_ID >> 8),
        (uint8_t)(SBIP_KNX_MANUFACTURER_ID & 0xFF),
        (uint8_t)(SBIP_KNX_APP_NUMBER >> 8),
        (uint8_t)(SBIP_KNX_APP_NUMBER & 0xFF),
        (uint8_t)SBIP_KNX_APP_VERSION,
    };

    uint8_t count = 1;
    knxBau.propertyValueWrite(OT_APPLICATION_PROG, 1, PID_PROG_VERSION,
                              count, 1, progVersion, sizeof(progVersion));

    sysLog.printf("KNX: %s - manufacturer 0x%04X, application 0x%04X v%u\n",
                  SBIP_KNX_PRODUCT_NAME,
                  (unsigned)SBIP_KNX_MANUFACTURER_ID,
                  (unsigned)SBIP_KNX_APP_NUMBER,
                  (unsigned)SBIP_KNX_APP_VERSION);
}

bool KnxLink::begin()
{
    s_instance = this;

    applyIdentity();

    if (knxPrefs.begin(KNX_NS, false))
    {
        sbipRouteUnfiltered = knxPrefs.getBool(KEY_ROUTEALL, false);
        knxPrefs.end();
    }

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

    // StatusLed drives the programming indicator: it has to work for an
    // addressable LED too, which the stack cannot switch on its own.
    knx.ledPin(-1);
    knx.buttonPin(-1); // the button is handled by ButtonService, not the stack

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
    /*
     * The UART belongs to someone else for a while.
     *
     * Acknowledging here rather than in suspend() is the whole point: the
     * stack is only ever driven from this task, so this is the one place
     * where stopping it cannot land in the middle of a frame.
     */
    if (_suspendRequest)
    {
        if (!_suspended)
        {
            if (knxSerial != nullptr) knxSerial->end();
            _suspended = true;
            sysLog.println("KNX: stack paused, UART handed over");
        }
        return;
    }

    if (_suspended)
    {
        _suspended = false;

        // reset() runs Esp32Platform::setupUart(), which reopens the port
        // with 19200 baud and 8E1 - so this both re-arms the port and
        // resynchronises with a TP-UART that was just power cycled.
        TpUartDataLinkLayer* tp = knxBau.getSecondaryDataLinkLayer();
        if (tp != nullptr) tp->reset();
        _lastLinkCheck = millis();
        sysLog.println("KNX: stack resumed");
    }

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
        sysLog.println("TP link down - re-running the TP-UART reset handshake");
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
    if (_stats.busLoadPermille > _stats.busLoadPeak)
    {
        _stats.busLoadPeak = _stats.busLoadPermille;
    }
    _framesInWindow = 0;
}

void KnxLink::activityTrampoline(uint8_t info)
{
    if (s_instance != nullptr)
    {
        s_instance->onActivity(info);
    }
}

HardwareSerial* KnxLink::uart() const
{
    return knxSerial;
}

bool KnxLink::suspend(uint32_t timeoutMs)
{
    _suspendRequest = true;

    uint32_t deadline = millis() + timeoutMs;
    while (!_suspended && (int32_t)(millis() - deadline) < 0)
    {
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    if (!_suspended)
    {
        _suspendRequest = false;
        return false;
    }
    return true;
}

void KnxLink::resume()
{
    _suspendRequest = false;
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

uint8_t KnxLink::tunnelAddresses(uint16_t* out, uint8_t max) const
{
    if (out == nullptr || max == 0) return 0;

    // Same route the cEMI server takes, so no private stack member is needed.
    // Start index 0 answers with the element count rather than with data.
    uint8_t  count  = 1;
    uint32_t length = 0;
    uint8_t* data   = nullptr;

    knxBau.propertyValueRead(OT_IP_PARAMETER, 1, PID_ADDITIONAL_INDIVIDUAL_ADDRESSES,
                             count, 0, &data, length);
    if (data == nullptr) return 0;

    uint8_t stored = (length >= 2) ? data[1] : 0;
    delete[] data;

    if (stored == 0) return 0;
    if (stored > max) stored = max;

    count  = stored;
    length = 0;
    data   = nullptr;

    knxBau.propertyValueRead(OT_IP_PARAMETER, 1, PID_ADDITIONAL_INDIVIDUAL_ADDRESSES,
                             count, 1, &data, length);
    if (data == nullptr) return 0;

    uint8_t written = 0;
    for (uint8_t i = 0; i < count && (i * 2 + 1) < length; i++)
    {
        out[written++] = ((uint16_t)data[i * 2] << 8) | data[i * 2 + 1];
    }

    delete[] data;
    return written;
}

String KnxLink::friendlyName() const
{
    // Lives in the IP parameter object, not the device object - the name is
    // part of the KNXnet/IP device DIB, not of the KNX device description.
    uint8_t  count  = 30; // PID_FRIENDLY_NAME is a fixed 30 octet string
    uint32_t length = 0;
    uint8_t* data   = nullptr;

    knxBau.propertyValueRead(OT_IP_PARAMETER, 1, PID_FRIENDLY_NAME, count, 1,
                             &data, length);
    if (data == nullptr) return String();

    String name;
    for (uint32_t i = 0; i < length; i++)
    {
        char c = (char)data[i];
        if (c == '\0') break;
        // Anything outside plain ASCII is a half written property, not a name.
        name += (c >= 0x20 && c < 0x7F) ? c : '?';
    }

    delete[] data;
    name.trim();
    return name;
}

bool KnxLink::progMode() const
{
    return knx.progMode();
}

void KnxLink::routeUnfiltered(bool enable)
{
    sbipRouteUnfiltered = enable;

    if (knxPrefs.begin(KNX_NS, false))
    {
        knxPrefs.putBool(KEY_ROUTEALL, enable);
        knxPrefs.end();
    }
}

bool KnxLink::routeUnfiltered() const
{
    return sbipRouteUnfiltered;
}

/** Read one unsigned long property of the KNXnet/IP parameter object. */
static uint32_t readIpParamLong(uint8_t propertyId)
{
    uint8_t  count  = 1;
    uint32_t length = 0;
    uint8_t* data   = nullptr;

    knxBau.propertyValueRead(OT_IP_PARAMETER, 1, propertyId, count, 1,
                             &data, length);
    if (data == nullptr) return 0;

    uint32_t value = 0;

    if (length >= 4)
    {
        value = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
                ((uint32_t)data[2] << 8) | data[3];
    }

    delete[] data;
    return value;
}

bool KnxLink::etsIpConfig(uint32_t& ip, uint32_t& mask, uint32_t& gw) const
{    uint8_t  count  = 1;
    uint32_t length = 0;
    uint8_t* data   = nullptr;

    knxBau.propertyValueRead(OT_IP_PARAMETER, 1, PID_IP_ASSIGNMENT_METHOD,
                             count, 1, &data, length);
    if (data == nullptr) return false;

    uint8_t method = (length >= 1) ? data[0] : 0;
    delete[] data;

    // Bit 0 is manual assignment. An unprogrammed device reads 0xFF here, so
    // the address has to be plausible as well before we believe it.
    if ((method & 0x01) == 0) return false;

    ip = readIpParamLong(PID_IP_ADDRESS);
    if (ip == 0 || ip == 0xFFFFFFFF) return false;

    mask = readIpParamLong(PID_SUBNET_MASK);
    if (mask == 0 || mask == 0xFFFFFFFF) return false;

    gw = readIpParamLong(PID_DEFAULT_GATEWAY);
    if (gw == 0xFFFFFFFF) gw = 0;

    return true;
}

bool KnxLink::filterTable(uint16_t* out, uint16_t max, uint16_t& total) const
{
    total = 0;

    RouterObject* router = (RouterObject*)knxBau.interfaceObject(OT_ROUTER, 1);
    if (router == nullptr) return false;

    // Group address 0 is the broadcast address and never sits in the table.
    for (uint32_t address = 1; address <= 0xFFFF; address++)
    {
        if (!router->isGroupAddressInFilterTable((uint16_t)address)) continue;

        if (total < max) out[total] = (uint16_t)address;
        total++;
    }

    // An unprogrammed coupler answers "not in the table" for every address,
    // which is indistinguishable from an empty one - report the load state so
    // the dashboard can tell the two apart.
    return knx.configured();
}

void KnxLink::requestProgMode(bool active)
{
    _progModeValue   = active;
    _progModePending = true;
}

bool KnxLink::resetConfiguration()
{
    uint8_t* nvm  = knxPlatform.getNonVolatileMemoryStart();
    size_t   size = knxPlatform.getNonVolatileMemorySize();

    if (nvm == nullptr || size == 0)
    {
        return false;
    }

    // 0xFF is what an unwritten area reads as, and what readMemory() treats as
    // "never programmed" - the same state the stack reports on a fresh chip
    // with "DataObject api changed, any data stored in flash is invalid".
    memset(nvm, 0xFF, size);
    knxPlatform.commitNonVolatileMemory();

    sysLog.printf("KNX: configuration cleared (%u bytes), restart required\n",
                  (unsigned)size);
    return true;
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
    sysLog.println("KNX: KNXnet/IP multicast re-armed on the active interface");
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
