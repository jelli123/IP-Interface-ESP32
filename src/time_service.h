/*
 *  time_service.h - KNX time server.
 *
 *  Keeps the system clock fed from NTP, an optional RV-3028-C7 RTC or a
 *  manual entry, and publishes it on the bus as DPT 19.001, 10.001 and
 *  11.001.
 */
#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <time.h>

#include "rv3028.h"

class TimeService
{
public:
    /** Where the currently held time came from. */
    enum Source : uint8_t
    {
        SRC_NONE = 0, //!< no usable time
        SRC_NTP,      //!< synchronised from an NTP server
        SRC_RTC,      //!< restored from the RTC at boot
        SRC_MANUAL,   //!< entered by hand or taken from the browser
        SRC_CARRIED   //!< still running from before a software reset
    };

    struct Config
    {
        bool     enabled     = false; //!< send telegrams at all
        uint16_t gaDateTime  = 0;     //!< DPT 19.001, 0 = do not send
        uint16_t gaTime      = 0;     //!< DPT 10.001, 0 = do not send
        uint16_t gaDate      = 0;     //!< DPT 11.001, 0 = do not send
        uint16_t intervalMin = 60;    //!< send interval in minutes
        bool     ntpEnabled  = true;
        bool     ntpFromDhcp = true;  //!< prefer the server offered by DHCP
        char     ntpServer[64] = "pool.ntp.org";
        char     tz[48]        = "CET-1CEST,M3.5.0,M10.5.0/3"; //!< POSIX TZ
    };

    /**
     * Put the stored timezone into place, nothing else.
     *
     * Split out of begin() because every log line is stamped with
     * localtime_r(): until this has run the stamps are UTC, and the moment
     * begin() catches up the whole log jumps by the offset. Needs NVS, so it
     * belongs directly after hwConfig.begin() - but before anything logs.
     */
    void applyTimezone();

    /** Load the configuration, start I2C/RTC and apply the timezone. */
    void begin();

    /** Drive NTP tracking, RTC refresh and the send schedule. */
    void loop();

    const Config& config() const { return _config; }

    /**
     * Store a new configuration and apply it.
     *
     * Safe to call from the web server task: only NVS, setenv() and the SNTP
     * client are touched, never the KNX stack or the I2C bus.
     */
    void applyConfig(const Config& config);

    /**
     * Request the clock to be set by hand or from the browser.
     *
     * Queued and applied in loop(), because setting the time also writes the
     * RTC over I2C and triggers a KNX transmission - neither belongs in the
     * web server task.
     *
     * @param utc UTC epoch
     * @return true if the value was plausible and queued
     */
    bool requestSetUtc(uint32_t utc);

    /** Ask for the configured telegrams to be sent on the next loop(). */
    void requestSend() { _sendPending = true; }

    Source      source() const { return _source; }
    const char* sourceName() const;
    bool        rtcPresent() const { return _rtc.present(); }

    /** Name of the NTP server actually in use, empty if none. */
    String activeNtpServer() const;

    /** @return true if the active server came from DHCP rather than config */
    bool ntpFromDhcpActive() const { return _dhcpServerActive; }

    /** Seconds since the system clock was last corrected from a real source. */
    uint32_t secondsSinceSync() const;

    /** @return true if the system clock holds a plausible date */
    static bool clockValid();

    /** Local time as "YYYY-MM-DD HH:MM:SS", or "-" if the clock is unset. */
    String localTimeString() const;

    /** Seconds until the next scheduled transmission, 0 if not scheduled. */
    uint32_t secondsToNextSend() const;

private:
    void loadConfig();
    void storeConfig();
    void startNtp();
    void trackNtp();
    void probeRtc();
    void refreshRtcFromSystem();
    void refreshSystemFromRtc();
    bool sendNow();
    bool applyUtc(uint32_t utc);

    static uint8_t encodeDateTime(const struct tm& t, bool synced, uint8_t* out);
    static uint8_t encodeTime(const struct tm& t, uint8_t* out);
    static uint8_t encodeDate(const struct tm& t, uint8_t* out);

    Config   _config;
    Rv3028   _rtc;
    Source   _source        = SRC_NONE;
    bool     _ntpEverSynced = false;
    bool     _dhcpServerActive = false;

    uint32_t _lastSendMs     = 0;
    uint32_t _lastRtcWriteMs = 0;
    uint32_t _lastRtcReadMs  = 0;
    uint32_t _lastRtcProbeMs = 0;
    uint32_t _lastNtpCheckMs = 0;
    uint32_t _lastSyncMs     = 0;

    volatile bool     _sendPending    = false;
    volatile uint32_t _pendingSetUtc  = 0; //!< 0 = nothing queued
};

extern TimeService timeService;
