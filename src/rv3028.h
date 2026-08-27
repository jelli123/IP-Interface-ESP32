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
 */
enum Rv3028Backup : uint8_t
{
    RV3028_BACKUP_DISABLED = 0, //!< no switchover, RTC dies with VDD
    RV3028_BACKUP_DIRECT   = 1, //!< direct switching, for a battery
    RV3028_BACKUP_LEVEL    = 2  //!< level switching, for a supercap
};

/** Trickle charger series resistance. Only meaningful for a supercap. */
enum Rv3028Trickle : uint8_t
{
    RV3028_TRICKLE_OFF = 0xFF,
    RV3028_TRICKLE_3K  = 0,
    RV3028_TRICKLE_5K  = 1,
    RV3028_TRICKLE_9K  = 2,
    RV3028_TRICKLE_15K = 3
};

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

    bool _present = false;
};
