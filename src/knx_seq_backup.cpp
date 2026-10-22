/*
 *  knx_seq_backup.cpp - Last valid sequence numbers in FRAM or RTC RAM.
 *  See knx_seq_backup.h.
 */

#include "knx_seq_backup.h"

#include <Wire.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <string.h>

#include "hw_config.h"
#include "i2c_bus.h"
#include "log_buffer.h"

KnxSeqBackup knxSeqBackup;

namespace
{

const uint8_t FRAM_ADDRESS     = 0x50;
const uint8_t DEVICE_ID_ADDRESS = 0xF8 >> 1;  // reserved slave ID, as an Arduino 7-bit address

const uint16_t FUJITSU     = 0x00A;
const uint8_t  MIN_DENSITY = 3;  // 64 kbit - from here on two address octets

/*
 * Header, then one entry per slot: address (2), sequence number (6), CRC
 * (2) over both and the slot number - so an entry copied into another slot
 * does not pass.
 */
const uint8_t  MAGIC[4]    = {'S', 'B', 'S', 'Q'};
const uint8_t  VERSION     = 1;
const uint8_t  HEADER_SIZE = 8;
const uint8_t  ENTRY_SIZE  = 10;
const uint16_t AREA_SIZE   = HEADER_SIZE + KnxSeqBackup::SLOTS * ENTRY_SIZE;

/*
 * Not cleared by the startup code: survives software resets, watchdogs and
 * panics. After power-on it holds noise, which the reset reason and the CRCs
 * keep out.
 */
RTC_NOINIT_ATTR uint8_t rtcArea[AREA_SIZE];

uint16_t crc16(const uint8_t* data, size_t length, uint8_t slot)
{
    // CRC-16/CCITT-FALSE, the slot number first.
    uint16_t crc = 0xFFFF;

    auto add = [&crc](uint8_t octet) {
        crc ^= (uint16_t)octet << 8;
        for (int bit = 0; bit < 8; bit++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    };

    add(slot);
    for (size_t i = 0; i < length; i++)
        add(data[i]);
    return crc;
}

} // namespace

const char* KnxSeqBackup::mediumName() const
{
    switch (_medium)
    {
        case FRAM:    return "FRAM";
        case RTC_RAM: return "RTC RAM";
        default:      return "none";
    }
}

bool KnxSeqBackup::framProbe()
{
    if (!i2cBusBegin())
        return false;

    // Anyone home at 0x50? Asked first, so an empty bus does not fill the
    // log with failed repeated starts.
    Wire.beginTransmission(FRAM_ADDRESS);
    if (Wire.endTransmission() != 0)
    {
        if (hwConfig.active().framFitted)
            sysLog.println("SEQ: the hardware profile names a FRAM, but nothing answers at I2C 0x50");
        return false;
    }

    if (hwConfig.active().framFitted)
        return true;

    // Device ID: the slave address shifted, to the reserved address F8h, then
    // three octets - 12 bits manufacturer, 4 bits density, 8 bits product.
    Wire.beginTransmission(DEVICE_ID_ADDRESS);
    Wire.write((uint8_t)(FRAM_ADDRESS << 1));
    Wire.endTransmission(false);

    uint8_t id[3] = {0};
    if (Wire.requestFrom(DEVICE_ID_ADDRESS, (uint8_t)3) != 3)
    {
        sysLog.println("SEQ: something at I2C 0x50 without a Device ID - not used; "
                       "a FRAM of that kind is enabled in the hardware profile");
        return false;
    }
    for (uint8_t& octet : id)
        octet = (uint8_t)Wire.read();

    uint16_t manufacturer = (uint16_t)((id[0] << 4) | (id[1] >> 4));
    uint8_t  density      = id[1] & 0x0F;

    if (manufacturer != FUJITSU || density < MIN_DENSITY)
    {
        sysLog.printf("SEQ: I2C 0x50 is no usable FRAM (Device ID %02X %02X %02X)\n", id[0], id[1], id[2]);
        return false;
    }

    return true;
}

bool KnxSeqBackup::read(uint16_t offset, uint8_t* data, uint8_t length)
{
    if (_medium == RTC_RAM)
    {
        memcpy(data, rtcArea + offset, length);
        return true;
    }

    if (_medium != FRAM)
        return false;

    Wire.beginTransmission(FRAM_ADDRESS);
    Wire.write((uint8_t)(offset >> 8));
    Wire.write((uint8_t)offset);
    Wire.endTransmission(false);

    if (Wire.requestFrom(FRAM_ADDRESS, length) != length)
        return false;
    for (uint8_t i = 0; i < length; i++)
        data[i] = (uint8_t)Wire.read();
    return true;
}

bool KnxSeqBackup::write(uint16_t offset, const uint8_t* data, uint8_t length)
{
    if (_medium == RTC_RAM)
    {
        memcpy(rtcArea + offset, data, length);
        return true;
    }

    if (_medium != FRAM)
        return false;

    Wire.beginTransmission(FRAM_ADDRESS);
    Wire.write((uint8_t)(offset >> 8));
    Wire.write((uint8_t)offset);
    Wire.write(data, length);
    return Wire.endTransmission() == 0;
}

bool KnxSeqBackup::headerValid()
{
    uint8_t header[HEADER_SIZE];
    return read(0, header, sizeof(header)) && memcmp(header, MAGIC, sizeof(MAGIC)) == 0 &&
           header[4] == VERSION && header[5] == SLOTS;
}

void KnxSeqBackup::begin()
{
    _medium = framProbe() ? FRAM : RTC_RAM;

    // RTC RAM loses its content with the supply; what it holds then is noise.
    esp_reset_reason_t reason = esp_reset_reason();
    bool powerLost = reason == ESP_RST_POWERON || reason == ESP_RST_BROWNOUT || reason == ESP_RST_UNKNOWN;

    if ((_medium == RTC_RAM && powerLost) || !headerValid())
        clear();

    sysLog.printf("SEQ: last valid sequence numbers of the proxy kept in %s%s\n", mediumName(),
                  _medium == FRAM ? "" : " - lost with a power cut, NVS every 5 min");
}

void KnxSeqBackup::store(uint8_t slot, uint16_t address, uint64_t seq)
{
    if (slot >= SLOTS)
        return;

    uint8_t entry[ENTRY_SIZE];
    entry[0] = (uint8_t)(address >> 8);
    entry[1] = (uint8_t)address;
    for (int i = 0; i < 6; i++)
        entry[2 + i] = (uint8_t)(seq >> (8 * (5 - i)));

    uint16_t crc = crc16(entry, 8, slot);
    entry[8] = (uint8_t)(crc >> 8);
    entry[9] = (uint8_t)crc;

    if (!write(HEADER_SIZE + slot * ENTRY_SIZE, entry, sizeof(entry)))
    {
        static uint32_t reportedAt = 0;
        if (reportedAt == 0 || millis() - reportedAt > 60000)
        {
            reportedAt = millis() | 1;
            sysLog.println("SEQ: writing the FRAM failed");
        }
    }
}

bool KnxSeqBackup::load(uint8_t slot, uint16_t& address, uint64_t& seq)
{
    uint8_t entry[ENTRY_SIZE];

    if (slot >= SLOTS || !read(HEADER_SIZE + slot * ENTRY_SIZE, entry, sizeof(entry)))
        return false;

    if (crc16(entry, 8, slot) != (uint16_t)((entry[8] << 8) | entry[9]))
        return false;

    address = (uint16_t)((entry[0] << 8) | entry[1]);
    seq     = 0;
    for (int i = 0; i < 6; i++)
        seq = (seq << 8) | entry[2 + i];

    return address != 0 && seq != 0;
}

void KnxSeqBackup::clear()
{
    uint8_t empty[ENTRY_SIZE] = {0};
    for (uint8_t slot = 0; slot < SLOTS; slot++)
        write(HEADER_SIZE + slot * ENTRY_SIZE, empty, sizeof(empty));

    uint8_t header[HEADER_SIZE] = {0};
    memcpy(header, MAGIC, sizeof(MAGIC));
    header[4] = VERSION;
    header[5] = SLOTS;
    write(0, header, sizeof(header));
}
