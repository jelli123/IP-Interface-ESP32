/*
 *  rv3028.cpp - Minimal driver for the Micro Crystal RV-3028-C7 RTC.
 */

#include <Arduino.h>
#include <Wire.h>

#include "rv3028.h"

/* Register map, RV-3028-C7 application manual section 3. */
#define REG_SECONDS   0x00
#define REG_MINUTES   0x01
#define REG_HOURS     0x02
#define REG_WEEKDAY   0x03
#define REG_DATE      0x04
#define REG_MONTH     0x05
#define REG_YEAR      0x06
#define REG_STATUS    0x0E
#define REG_CTRL1     0x0F
#define REG_CTRL2     0x10
#define REG_EE_BACKUP 0x37

#define STATUS_PORF   0x01 //!< power on reset flag: supply was lost
#define CTRL1_EERD    0x08 //!< disable automatic EEPROM refresh

/** Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant). */
static int32_t daysFromCivil(int32_t year, uint32_t month, uint32_t day)
{
    year -= (month <= 2) ? 1 : 0;
    const int32_t  era = (year >= 0 ? year : year - 399) / 400;
    const uint32_t yoe = (uint32_t)(year - era * 400);                       // 0..399
    const uint32_t doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;              // 0..146096
    return era * 146097 + (int32_t)doe - 719468;
}

/** Inverse of daysFromCivil(). */
static void civilFromDays(int32_t days, int32_t& year, uint32_t& month, uint32_t& day)
{
    days += 719468;
    const int32_t  era = (days >= 0 ? days : days - 146096) / 146097;
    const uint32_t doe = (uint32_t)(days - era * 146097);                    // 0..146096
    const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int32_t  y   = (int32_t)yoe + era * 400;
    const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);            // 0..365
    const uint32_t mp  = (5 * doy + 2) / 153;                                // 0..11
    day   = doy - (153 * mp + 2) / 5 + 1;
    month = mp + (mp < 10 ? 3 : -9);
    year  = y + ((month <= 2) ? 1 : 0);
}

bool Rv3028::readRegs(uint8_t reg, uint8_t* buffer, uint8_t length)
{
    Wire.beginTransmission(RV3028_I2C_ADDRESS);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0)
    {
        return false;
    }
    if (Wire.requestFrom((uint8_t)RV3028_I2C_ADDRESS, length) != length)
    {
        return false;
    }
    for (uint8_t i = 0; i < length; i++)
    {
        buffer[i] = (uint8_t)Wire.read();
    }
    return true;
}

bool Rv3028::writeRegs(uint8_t reg, const uint8_t* buffer, uint8_t length)
{
    Wire.beginTransmission(RV3028_I2C_ADDRESS);
    Wire.write(reg);
    Wire.write(buffer, length);
    return Wire.endTransmission() == 0;
}

bool Rv3028::readReg(uint8_t reg, uint8_t& value)
{
    return readRegs(reg, &value, 1);
}

bool Rv3028::writeReg(uint8_t reg, uint8_t value)
{
    return writeRegs(reg, &value, 1);
}

bool Rv3028::begin(Rv3028Backup backup, Rv3028Trickle trickle)
{
    _present = false;

    uint8_t status;
    if (!readReg(REG_STATUS, status))
    {
        return false;
    }
    _present = true;

    /*
     * Configure backup switchover.
     *
     * Register 0x37 lives in the configuration EEPROM but is mirrored into
     * RAM, and that mirror is what the chip actually acts on. The mirror is
     * powered from the backed-up rail, so writing it here survives a VDD
     * loss - which is exactly the case it has to cover. Writing the EEPROM
     * itself would only matter if the backup source were empty too, and then
     * the time is gone anyway and we reconfigure on the next boot.
     *
     * So: RAM only. No EEPROM command sequence, no wear.
     */
    uint8_t eeBackup;
    if (!readReg(REG_EE_BACKUP, eeBackup))
    {
        return false;
    }

    eeBackup &= (uint8_t)~0x0C;                       // clear BSM
    eeBackup |= (uint8_t)((backup & 0x03) << 2);      // set BSM

    if (trickle == RV3028_TRICKLE_OFF)
    {
        eeBackup &= (uint8_t)~0x20;                   // TCE off
    }
    else
    {
        eeBackup |= 0x20;                             // TCE on
        eeBackup &= (uint8_t)~0x03;                   // clear TCR
        eeBackup |= (uint8_t)(trickle & 0x03);        // set TCR
    }

    return writeReg(REG_EE_BACKUP, eeBackup);
}

bool Rv3028::timeValid()
{
    uint8_t status;
    if (!_present || !readReg(REG_STATUS, status))
    {
        return false;
    }
    return (status & STATUS_PORF) == 0;
}

bool Rv3028::readUtc(uint32_t& utc)
{
    if (!_present || !timeValid())
    {
        return false;
    }

    uint8_t raw[7];
    if (!readRegs(REG_SECONDS, raw, sizeof(raw)))
    {
        return false;
    }

    uint8_t second = fromBcd(raw[0] & 0x7F);
    uint8_t minute = fromBcd(raw[1] & 0x7F);
    uint8_t hour   = fromBcd(raw[2] & 0x3F); // 24 h mode, set by default
    uint8_t day    = fromBcd(raw[4] & 0x3F);
    uint8_t month  = fromBcd(raw[5] & 0x1F);
    uint8_t year   = fromBcd(raw[6]);        // 00..99 => 2000..2099

    if (month < 1 || month > 12 || day < 1 || day > 31 ||
        hour > 23 || minute > 59 || second > 59)
    {
        return false;
    }

    int32_t days = daysFromCivil(2000 + year, month, day);
    utc = (uint32_t)(days * 86400L + hour * 3600L + minute * 60L + second);
    return true;
}

bool Rv3028::writeUtc(uint32_t utc)
{
    if (!_present)
    {
        return false;
    }

    int32_t  days    = (int32_t)(utc / 86400);
    uint32_t secOfDay = utc % 86400;

    int32_t  year;
    uint32_t month;
    uint32_t day;
    civilFromDays(days, year, month, day);

    if (year < 2000 || year > 2099)
    {
        return false; // outside the two-digit year range of the chip
    }

    // Weekday is derived, not trusted from the chip: the register is a free
    // running 0..6 counter with no defined epoch.
    uint8_t weekday = (uint8_t)(((days % 7) + 11) % 7); // 1970-01-01 was a Thursday

    uint8_t raw[7];
    raw[0] = toBcd((uint8_t)(secOfDay % 60));
    raw[1] = toBcd((uint8_t)((secOfDay / 60) % 60));
    raw[2] = toBcd((uint8_t)(secOfDay / 3600));
    raw[3] = weekday;
    raw[4] = toBcd((uint8_t)day);
    raw[5] = toBcd((uint8_t)month);
    raw[6] = toBcd((uint8_t)(year - 2000));

    if (!writeRegs(REG_SECONDS, raw, sizeof(raw)))
    {
        return false;
    }

    // Clear the power-on reset flag so the stored time counts as trustworthy.
    uint8_t status;
    if (!readReg(REG_STATUS, status))
    {
        return false;
    }
    return writeReg(REG_STATUS, (uint8_t)(status & ~STATUS_PORF));
}
