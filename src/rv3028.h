/*
 *  rv3028.h - Minimal driver for the Micro Crystal RV-3028-C7 RTC.
 *
 *  Only what a time server needs: read and write the clock, and tell whether
 *  the stored time survived the last power loss.
 */
#pragma once

#include <stdint.h>

/** I2C address of the RV-3028-C7. Fixed, not selectable. */
#define RV3028_I2C_ADDRESS 0x52

/**
 * Backup power switchover mode, written to the configuration register on
 * every boot.
 *
 * The factory default is DISABLED, which means the RTC stops as soon as VDD
 * goes away even with a battery or supercap fitted. Without setting this, a
 * fitted backup source does nothing at all.
 *
 * The values are the BSM bit pattern, not an index: 10b is standby and not
 * a switchover at all, which is why it sits in the middle.
 */
enum Rv3028Backup : uint8_t
{
    RV3028_BACKUP_DISABLED = 0, //!< no switchover, RTC dies with VDD
    RV3028_BACKUP_DIRECT   = 1, //!< direct switching, for a battery
    RV3028_BACKUP_STANDBY  = 2, //!< standby, backup unused as well
    RV3028_BACKUP_LEVEL    = 3  //!< level switching, for a supercap
};

/**
 * Trickle charger series resistance, the TCR bit pattern.
 *
 * The resistors are inside the chip: the charger feeds the backup pin from
 * VDD through the one this selects, and nothing is fitted on the board for
 * it. Only for a supercap or a rechargeable cell; on anything else it has
 * to stay off.
 */
enum Rv3028Trickle : uint8_t
{
    RV3028_TRICKLE_OFF = 0xFF,
    RV3028_TRICKLE_3K  = 0,
    RV3028_TRICKLE_5K  = 1,
    RV3028_TRICKLE_9K  = 2,
    RV3028_TRICKLE_15K = 3
};

/**
 * Translate a series resistance in ohms into the register setting.
 *
 * Only the four values the chip actually has are accepted; everything else,
 * 0 included, means "do not charge". A configuration that names a resistor
 * the chip cannot produce therefore switches the charger off rather than
 * silently picking a neighbouring one.
 */
inline Rv3028Trickle rv3028TrickleFromOhms(uint16_t ohms)
{
    switch (ohms)
    {
        case 3000:  return RV3028_TRICKLE_3K;
        case 5000:  return RV3028_TRICKLE_5K;
        case 9000:  return RV3028_TRICKLE_9K;
        case 15000: return RV3028_TRICKLE_15K;
        default:    return RV3028_TRICKLE_OFF;
    }
}

class Rv3028
{
public:
    /**
     * Probe the chip and configure backup switchover.
     *
     * The I2C bus must already be started by the caller.
     *
     * @param backup  switchover mode to activate
     * @param trickle trickle charger setting, RV3028_TRICKLE_OFF to disable
     * @return true if the chip answered on the bus
     */
    bool begin(Rv3028Backup backup = RV3028_BACKUP_LEVEL,
               Rv3028Trickle trickle = RV3028_TRICKLE_OFF);

    /** @return true if begin() found the chip */
    bool present() const { return _present; }

    /**
     * Whether the backup configuration read back exactly as written.
     *
     * A fitted supercap that is never charged and a switchover that never
     * happens both look like a working clock until the next power cut, so
     * the write is verified rather than assumed.
     */
    bool backupConfigured() const { return _backupOk; }

    /**
     * Test whether the stored time is trustworthy.
     *
     * Reads the power-on reset flag. It is set by the chip whenever the
     * supply - including backup - dropped below the operating threshold, and
     * is only cleared by writing the clock.
     *
     * @return true if the clock kept running since it was last set
     */
    bool timeValid();

    /**
     * Read the clock.
     *
     * @param utc receives the UTC epoch
     * @return true on success and if the time is valid
     */
    bool readUtc(uint32_t& utc);

    /**
     * Set the clock and clear the power-on reset flag.
     *
     * @param utc UTC epoch to store
     * @return true on success
     */
    bool writeUtc(uint32_t utc);

    /**
     * The two bytes of user RAM.
     *
     * Real RAM, not EEPROM: writing costs nothing and never wears out, and
     * the backup supply keeps it exactly as long as it keeps the clock. Two
     * bytes is all there is - the 43 bytes next to it are EEPROM with an
     * endurance of about 100000 cycles.
     *
     * Addressed one byte at a time so a user of the second byte is not
     * overwritten by a user of the first.
     *
     * @param index 0 or 1
     */
    bool readRam(uint8_t index, uint8_t& value);
    bool writeRam(uint8_t index, uint8_t value);

private:
    bool readRegs(uint8_t reg, uint8_t* buffer, uint8_t length);
    bool writeRegs(uint8_t reg, const uint8_t* buffer, uint8_t length);
    bool readReg(uint8_t reg, uint8_t& value);
    bool writeReg(uint8_t reg, uint8_t value);

    static uint8_t fromBcd(uint8_t bcd) { return (uint8_t)((bcd >> 4) * 10 + (bcd & 0x0F)); }
    static uint8_t toBcd(uint8_t value) { return (uint8_t)(((value / 10) << 4) | (value % 10)); }

    bool _present  = false;
    bool _backupOk = false;
};
