/*
 *  time_service.cpp - KNX time server.
 */

#include <Preferences.h>
#include <Wire.h>
#include <esp_sntp.h>
#include <sys/time.h>

#include "hw_config.h"
#include "interface_config.h"
#include "knx_link.h"
#include "time_service.h"

TimeService timeService;

/** Anything before 2024-01-01 means the clock was never set. */
static const uint32_t CLOCK_VALID_THRESHOLD = 1704067200UL;

/** How often the RTC is refreshed from a synchronised system clock. */
static const uint32_t RTC_WRITE_INTERVAL_MS = 3600000UL; // 1 h

/**
 * How often the system clock is pulled back from the RTC.
 *
 * The ESP32 keeps system time from APB_CLK, specified at +/-10 ppm - about
 * 26 s per month. The RV-3028-C7 is a temperature compensated +/-1 ppm part,
 * roughly 30 s per year. Without NTP the RTC is therefore by far the better
 * reference and the system clock has to be pulled back to it regularly.
 */
static const uint32_t RTC_READ_INTERVAL_MS = 900000UL; // 15 min

/**
 * Retry interval for the RTC probe.
 *
 * Lets a module fitted after power-up be picked up without a reboot; a failed
 * probe on an empty bus costs one I2C address cycle.
 */
static const uint32_t RTC_PROBE_INTERVAL_MS = 30000UL;

/** How often the NTP state is re-evaluated. */
static const uint32_t NTP_CHECK_INTERVAL_MS = 10000UL;

static Preferences prefs;

/* ------------------------------------------------------------------------- *
 * Configuration
 * ------------------------------------------------------------------------- */

void TimeService::loadConfig()
{
    prefs.begin("timesrv", false);
    _config.enabled     = prefs.getBool("en", _config.enabled);
    _config.gaDateTime  = prefs.getUShort("gadt", _config.gaDateTime);
    _config.gaTime      = prefs.getUShort("gat", _config.gaTime);
    _config.gaDate      = prefs.getUShort("gad", _config.gaDate);
    _config.intervalMin = prefs.getUShort("ivl", _config.intervalMin);
    _config.ntpEnabled  = prefs.getBool("ntpen", _config.ntpEnabled);
    _config.ntpFromDhcp = prefs.getBool("ntpdhcp", _config.ntpFromDhcp);

    String server = prefs.getString("ntpsrv", _config.ntpServer);
    strlcpy(_config.ntpServer, server.c_str(), sizeof(_config.ntpServer));

    String tz = prefs.getString("tz", _config.tz);
    strlcpy(_config.tz, tz.c_str(), sizeof(_config.tz));

    prefs.end();

    if (_config.intervalMin == 0)
    {
        _config.intervalMin = 60;
    }
}

void TimeService::storeConfig()
{
    prefs.begin("timesrv", false);
    prefs.putBool("en", _config.enabled);
    prefs.putUShort("gadt", _config.gaDateTime);
    prefs.putUShort("gat", _config.gaTime);
    prefs.putUShort("gad", _config.gaDate);
    prefs.putUShort("ivl", _config.intervalMin);
    prefs.putBool("ntpen", _config.ntpEnabled);
    prefs.putBool("ntpdhcp", _config.ntpFromDhcp);
    prefs.putString("ntpsrv", _config.ntpServer);
    prefs.putString("tz", _config.tz);
    prefs.end();
}

void TimeService::applyConfig(const Config& config)
{
    _config = config;
    if (_config.intervalMin == 0)
    {
        _config.intervalMin = 60;
    }
    storeConfig();

    setenv("TZ", _config.tz, 1);
    tzset();

    startNtp();

    // Publish immediately so a changed group address is visible at once.
    _sendPending = true;
}

/* ------------------------------------------------------------------------- *
 * Startup
 * ------------------------------------------------------------------------- */

void TimeService::begin()
{
    loadConfig();

    setenv("TZ", _config.tz, 1);
    tzset();

    const HwProfile& hw = hwConfig.active();
    if (hw.i2cEnabled)
    {
        Wire.begin(hw.i2cSdaPin, hw.i2cSclPin);
        probeRtc();

        if (_rtc.present())
        {
            refreshSystemFromRtc();
        }
    }

    startNtp();
}

/*
 * Look for the RTC. Repeated from loop() while none was found, so a module
 * fitted later is picked up without a reboot.
 */
void TimeService::probeRtc()
{
    _lastRtcProbeMs = millis();

    if (_rtc.present())
    {
        return;
    }

    if (_rtc.begin(RV3028_BACKUP_LEVEL, RV3028_TRICKLE_OFF))
    {
        Serial.println("RTC: RV-3028-C7 found");
    }
}

/*
 * Pull the system clock back to the RTC.
 *
 * Only steps the clock when the two differ by more than a second: writing
 * settimeofday() on every pass would fight the SNTP client and produce
 * needless log noise.
 */
void TimeService::refreshSystemFromRtc()
{
    _lastRtcReadMs = millis();

    uint32_t rtcUtc;
    if (!_rtc.readUtc(rtcUtc) || rtcUtc < CLOCK_VALID_THRESHOLD)
    {
        return;
    }

    uint32_t systemUtc = (uint32_t)time(nullptr);
    int32_t  drift     = (int32_t)(rtcUtc - systemUtc);

    if (drift > -2 && drift < 2)
    {
        _lastSyncMs = millis();
        return;
    }

    struct timeval tv = { (time_t)rtcUtc, 0 };
    settimeofday(&tv, nullptr);
    _lastSyncMs = millis();

    if (_source == SRC_NONE)
    {
        _source = SRC_RTC;
        Serial.printf("RTC: system clock restored, %s\n", localTimeString().c_str());
    }
    else
    {
        Serial.printf("RTC: system clock corrected by %ld s\n", (long)drift);
    }
}

/*
 * Start or restart the SNTP client.
 *
 * Order matters: the DHCP server mode has to be selected while SNTP is
 * stopped, otherwise lwIP keeps the previous server list.
 *
 * DHCP option 42 only reaches us when the lwIP build has
 * CONFIG_LWIP_DHCP_GET_NTP_SRV enabled. The stock Arduino-ESP32 libraries
 * ship with it OFF, so sntp_servermode_dhcp() is a no-op there. That is why
 * the configured server is always registered as a fallback rather than being
 * skipped when DHCP mode is requested - see README.
 */
void TimeService::startNtp()
{
    esp_sntp_stop();
    _dhcpServerActive = false;

    if (!_config.ntpEnabled)
    {
        return;
    }

    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);

#if LWIP_DHCP_GET_NTP_SRV
    esp_sntp_servermode_dhcp(_config.ntpFromDhcp);
    _dhcpServerActive = _config.ntpFromDhcp;
#else
    if (_config.ntpFromDhcp)
    {
        Serial.println("NTP: DHCP option 42 unavailable in this lwIP build, "
                       "using the configured server");
    }
#endif

    // Slot 0 is the one DHCP overwrites when the feature is compiled in, so
    // the configured server goes to slot 1 and stays reachable either way.
    if (strlen(_config.ntpServer) > 0)
    {
        esp_sntp_setservername(_dhcpServerActive ? 1 : 0, _config.ntpServer);
    }

    esp_sntp_init();
}

String TimeService::activeNtpServer() const
{
    if (!_config.ntpEnabled)
    {
        return String("");
    }

    for (uint8_t i = 0; i < 2; i++)
    {
        const char* name = esp_sntp_getservername(i);
        if (name != nullptr && strlen(name) > 0)
        {
            return String(name);
        }

        const ip_addr_t* addr = esp_sntp_getserver(i);
        if (addr != nullptr && !ip_addr_isany(addr))
        {
            return String(ipaddr_ntoa(addr));
        }
    }
    return String("");
}

uint32_t TimeService::secondsSinceSync() const
{
    if (_lastSyncMs == 0)
    {
        return 0;
    }
    return (millis() - _lastSyncMs) / 1000;
}

/* ------------------------------------------------------------------------- *
 * Time sources
 * ------------------------------------------------------------------------- */

bool TimeService::clockValid()
{
    return (uint32_t)time(nullptr) >= CLOCK_VALID_THRESHOLD;
}

/*
 * NTP state tracking.
 *
 * sntp_get_sync_status() reports SNTP_SYNC_STATUS_COMPLETED once after each
 * successful update, then resets itself. Polling it is therefore an exact
 * indicator of a real sync - no guessing from "the clock looks plausible".
 */
void TimeService::trackNtp()
{
    if (!_config.ntpEnabled)
    {
        return;
    }
    if ((uint32_t)(millis() - _lastNtpCheckMs) < NTP_CHECK_INTERVAL_MS)
    {
        return;
    }
    _lastNtpCheckMs = millis();

    if (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED)
    {
        return;
    }

    _lastSyncMs = millis();
    _source     = SRC_NTP;

    if (!_ntpEverSynced)
    {
        _ntpEverSynced = true;
        Serial.printf("NTP: synchronised from %s, %s\n",
                      activeNtpServer().c_str(), localTimeString().c_str());
        _sendPending = true;
    }

    // Carry the fresh time straight into the RTC - that is the whole point of
    // having one when the network later goes away.
    refreshRtcFromSystem();
}

void TimeService::refreshRtcFromSystem()
{
    if (!_rtc.present() || !clockValid())
    {
        return;
    }
    if (_rtc.writeUtc((uint32_t)time(nullptr)))
    {
        _lastRtcWriteMs = millis();
    }
}

bool TimeService::requestSetUtc(uint32_t utc)
{
    if (utc < CLOCK_VALID_THRESHOLD)
    {
        return false;
    }
    _pendingSetUtc = utc;
    return true;
}

bool TimeService::applyUtc(uint32_t utc)
{
    struct timeval tv = { (time_t)utc, 0 };
    if (settimeofday(&tv, nullptr) != 0)
    {
        return false;
    }

    // A manual entry outranks a stale NTP state but not a live one: if NTP is
    // reachable it will step the clock back on its next poll anyway.
    _source     = _ntpEverSynced ? SRC_NTP : SRC_MANUAL;
    _lastSyncMs = millis();

    refreshRtcFromSystem();
    _sendPending = true;

    Serial.printf("Time set manually: %s\n", localTimeString().c_str());
    return true;
}

const char* TimeService::sourceName() const
{
    switch (_source)
    {
    case SRC_NTP:    return "ntp";
    case SRC_RTC:    return "rtc";
    case SRC_MANUAL: return "manual";
    default:         return "none";
    }
}

String TimeService::localTimeString() const
{
    if (!clockValid())
    {
        return String("-");
    }

    time_t    now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);

    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &t);
    return String(buffer);
}

uint32_t TimeService::secondsToNextSend() const
{
    if (!_config.enabled || !clockValid())
    {
        return 0;
    }

    uint32_t intervalMs = (uint32_t)_config.intervalMin * 60000UL;
    uint32_t elapsed    = millis() - _lastSendMs;
    return (elapsed >= intervalMs) ? 0 : ((intervalMs - elapsed) / 1000);
}

/* ------------------------------------------------------------------------- *
 * DPT encoding
 * ------------------------------------------------------------------------- */

/** KNX counts Monday as 1 and Sunday as 7; struct tm counts Sunday as 0. */
static uint8_t knxWeekday(const struct tm& t)
{
    return (uint8_t)((t.tm_wday == 0) ? 7 : t.tm_wday);
}

/** DPT 19.001 DateTime, 8 octets. See KNX Spec 3/7/2. */
uint8_t TimeService::encodeDateTime(const struct tm& t, bool synced, uint8_t* out)
{
    out[0] = (uint8_t)t.tm_year;                  // tm_year is already year - 1900
    out[1] = (uint8_t)(t.tm_mon + 1);
    out[2] = (uint8_t)t.tm_mday;
    out[3] = (uint8_t)((knxWeekday(t) << 5) | (t.tm_hour & 0x1F));
    out[4] = (uint8_t)(t.tm_min & 0x3F);
    out[5] = (uint8_t)(t.tm_sec & 0x3F);

    // Octet 6 flags. Every "no ..." bit stays 0 because we supply all fields.
    // Only the working day pair and the summer time bit are set here.
    //   b7 F, b6 WD, b5 NWD, b4 NY, b3 ND, b2 NDoW, b1 NT, b0 SUTI
    out[6] = 0x20;                                // NWD: working day field unused
    if (t.tm_isdst > 0)
    {
        out[6] |= 0x01;                           // SUTI: summer time active
    }

    // Octet 7, b7 CLQ: set when the clock has an external synchronisation
    // signal. Some devices ignore telegrams with CLQ cleared, so this must
    // reflect reality rather than being set unconditionally.
    out[7] = synced ? 0x80 : 0x00;

    return 8;
}

/** DPT 10.001 TimeOfDay, 3 octets. */
uint8_t TimeService::encodeTime(const struct tm& t, uint8_t* out)
{
    out[0] = (uint8_t)((knxWeekday(t) << 5) | (t.tm_hour & 0x1F));
    out[1] = (uint8_t)(t.tm_min & 0x3F);
    out[2] = (uint8_t)(t.tm_sec & 0x3F);
    return 3;
}

/** DPT 11.001 Date, 3 octets. Year 0..89 means 2000..2089. */
uint8_t TimeService::encodeDate(const struct tm& t, uint8_t* out)
{
    int year = t.tm_year + 1900;
    out[0] = (uint8_t)(t.tm_mday & 0x1F);
    out[1] = (uint8_t)((t.tm_mon + 1) & 0x0F);
    out[2] = (uint8_t)((year >= 2000) ? (year - 2000) : (year - 1900));
    return 3;
}

/* ------------------------------------------------------------------------- *
 * Sending
 * ------------------------------------------------------------------------- */

bool TimeService::sendNow()
{
    if (!clockValid())
    {
        return false;
    }

    time_t    now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);

    bool    synced  = (_source == SRC_NTP);
    uint8_t payload[8];
    bool    sent    = false;

    if (_config.gaDateTime != 0)
    {
        uint8_t length = encodeDateTime(t, synced, payload);
        sent |= knxLink.sendGroupValue(_config.gaDateTime, payload, length);
    }

    if (_config.gaTime != 0)
    {
        uint8_t length = encodeTime(t, payload);
        sent |= knxLink.sendGroupValue(_config.gaTime, payload, length);
    }

    if (_config.gaDate != 0)
    {
        uint8_t length = encodeDate(t, payload);
        sent |= knxLink.sendGroupValue(_config.gaDate, payload, length);
    }

    if (sent)
    {
        _lastSendMs = millis();
    }
    return sent;
}

void TimeService::loop()
{
    // Apply a web-requested clock change here, in the main task.
    if (_pendingSetUtc != 0)
    {
        uint32_t utc = _pendingSetUtc;
        _pendingSetUtc = 0;
        applyUtc(utc);
    }

    trackNtp();

    if (hwConfig.active().i2cEnabled)
    {
        // Keep looking for the RTC so one fitted later is picked up.
        if (!_rtc.present() &&
            (uint32_t)(millis() - _lastRtcProbeMs) >= RTC_PROBE_INTERVAL_MS)
        {
            probeRtc();
            if (_rtc.present())
            {
                refreshSystemFromRtc();
            }
        }

        if (_rtc.present())
        {
            // Without a live NTP sync the RTC is the better reference: pull
            // the drifting system clock back to it.
            if (_source != SRC_NTP &&
                (uint32_t)(millis() - _lastRtcReadMs) >= RTC_READ_INTERVAL_MS)
            {
                refreshSystemFromRtc();
            }

            // With NTP alive the RTC is the one being kept up to date, so
            // that it holds a good value once the network disappears.
            if (_source == SRC_NTP &&
                (uint32_t)(millis() - _lastRtcWriteMs) >= RTC_WRITE_INTERVAL_MS)
            {
                refreshRtcFromSystem();
            }
        }
    }

    if (!_config.enabled)
    {
        return;
    }

    if (_sendPending)
    {
        _sendPending = false;
        sendNow();
        return;
    }

    uint32_t intervalMs = (uint32_t)_config.intervalMin * 60000UL;
    if ((uint32_t)(millis() - _lastSendMs) >= intervalMs)
    {
        sendNow();
    }
}
