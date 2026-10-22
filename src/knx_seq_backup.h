/*
 *  knx_seq_backup.h - The last valid sequence numbers of the senders the
 *  Security Proxy decrypts, kept where writing them costs nothing.
 *
 *  They belong to the security object, which reaches NVS only with an ETS
 *  download and, for the proxy, at most every five minutes - flash wears
 *  out. Without more, a restart would fall back to the last save, and a
 *  telegram recorded in the minutes before could be played back once.
 *
 *  So every accepted number is also written here, at once:
 *
 *    FRAM     on the I2C bus at 0x50, if the hardware profile says one is
 *             fitted, or if it answers the Device ID query as a Fujitsu
 *             MB85RC of 64 kbit or more. Survives everything; the five
 *             minute NVS save is dropped then. Never an EEPROM at the same
 *             address: it would be written with every telegram - that is
 *             why probing insists on the Device ID.
 *    RTC RAM  otherwise. Survives a restart, a watchdog or a panic, but not
 *             a power cut; after one it is discarded.
 *
 *  One slot per entry of the security IA table (PID 54, 32 entries), each
 *  with the address it belongs to: if ETS reorders the table, the restore
 *  still finds the right sender.
 */
#pragma once

#include <Arduino.h>
#include <stdint.h>

class KnxSeqBackup
{
public:
    enum Medium : uint8_t
    {
        NONE,
        RTC_RAM,
        FRAM,
    };

    /** Entries of the stack's security IA table. */
    static const uint8_t SLOTS = 32;

    /** Pick the medium. Before the proxy restores anything. */
    void begin();

    void store(uint8_t slot, uint16_t address, uint64_t seq);
    bool load(uint8_t slot, uint16_t& address, uint64_t& seq);

    /** Forget every slot, with the keys they belong to. */
    void clear();

    Medium      medium() const { return _medium; }
    const char* mediumName() const;

private:
    bool framProbe();
    bool read(uint16_t offset, uint8_t* data, uint8_t length);
    bool write(uint16_t offset, const uint8_t* data, uint8_t length);
    bool headerValid();

    Medium _medium = NONE;
};

extern KnxSeqBackup knxSeqBackup;
