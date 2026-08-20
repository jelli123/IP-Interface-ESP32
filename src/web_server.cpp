/*
 *  web_server.cpp - Dashboard and REST API.
 */

#include <ESPAsyncWebServer.h>
#include <Update.h>
#include <WiFi.h>
#include <memory>
#include <new>
#include <utility>

#include "build_info.h"
#include "auth.h"
#include "cpu_load.h"
#include "eth_interface.h"
#include "fw_hash.h"
#include "hw_config.h"
#include "interface_config.h"
#include "index_html.h"
#include "json_util.h"
#include "knx_link.h"
#include "lpc_isp.h"
#include "net_manager.h"
#include "ota_service.h"
#include "status_led.h"
#include "time_service.h"
#include "web_server.h"

#include "log_buffer.h"
static AsyncWebServer server(80);

/*
 * State of the running manual upload.
 *
 * Only ever touched from the async_tcp task, and an upload is serialised by
 * the Update class itself, so no locking is needed.
 */
static FwHash g_uploadHash;
static String g_uploadSha256;
static bool   g_uploadHashFailed = false;
/* Reason the upload never got off the ground. Update.errorString() cannot
 * carry it: a refused begin() leaves the previous error in place. */
static String g_uploadError;

/* Same for an LPC image: the body handler has to remember why it bailed out
 * so the response handler can say so. */
static String g_lpcUploadError;

/* ------------------------------------------------------------------------- *
 * Request gating
 * ------------------------------------------------------------------------- */

/** Reduce a URL or Host header to its bare host name, lower case. */
static String hostOnly(String value)
{
    int at = value.indexOf("://");
    if (at >= 0) value = value.substring(at + 3);

    at = value.indexOf('/');
    if (at >= 0) value = value.substring(0, at);

    if (value.startsWith("[")) // bracketed IPv6 literal, keep the brackets
    {
        at = value.indexOf(']');
        if (at >= 0) value = value.substring(0, at + 1);
    }
    else
    {
        at = value.indexOf(':');
        if (at >= 0) value = value.substring(0, at);
    }

    value.toLowerCase();
    return value;
}

/*
 * Cross-origin check. A malicious page open in a browser on the same LAN can
 * fire form POSTs at our endpoints - the victim's browser is the confused
 * deputy. Browsers attach an Origin header to every cross-origin POST, our own
 * UI is same-origin, and non-browser clients send none at all.
 *
 * No Origin -> allow. Origin host equals Host header -> allow. Anything else,
 * including the literal "null", is rejected.
 */
static bool originAllowed(AsyncWebServerRequest* request)
{
    if (!request->hasHeader("Origin"))
    {
        return true;
    }
    return hostOnly(request->header("Origin")) == hostOnly(request->host());
}

/*
 * Gate for every state changing endpoint. Sends its own 403.
 *
 * The provisioning access point is open - anyone in radio range can join it.
 * While it is up the web surface is restricted to onboarding, so the only
 * routes that pass are the ones marked allowInApMode.
 */
static bool mutationAllowed(AsyncWebServerRequest* request, bool allowInApMode = false)
{
    if (!originAllowed(request))
    {
        request->send(403, "application/json",
                      "{\"error\":\"cross-origin request rejected\"}");
        return false;
    }
    if (netManager.isApMode() && !allowInApMode)
    {
        request->send(403, "application/json",
                      "{\"error\":\"disabled while the provisioning AP is active\"}");
        return false;
    }
    return true;
}

/*
 * Printable ASCII, nothing else.
 *
 * The values guarded by this reach places that trust them: the TZ string goes
 * to setenv()/tzset(), the NTP server name to the resolver. Neither has a use
 * for a control character, and a CR or LF in a value that is also written to
 * the log would let a caller forge log lines around it.
 */
static bool printableAscii(const String& value, size_t max)
{
    if (value.length() == 0 || value.length() > max)
    {
        return false;
    }
    for (size_t i = 0; i < value.length(); i++)
    {
        char c = value[i];
        if (c < 0x20 || c > 0x7E) return false;
    }
    return true;
}

/** An upload file name as it may appear in the log: no control characters. */
static String logSafe(const String& value)
{
    String out;
    size_t n = (value.length() > 64) ? 64 : value.length();
    out.reserve(n);

    for (size_t i = 0; i < n; i++)
    {
        char c = value[i];
        out += (c < 0x20 || c > 0x7E) ? '?' : c;
    }
    return out;
}

/* ------------------------------------------------------------------------- *
 * Status document
 * ------------------------------------------------------------------------- */

static String uptimeString()
{
    uint32_t seconds = millis() / 1000;
    char     buffer[48];
    snprintf(buffer, sizeof(buffer), "%lud %02dh %02dm %02ds",
             (unsigned long)(seconds / 86400),
             (int)((seconds % 86400) / 3600),
             (int)((seconds % 3600) / 60),
             (int)(seconds % 60));
    return String(buffer);
}

/*
 * The parts of the status document that are expensive to produce.
 *
 * The tunnel addresses come out of the KNX interface objects and the radio
 * level out of the WiFi driver; neither changes between two polls, but both
 * cost far more than the rest of the document put together. The dashboard
 * asks every two seconds, so they are refreshed on their own schedule.
 */
static String tunnelPaJson()
{
    static String   cached;
    static uint32_t cachedAt = 0;

    if (cached.length() && (uint32_t)(millis() - cachedAt) < 5000)
    {
        return cached;
    }

    uint16_t tunnelPa[KNX_TUNNELING];
    uint8_t  count = knxLink.tunnelAddresses(tunnelPa, KNX_TUNNELING);

    String json = "[";
    for (uint8_t i = 0; i < count; i++)
    {
        if (i) json += ",";
        json += "\"" + String((tunnelPa[i] >> 12) & 0x0F) + "." +
                String((tunnelPa[i] >> 8) & 0x0F) + "." +
                String(tunnelPa[i] & 0xFF) + "\"";
    }
    json += "]";

    cached   = json;
    cachedAt = millis();
    return cached;
}

static int32_t cachedRssi()
{
    static int32_t  cached   = 0;
    static uint32_t cachedAt = 0;

    if (netManager.isApMode() || netManager.isEthernetMode())
    {
        return 0;
    }
    if (cachedAt != 0 && (uint32_t)(millis() - cachedAt) < 5000)
    {
        return cached;
    }

    cached   = WiFi.RSSI();
    cachedAt = millis();
    return cached;
}

static String statusJson()
{
    const KnxLink::Stats& stats = knxLink.stats();

    String json = "{";
    json += "\"uptime\":\"" + uptimeString() + "\",";
    json += "\"device_name\":\"" + jsonEscape(netManager.deviceName()) + "\",";
    json += "\"knx_name\":\"" + jsonEscape(knxLink.friendlyName()) + "\",";
    json += "\"auth_user\":\"" + jsonEscape(Auth::user()) + "\",";
    json += "\"auth_possible\":" + String(Auth::recoveryPossible() ? "true" : "false") + ",";
    json += "\"iface\":\"" + String(netManager.activeInterface()) + "\",";
    json += "\"is_ap_mode\":" + String(netManager.isApMode() ? "true" : "false") + ",";
    json += "\"ssid\":\"" + jsonEscape(netManager.currentSsid()) + "\",";
    json += "\"ip\":\"" + netManager.currentIp() + "\",";
    json += "\"netmask\":\"" + netManager.currentNetmask() + "\",";
    json += "\"gateway\":\"" + netManager.currentGateway() + "\",";
    json += "\"dns\":\"" + netManager.currentDns() + "\",";
    json += "\"ip_from_ets\":" +
            String(netManager.addressFromEts() ? "true" : "false") + ",";
    json += "\"mac\":\"" + netManager.currentMac() + "\",";
    json += "\"wifi_connected\":" +
            String((netManager.isApMode() || netManager.isOnline()) ? "true" : "false") + ",";
    json += "\"rssi\":" + String(cachedRssi()) + ",";

    // Wired interface. Always reported so the dashboard can show that a
    // W5500 was looked for and not found.
    json += "\"eth\":{";
    json += "\"state\":\"" + String(ethInterface.stateName()) + "\",";
    json += "\"present\":" + String(ethInterface.chipPresent() ? "true" : "false") + ",";
    json += "\"active\":" + String(ethInterface.active() ? "true" : "false") + ",";
    json += "\"ip\":\"" + ethInterface.ipString() + "\",";
    json += "\"mac\":\"" + ethInterface.macString() + "\",";
    json += "\"speed\":" + String(ethInterface.linkSpeed()) + ",";
    json += "\"duplex\":\"" + String(ethInterface.fullDuplex() ? "full" : "half") + "\"";
    json += "},";

    json += "\"knx_configured\":" + String(knxLink.configured() ? "true" : "false") + ",";
    json += "\"knx_route_all\":" + String(knxLink.routeUnfiltered() ? "true" : "false") + ",";
    json += "\"led_present\":" + String(statusLed.present() ? "true" : "false") + ",";
    json += "\"led_beat_available\":" + String(statusLed.hasHeartbeat() ? "true" : "false") + ",";
    json += "\"led_heartbeat\":" + String(statusLed.heartbeat() ? "true" : "false") + ",";
    json += "\"led_brightness\":" + String(statusLed.brightness()) + ",";
    json += "\"wifi_enabled\":" + String(netManager.wifiEnabled() ? "true" : "false") + ",";
    json += "\"prog_mode\":" + String(knxLink.progMode() ? "true" : "false") + ",";

    uint16_t pa = knxLink.individualAddress();
    json += "\"knx_pa\":\"" + String((pa >> 12) & 0x0F) + "." +
            String((pa >> 8) & 0x0F) + "." + String(pa & 0xFF) + "\",";
    json += "\"knx_max_tunnels\":" + String(KNX_TUNNELING) + ",";

    // The addresses clients appear under when they talk through us. Worth
    // showing: they are what ETS assigns, and there is no other way to see
    // what actually ended up in the device.
    json += "\"knx_tunnel_pa\":" + tunnelPaJson() + ",";

    // The TP1 side. No NCN512x rail or crystal telemetry here: the Selfbus
    // SB-Interface emulates a plain TP-UART 2 and has no such registers.
    json += "\"tp\":{";
    json += "\"type\":\"Selfbus SB-Interface (TP-UART 2 emulation)\",";
    json += "\"connected\":" + String(knxLink.tpConnected() ? "true" : "false") + ",";
    json += "\"baud\":" + String(SBIP_KNX_BAUDRATE) + ",";
    json += "\"rx_frames\":" + String(stats.tpRxFrames) + ",";
    json += "\"rx_ignored\":" + String(stats.tpRxIgnored) + ",";
    json += "\"rx_invalid\":" + String(stats.tpRxInvalid) + ",";
    json += "\"tx_frames\":" + String(stats.tpTxFrames) + ",";
    json += "\"tx_processed\":" + String(stats.tpTxProcessed) + ",";
    json += "\"bus_load\":" + String(stats.busLoadPermille) + ",";
    json += "\"bus_load_peak\":" + String(stats.busLoadPeak) + ",";
    json += "\"bus_load_peak_age\":" + String(knxLink.busPeakAge()) + ",";
    json += "\"isp\":" + String(lpcIsp.available() ? "true" : "false") + ",";
    json += "\"self_test\":\"" + jsonEscape(String(knxLink.selfTestResult())) + "\"";
    json += "},";

    json += "\"ip_stats\":{";
    json += "\"rx_frames\":" + String(stats.ipRxFrames) + ",";
    json += "\"tx_frames\":" + String(stats.ipTxFrames);
    json += "},";

    json += "\"build\":{";
    json += "\"version\":\"" FIRMWARE_VERSION "\",";
    json += "\"number\":" + String(BUILD_NUMBER) + ",";
    json += "\"git\":\"" BUILD_GIT "\",";
    json += "\"partition\":\"" + String(OtaService::runningPartition()) + "\",";
    json += "\"ota_state\":\"" + String(OtaService::runningPartitionState()) + "\",";
    json += "\"partitions\":" + OtaService::partitionsJson();
    json += "},";

    json += "\"hardware\":{";
    json += "\"chip_model\":\"" + String(ESP.getChipModel()) + "\",";
    json += "\"chip_rev\":" + String(ESP.getChipRevision()) + ",";
    json += "\"cpu_freq\":" + String(ESP.getCpuFreqMHz()) + ",";
    json += "\"cpu_cores\":" + String(cpuLoad.cores()) + ",";

    json += "\"cpu_load\":[";
    for (uint8_t c = 0; c < cpuLoad.cores(); c++)
    {
        if (c) json += ",";
        json += String(cpuLoad.permille(c));
    }
    json += "],\"cpu_peak\":[";
    for (uint8_t c = 0; c < cpuLoad.cores(); c++)
    {
        if (c) json += ",";
        json += String(cpuLoad.peak(c));
    }
    json += "],";
    json += "\"loop_load\":" + String(cpuLoad.loopPermille()) + ",";
    json += "\"loop_peak\":" + String(cpuLoad.loopPeak()) + ",";
    json += "\"loop_max_us\":" + String(cpuLoad.loopMaxUs()) + ",";
    json += "\"peak_age\":" + String(cpuLoad.peakAge()) + ",";
    json += "\"heap_total\":" + String(ESP.getHeapSize()) + ",";
    json += "\"heap_free\":" + String(ESP.getFreeHeap()) + ",";
    json += "\"flash_size\":" + String(ESP.getFlashChipSize()) + ",";
    json += "\"sketch_size\":" + String(OtaService::sketchSize()) + ",";
    json += "\"part_size\":" + String(OtaService::partitionSize()) + ",";
    json += "\"psram_total\":" + String(ESP.getPsramSize()) + ",";
    json += "\"psram_free\":" + String(ESP.getFreePsram()) + ",";
    json += "\"log_size\":" + String(sysLog.capacity()) + ",";
    json += "\"log_used\":" + String(sysLog.used()) + ",";
    json += "\"log_psram\":" + String(sysLog.inPsram() ? "true" : "false") + ",";
    json += "\"log_rtc\":" + String(sysLog.keepAcrossReset() ? "true" : "false") + ",";
    json += "\"log_rtc_used\":" + String(sysLog.rtcUsed()) + ",";
    json += "\"profile_default\":" + String(hwConfig.usingDefaults() ? "true" : "false") + ",";
    json += "\"profile_fallback\":\"" + String(hwConfig.fallbackReason()) + "\",";
    json += "\"reboot_pending\":" + String(hwConfig.rebootPending() ? "true" : "false");
    json += "}";

    json += "}";
    return json;
}

/* ------------------------------------------------------------------------- *
 * Routes
 * ------------------------------------------------------------------------- */

static void registerCaptivePortalRoutes()
{
    // Probe URLs of the common operating systems. Redirecting them makes the
    // sign-in sheet pop up automatically once a phone joins the AP.
    auto redirectToRoot = [](AsyncWebServerRequest* request) {
        request->redirect(String("http://") + WiFi.softAPIP().toString() + "/");
    };

    server.on("/generate_204", HTTP_GET, redirectToRoot);
    server.on("/gen_204", HTTP_GET, redirectToRoot);
    server.on("/hotspot-detect.html", HTTP_GET, redirectToRoot);
    server.on("/canonical.html", HTTP_GET, redirectToRoot);
    server.on("/connecttest.txt", HTTP_GET, redirectToRoot);
    server.on("/ncsi.txt", HTTP_GET, redirectToRoot);

    server.onNotFound([](AsyncWebServerRequest* request) {
        if (netManager.isApMode())
        {
            request->redirect(String("http://") + WiFi.softAPIP().toString() + "/");
        }
        else
        {
            request->send(404, "text/plain", "Not found");
        }
    });
}

static void registerWifiRoutes()
{
    /*
     * Asynchronous scan. WiFi.scanNetworks(true) returns immediately and the
     * results are collected later. A blocking scan here would freeze the whole
     * async_tcp task - every HTTP and TCP callback - for several seconds.
     *
     * GET ?start=1 kicks off a fresh scan, a plain GET polls it.
     */
    server.on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest* request) {
        int n = WiFi.scanComplete();

        if (n == WIFI_SCAN_RUNNING)
        {
            request->send(200, "application/json", "{\"scanning\":true}");
            return;
        }
        if (request->hasParam("start") || n == WIFI_SCAN_FAILED)
        {
            WiFi.scanDelete();

            /*
             * The provisioning AP runs as WIFI_AP, without a station half -
             * that combination crashed the device on startup. scanNetworks()
             * does call WiFi.enableSTA(true) on its own, but the station then
             * comes up with modem sleep enabled, and a sleeping station finds
             * nothing. Switch deliberately and disable power save first.
             */
            if (WiFi.getMode() == WIFI_AP)
            {
                WiFi.mode(WIFI_AP_STA);
                WiFi.setSleep(WIFI_PS_NONE);
            }

            // A failed start used to be reported as "scanning", which left the
            // page polling forever instead of showing an error.
            if (WiFi.scanNetworks(true) == WIFI_SCAN_FAILED)
            {
                sysLog.println("WiFi: scan could not be started");
                request->send(500, "application/json",
                              "{\"error\":\"scan could not be started\"}");
                return;
            }

            request->send(200, "application/json", "{\"scanning\":true}");
            return;
        }

        String json = "[";
        for (int i = 0; i < n; i++)
        {
            if (i > 0) json += ",";
            json += "{\"ssid\":\"" + jsonEscape(WiFi.SSID(i)) + "\",";
            json += "\"rssi\":" + String(WiFi.RSSI(i)) + "}";
        }
        json += "]";

        WiFi.scanDelete();
        request->send(200, "application/json", json);
    });

    server.on("/api/wifi/connect", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!mutationAllowed(request, true /* onboarding route */)) return;

        if (!request->hasParam("ssid", true))
        {
            request->send(400, "application/json", "{\"error\":\"missing ssid\"}");
            return;
        }

        String ssid = request->getParam("ssid", true)->value();
        String pass = request->hasParam("password", true)
                          ? request->getParam("password", true)->value()
                          : String("");

        request->send(200, "application/json", "{\"status\":\"ok\"}");
        netManager.applyCredentials(ssid, pass);
    });

    server.on("/api/wifi/ap_mode", HTTP_POST, [](AsyncWebServerRequest* request) {
        // Locked in AP mode as well: pointless there, and in a fallback AP an
        // RF neighbour could otherwise wipe the stored credentials.
        if (!mutationAllowed(request)) return;

        request->send(200, "application/json", "{\"status\":\"ok\"}");
        netManager.forgetCredentials();
    });

    server.on("/api/name", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!mutationAllowed(request)) return;

        if (!request->hasParam("name", true))
        {
            request->send(400, "application/json", "{\"error\":\"missing name\"}");
            return;
        }

        if (!netManager.setDeviceName(request->getParam("name", true)->value()))
        {
            request->send(400, "application/json",
                          "{\"error\":\"1 to 31 characters from A-Z a-z 0-9 -, "
                          "not starting or ending with a hyphen\"}");
            return;
        }

        request->send(200, "application/json",
                      "{\"status\":\"ok\",\"reboot_required\":true}");
    });
}

static void registerKnxRoutes()
{
    // Accepts ?state=on|off|toggle, default toggle. Returns the requested
    // state; the UI re-syncs to the real one via /api/status.
    server.on("/api/progmode", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!mutationAllowed(request)) return;

        String state = "toggle";
        if (request->hasParam("state", true))
        {
            state = request->getParam("state", true)->value();
        }
        else if (request->hasParam("state"))
        {
            state = request->getParam("state")->value();
        }

        bool newState;
        if (state == "on")       newState = true;
        else if (state == "off") newState = false;
        else                     newState = !knxLink.progMode();

        // Never touch the KNX stack from the async_tcp task - that races
        // knx.loop(). requestProgMode() defers the write to the main task.
        knxLink.requestProgMode(newState);

        request->send(200, "application/json",
                      String("{\"prog_mode\":") + (newState ? "true" : "false") + "}");
    });

    /*
     * Factory reset of the KNX side only.
     *
     * Needed because the stack derives the tunnel addresses from the device
     * address exactly once and then keeps them. Programming the device later
     * leaves its tunnels behind in the old line, and ETS refuses to download
     * with "more than one device in programming mode".
     *
     * WiFi credentials and the hardware profile are in separate NVS
     * namespaces and survive.
     */
    server.on("/api/knx/reset", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!mutationAllowed(request)) return;

        if (!knxLink.resetConfiguration())
        {
            request->send(500, "application/json",
                          "{\"error\":\"could not clear the KNX configuration\"}");
            return;
        }

        request->send(200, "application/json",
                      "{\"status\":\"ok\",\"reboot\":true}");

        // The stack keeps its tables in RAM, so only a restart makes the wipe
        // effective.
        netManager.scheduleReboot();
    });

    /*
     * Forward group telegrams without a downloaded filter table. Takes effect
     * immediately, no restart needed - the flag is read per telegram.
     */
    server.on("/api/knx/routing", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!mutationAllowed(request)) return;

        if (!request->hasParam("unfiltered", true))
        {
            request->send(400, "application/json",
                          "{\"error\":\"parameter 'unfiltered' missing\"}");
            return;
        }

        bool enable = request->getParam("unfiltered", true)->value() == "1";
        knxLink.routeUnfiltered(enable);
        sysLog.printf("KNX: unfiltered routing %s\n", enable ? "on" : "off");

        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    /*
     * The group addresses the filter table lets through. Capped because the
     * response is built in RAM and a full table can hold thousands.
     */
    server.on("/api/knx/filter", HTTP_GET, [](AsyncWebServerRequest* request) {
        static const uint16_t LIMIT = 400;

        uint16_t* addresses = new (std::nothrow) uint16_t[LIMIT];

        if (addresses == nullptr)
        {
            request->send(500, "application/json", "{\"error\":\"out of memory\"}");
            return;
        }

        uint16_t total  = 0;
        bool     loaded = knxLink.filterTable(addresses, LIMIT, total);
        uint16_t shown  = (total < LIMIT) ? total : LIMIT;

        String json = "{\"loaded\":";
        json += loaded ? "true" : "false";
        json += ",\"total\":" + String(total);
        json += ",\"addresses\":[";

        for (uint16_t i = 0; i < shown; i++)
        {
            if (i) json += ",";
            json += "\"" + String((addresses[i] >> 11) & 0x1F) + "/" +
                    String((addresses[i] >> 8) & 0x07) + "/" +
                    String(addresses[i] & 0xFF) + "\"";
        }

        json += "]}";
        delete[] addresses;

        request->send(200, "application/json", json);
    });
}

/* ------------------------------------------------------------------------- *
 * Time server
 * ------------------------------------------------------------------------- */

/** Parse a group address given either as "1/2/3" or as a plain number. */
static uint16_t parseGroupAddress(const String& text)
{
    String value = text;
    value.trim();
    if (value.length() == 0)
    {
        return 0;
    }

    int firstSlash = value.indexOf('/');
    if (firstSlash < 0)
    {
        return (uint16_t)value.toInt();
    }

    int secondSlash = value.indexOf('/', firstSlash + 1);
    if (secondSlash < 0)
    {
        // two level notation: main/sub
        uint16_t main = (uint16_t)value.substring(0, firstSlash).toInt();
        uint16_t sub  = (uint16_t)value.substring(firstSlash + 1).toInt();
        return (uint16_t)(((main & 0x1F) << 11) | (sub & 0x07FF));
    }

    uint16_t main   = (uint16_t)value.substring(0, firstSlash).toInt();
    uint16_t middle = (uint16_t)value.substring(firstSlash + 1, secondSlash).toInt();
    uint16_t sub    = (uint16_t)value.substring(secondSlash + 1).toInt();
    return (uint16_t)(((main & 0x1F) << 11) | ((middle & 0x07) << 8) | (sub & 0xFF));
}

/** Render a group address as "1/2/3", or an empty string for 0. */
static String formatGroupAddress(uint16_t address)
{
    if (address == 0)
    {
        return String("");
    }
    return String((address >> 11) & 0x1F) + "/" +
           String((address >> 8) & 0x07) + "/" +
           String(address & 0xFF);
}

static String timeJson()
{
    const TimeService::Config& config = timeService.config();

    String json = "{";
    json += "\"enabled\":" + String(config.enabled ? "true" : "false") + ",";
    json += "\"ga_datetime\":\"" + formatGroupAddress(config.gaDateTime) + "\",";
    json += "\"ga_time\":\"" + formatGroupAddress(config.gaTime) + "\",";
    json += "\"ga_date\":\"" + formatGroupAddress(config.gaDate) + "\",";
    json += "\"interval_min\":" + String(config.intervalMin) + ",";
    json += "\"ntp_enabled\":" + String(config.ntpEnabled ? "true" : "false") + ",";
    json += "\"ntp_from_dhcp\":" + String(config.ntpFromDhcp ? "true" : "false") + ",";
    json += "\"ntp_dhcp_active\":" + String(timeService.ntpFromDhcpActive() ? "true" : "false") + ",";
    json += "\"ntp_server\":\"" + jsonEscape(String(config.ntpServer)) + "\",";
    json += "\"ntp_active\":\"" + jsonEscape(timeService.activeNtpServer()) + "\",";
    json += "\"tz\":\"" + jsonEscape(String(config.tz)) + "\",";
    json += "\"source\":\"" + String(timeService.sourceName()) + "\",";
    json += "\"since_sync_s\":" + String(timeService.secondsSinceSync()) + ",";
    json += "\"rtc_present\":" + String(timeService.rtcPresent() ? "true" : "false") + ",";
    json += "\"clock_valid\":" + String(TimeService::clockValid() ? "true" : "false") + ",";
    json += "\"local_time\":\"" + timeService.localTimeString() + "\",";
    json += "\"next_send_s\":" + String(timeService.secondsToNextSend());
    json += "}";
    return json;
}

static void registerTimeRoutes()
{
    server.on("/api/time", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "application/json", timeJson());
    });

    // Full configuration write. Missing fields keep their current value so the
    // UI can send partial forms.
    server.on("/api/time/config", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!mutationAllowed(request)) return;

        TimeService::Config config = timeService.config();

        auto param = [request](const char* name, String& out) -> bool {
            if (request->hasParam(name, true))
            {
                out = request->getParam(name, true)->value();
                return true;
            }
            return false;
        };

        String value;
        if (param("enabled", value))      config.enabled = (value == "1" || value == "true");
        if (param("ga_datetime", value))  config.gaDateTime = parseGroupAddress(value);
        if (param("ga_time", value))      config.gaTime = parseGroupAddress(value);
        if (param("ga_date", value))      config.gaDate = parseGroupAddress(value);
        if (param("ntp_enabled", value))  config.ntpEnabled = (value == "1" || value == "true");
        if (param("ntp_from_dhcp", value)) config.ntpFromDhcp = (value == "1" || value == "true");

        if (param("interval_min", value))
        {
            long minutes = value.toInt();
            if (minutes < 1)     minutes = 1;
            if (minutes > 10080) minutes = 10080; // a week
            config.intervalMin = (uint16_t)minutes;
        }

        if (param("ntp_server", value))
        {
            if (!printableAscii(value, sizeof(config.ntpServer) - 1))
            {
                request->send(400, "application/json", "{\"error\":\"bad ntp_server\"}");
                return;
            }
            strlcpy(config.ntpServer, value.c_str(), sizeof(config.ntpServer));
        }

        if (param("tz", value))
        {
            if (!printableAscii(value, sizeof(config.tz) - 1))
            {
                request->send(400, "application/json", "{\"error\":\"bad tz\"}");
                return;
            }
            strlcpy(config.tz, value.c_str(), sizeof(config.tz));
        }

        timeService.applyConfig(config);
        request->send(200, "application/json", timeJson());
    });

    // Set the clock. "epoch" carries UTC seconds - the browser sends
    // Date.now()/1000, a manual entry is converted client side.
    server.on("/api/time/set", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!mutationAllowed(request)) return;

        if (!request->hasParam("epoch", true))
        {
            request->send(400, "application/json", "{\"error\":\"missing epoch\"}");
            return;
        }

        uint32_t epoch = (uint32_t)strtoul(request->getParam("epoch", true)->value().c_str(),
                                           nullptr, 10);
        if (!timeService.requestSetUtc(epoch))
        {
            request->send(400, "application/json", "{\"error\":\"implausible timestamp\"}");
            return;
        }

        request->send(202, "application/json", "{\"status\":\"ok\"}");
    });

    // Send the configured telegrams immediately.
    server.on("/api/time/send", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!mutationAllowed(request)) return;

        if (!TimeService::clockValid())
        {
            request->send(409, "application/json", "{\"error\":\"clock not set\"}");
            return;
        }

        timeService.requestSend();
        request->send(202, "application/json", "{\"status\":\"ok\"}");
    });
}

/* ------------------------------------------------------------------------- *
 * Hardware profile
 * ------------------------------------------------------------------------- */

static void registerHardwareRoutes()
{
    server.on("/api/hwconfig", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "application/json", hwConfig.toJson());
    });

    /*
     * Replace the stored profile. Body is the raw JSON document, not a form
     * field, so the same file can be uploaded that /api/hwconfig hands out.
     *
     * Applied on the next boot only - pins cannot be moved while the
     * peripherals using them are running.
     */
    static const size_t HWCONFIG_MAX_BODY = 6144;

    server.on(
        "/api/hwconfig", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            // Every reply is written here. The body handler below runs first
            // and must stay silent: whatever it sent would be replaced by the
            // response from this handler, which runs unconditionally once the
            // request is complete.
            if (!mutationAllowed(request)) return;

            if (request->contentLength() > HWCONFIG_MAX_BODY)
            {
                request->send(413, "application/json", "{\"error\":\"body too large\"}");
                return;
            }

            String* body = (String*)request->_tempObject;
            if (body == nullptr || body->length() == 0)
            {
                // Also covers a body the collector dropped for exceeding the
                // cap, which would otherwise look like an empty patch and
                // store the running profile again.
                delete body;
                request->_tempObject = nullptr;
                request->send(400, "application/json", "{\"error\":\"empty body\"}");
                return;
            }

            String error;
            bool   ok = hwConfig.applyJson(*body, error);

            // The request destructor frees _tempObject with free(), which
            // would leave the String's own buffer behind.
            delete body;
            request->_tempObject = nullptr;

            if (!ok)
            {
                request->send(400, "application/json",
                              String("{\"error\":\"") + jsonEscape(error) + "\"}");
                return;
            }

            request->send(200, "application/json",
                          String("{\"status\":\"ok\",\"reboot_required\":") +
                              (hwConfig.rebootPending() ? "true" : "false") + "}");
        },
        nullptr,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len,
           size_t index, size_t total) {
            // Collect only. Gate here as well so a rejected request never has
            // a buffer allocated for it.
            if (!originAllowed(request) || netManager.isApMode()) return;
            if (total > HWCONFIG_MAX_BODY) return;

            if (index == 0)
            {
                request->_tempObject = new String();
                ((String*)request->_tempObject)->reserve(total + 1);
            }

            String* body = (String*)request->_tempObject;
            if (body == nullptr) return;

            // total is what the client announced. A chunked request announces
            // nothing, so the cap above can be passed with a Content-Length
            // of zero - the accumulated length is the one that has to hold.
            if (body->length() + len > HWCONFIG_MAX_BODY)
            {
                body->clear();
                return;
            }

            for (size_t i = 0; i < len; i++)
            {
                *body += (char)data[i];
            }
        });

    server.on("/api/hwconfig/reset", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!mutationAllowed(request)) return;
        hwConfig.resetToDefaults();
        request->send(200, "application/json",
                      "{\"status\":\"ok\",\"reboot_required\":true}");
    });

    /*
     * Accepts ?scope=bus|cpu|all, default all. A peak only says something
     * next to the period it covers, so every bar carries its own age on the
     * dashboard - which is what lets the two be cleared apart.
     */
    server.on("/api/peaks/reset", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!mutationAllowed(request)) return;

        String scope = "all";
        if (request->hasParam("scope", true))
        {
            scope = request->getParam("scope", true)->value();
        }
        else if (request->hasParam("scope"))
        {
            scope = request->getParam("scope")->value();
        }

        if (scope != "cpu") knxLink.resetPeak();
        if (scope != "bus") cpuLoad.resetPeaks();

        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!mutationAllowed(request)) return;
        request->send(200, "application/json", "{\"status\":\"ok\"}");
        netManager.scheduleReboot();
    });
}

static void registerOtaRoutes()
{
    /*
     * Manual firmware upload, multipart field "firmware".
     *
     * An optional X-SHA256 header (64 hex chars) is verified before the boot
     * partition is switched, so a corrupt or truncated upload cannot brick the
     * device. X-MD5 is still accepted for tooling that only produces MD5, but
     * SHA-256 takes precedence when both are present.
     */
    server.on(
        "/api/ota", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            // Gate again: the body handler already refused to start Update
            // for a rejected request, so without this we would answer with a
            // cheerful 200.
            if (!mutationAllowed(request)) return;

            bool ok = g_uploadError.isEmpty() && !g_uploadHashFailed &&
                      !Update.hasError();

            String reason = g_uploadHashFailed ? String("sha256 mismatch")
                            : !g_uploadError.isEmpty() ? g_uploadError
                                                       : String(Update.errorString());

            String body = ok ? String("{\"status\":\"ok\"}")
                             : String("{\"error\":\"") + reason + "\"}";

            AsyncWebServerResponse* response =
                request->beginResponse(ok ? 200 : 500, "application/json", body);
            response->addHeader("Connection", "close");
            request->send(response);

            if (ok)
            {
                sysLog.println("OTA: upload accepted, rebooting shortly");
                netManager.scheduleReboot();
            }
        },
        [](AsyncWebServerRequest* request, String filename, size_t index,
           uint8_t* data, size_t len, bool final) {
            if (index == 0)
            {
                g_uploadSha256 = "";
                g_uploadHashFailed = false;
                g_uploadError = "";

                // This body handler runs before the response handler above, so
                // a gated request must never reach Update.begin() - otherwise
                // the image is already in flash by the time the 403 goes out.
                // Later chunks drain harmlessly, isRunning() stays false.
                if (!originAllowed(request) || netManager.isApMode())
                {
                    sysLog.println("OTA: rejected (cross-origin or AP mode)");
                    g_uploadError = "not allowed in AP mode";
                    return;
                }

                sysLog.printf("OTA: upload start: %s\n", logSafe(filename).c_str());

                // A browser that walks away mid-upload leaves Update running:
                // the final chunk never arrives, so end() is never reached.
                // begin() then bails out with "already running" without
                // clearing anything, while isRunning() keeps saying true - so
                // the next upload writes on top of the abandoned one and dies
                // with "Not Enough Space" once the stale progress plus the new
                // image exceed the partition. Every attempt after that fails
                // the same way until the device is rebooted.
                if (Update.isRunning())
                {
                    sysLog.println("OTA: discarding an abandoned upload");
                    Update.abort();
                }

                if (!Update.begin(UPDATE_SIZE_UNKNOWN))
                {
                    g_uploadError = Update.errorString();
                    Update.printError(Serial);
                    return;
                }

                sysLog.printf("OTA: target partition holds %u bytes\n",
                              (unsigned)Update.size());

                if (request->hasHeader("X-SHA256"))
                {
                    String sha = request->header("X-SHA256");
                    if (FwHash::isValidHex(sha))
                    {
                        g_uploadSha256 = sha;
                        g_uploadHash.begin();
                        sysLog.printf("OTA: SHA-256 target %s\n", sha.c_str());
                    }
                    else
                    {
                        sysLog.println("OTA: X-SHA256 ignored (bad format)");
                    }
                }
                else if (request->hasHeader("X-MD5"))
                {
                    // Legacy path. MD5 is no longer collision resistant, so it
                    // only guards against transport corruption here - which is
                    // all Update.h ever used it for.
                    String md5 = request->header("X-MD5");
                    md5.trim();
                    md5.toLowerCase();
                    if (md5.length() == 32 && Update.setMD5(md5.c_str()))
                    {
                        sysLog.printf("OTA: MD5 target %s (legacy)\n", md5.c_str());
                    }
                    else
                    {
                        sysLog.println("OTA: X-MD5 ignored (bad format)");
                    }
                }
                else
                {
                    sysLog.println("OTA: no checksum header - proceeding unverified");
                }
            }

            if (len && Update.isRunning() && !Update.hasError())
            {
                // Update.begin(UPDATE_SIZE_UNKNOWN) set the limit to the OTA
                // partition size. Compare against what Update itself has left
                // rather than against the upload offset - the two drift apart
                // because progress() only counts flushed sectors.
                if (len > Update.remaining())
                {
                    sysLog.printf("OTA: image exceeds the %u byte partition - abort\n",
                                  (unsigned)Update.size());
                    g_uploadError = "image larger than the OTA partition";
                    Update.abort();
                }
                else if (Update.write(data, len) != len)
                {
                    Update.printError(Serial);
                }
                else if (g_uploadSha256.length() > 0)
                {
                    g_uploadHash.update(data, len);
                }
            }

            if (final && Update.isRunning())
            {
                // Verify before end(): only that call switches the boot
                // partition, so a mismatch here is completely harmless.
                if (g_uploadSha256.length() > 0)
                {
                    g_uploadHash.finish();
                    if (!g_uploadHash.matches(g_uploadSha256))
                    {
                        sysLog.printf("OTA: SHA-256 mismatch\n  expected %s\n  actual   %s\n",
                                      g_uploadSha256.c_str(), g_uploadHash.hex().c_str());
                        g_uploadHashFailed = true;
                        Update.abort();
                        return;
                    }
                    sysLog.println("OTA: SHA-256 ok");
                }

                if (!Update.end(true))
                {
                    Update.printError(Serial);
                }
                else
                {
                    sysLog.printf("OTA: %u bytes written\n", (unsigned)(index + len));
                }
            }
        });

    server.on("/api/update/check", HTTP_GET, [](AsyncWebServerRequest* request) {
        otaService.startCheck();
        request->send(200, "application/json", otaService.statusJson());
    });

    server.on("/api/update/install", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!mutationAllowed(request)) return;
        bool ok = otaService.startInstall();
        request->send(ok ? 202 : 409, "application/json", otaService.statusJson());
    });

    server.on("/api/update/status", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "application/json", otaService.statusJson());
    });

    /*
     * Boot from the other slot next time. Not an upload target choice - an
     * upload always lands in the slot that is not executing.
     */
    server.on("/api/ota/switch", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!mutationAllowed(request)) return;

        if (!OtaService::switchPartition())
        {
            request->send(409, "application/json",
                          "{\"error\":\"the other slot holds no valid firmware\"}");
            return;
        }

        request->send(200, "application/json",
                      "{\"status\":\"ok\",\"reboot\":true}");
        netManager.scheduleReboot();
    });

    /* Heartbeat on the addressable status LED. Persisted, so it sticks. */
    server.on("/api/led/heartbeat", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!mutationAllowed(request)) return;

        if (!statusLed.hasHeartbeat())
        {
            request->send(409, "application/json",
                          "{\"error\":\"no LED is assigned to the heartbeat\"}");
            return;
        }

        bool enable = true;

        if (request->hasParam("enabled", true))
        {
            String v = request->getParam("enabled", true)->value();
            enable = (v == "1" || v == "true");
        }

        statusLed.heartbeat(enable);

        request->send(200, "application/json",
                      String("{\"status\":\"ok\",\"heartbeat\":") +
                          (enable ? "true" : "false") + "}");
    });

    /* Common brightness for every LED. */
    server.on("/api/led/brightness", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!mutationAllowed(request)) return;

        if (!request->hasParam("percent", true))
        {
            request->send(400, "application/json",
                          "{\"error\":\"parameter 'percent' missing\"}");
            return;
        }

        long value = request->getParam("percent", true)->value().toInt();

        if (value < 1 || value > 100)
        {
            request->send(400, "application/json",
                          "{\"error\":\"percent out of range (1..100)\"}");
            return;
        }

        statusLed.brightness((uint8_t)value);

        request->send(200, "application/json",
                      String("{\"status\":\"ok\",\"brightness\":") + String(value) + "}");
    });
}

/* ------------------------------------------------------------------------- *
 * Programming the SB-Interface
 *
 * Every job takes the KNX UART away from the stack for a few seconds, so all
 * of these only kick one off and answer straight away. The dashboard polls
 * /api/lpc for the outcome.
 * ------------------------------------------------------------------------- */

static void registerLpcRoutes()
{
    server.on("/api/lpc", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "application/json", lpcIsp.statusJson());
    });

    auto startJob = [](AsyncWebServerRequest* request, bool flash, bool run) {
        if (!mutationAllowed(request)) return;

        String error;
        bool   ok = run     ? lpcIsp.startRun(error)
                    : flash ? lpcIsp.startFlash(error)
                            : lpcIsp.startProbe(error);

        if (!ok)
        {
            request->send(409, "application/json",
                          String("{\"error\":\"") + jsonEscape(error) + "\"}");
            return;
        }
        request->send(202, "application/json", lpcIsp.statusJson());
    };

    server.on("/api/lpc/probe", HTTP_POST, [startJob](AsyncWebServerRequest* request) {
        startJob(request, false, false);
    });

    server.on("/api/lpc/run", HTTP_POST, [startJob](AsyncWebServerRequest* request) {
        startJob(request, false, true);
    });

    server.on("/api/lpc/write", HTTP_POST, [startJob](AsyncWebServerRequest* request) {
        startJob(request, true, false);
    });

    /*
     * Upload of the image, multipart field "firmware". Intel Hex or raw
     * binary; nothing is written to the LPC here, the file only lands in the
     * staging buffer so /api/lpc/write can be a separate, deliberate step.
     */
    server.on(
        "/api/lpc/upload", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            if (!mutationAllowed(request)) return;

            String error;
            bool   ok = g_lpcUploadError.isEmpty() && lpcIsp.uploadEnd(error);

            if (!ok)
            {
                if (!g_lpcUploadError.isEmpty()) error = g_lpcUploadError;
                lpcIsp.uploadDiscard();
                request->send(400, "application/json",
                              String("{\"error\":\"") + jsonEscape(error) + "\"}");
                return;
            }
            request->send(200, "application/json", lpcIsp.statusJson());
        },
        [](AsyncWebServerRequest* request, String filename, size_t index,
           uint8_t* data, size_t len, bool final) {
            if (index == 0)
            {
                g_lpcUploadError = "";

                // The body handler runs ahead of the response handler, so the
                // gate has to be repeated here - later chunks then drain into
                // nothing because the buffer was never armed.
                if (!originAllowed(request) || netManager.isApMode())
                {
                    g_lpcUploadError = "not allowed in AP mode";
                    return;
                }

                String error;
                if (!lpcIsp.uploadBegin(error))
                {
                    g_lpcUploadError = error;
                    return;
                }
                sysLog.printf("LPC: upload start: %s\n", logSafe(filename).c_str());
            }

            if (len && g_lpcUploadError.isEmpty())
            {
                if (!lpcIsp.uploadData(data, len))
                {
                    g_lpcUploadError = "the file was rejected";
                }
            }

            (void)final;
        });
}

void webServerBegin()
{
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        // Stream straight out of PROGMEM. The const char* overload would copy
        // the whole page into a heap String and re-substring it per ACK, which
        // truncates the page once the heap is fragmented.
        request->send(200, "text/html", (const uint8_t*)index_html,
                      sizeof(index_html) - 1);
    });

    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "application/json", statusJson());
    });

    // Fixed at flash time, so it has no business in a document the dashboard
    // fetches every two seconds.
    server.on("/api/partitions", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "application/json", OtaService::partitionTableJson());
    });

    /*
     * A slice of the captured log, sent in pieces straight out of the ring.
     *
     * Without "from" it answers with the newest "bytes". With it, the slice
     * starts at that absolute position, which is what lets the dashboard
     * scroll through half a megabyte without holding all of it.
     */
    server.on("/api/log", HTTP_GET, [](AsyncWebServerRequest* request) {
        size_t want = 8192;

        if (request->hasParam("bytes"))
        {
            long value = request->getParam("bytes")->value().toInt();
            if (value > 0) want = (size_t)value;
        }

        // More than the ring holds cannot exist, and asking for it would keep
        // the chunked response alive over an empty buffer.
        if (want > sysLog.capacity()) want = sysLog.capacity();
        if (want == 0) want = 1;

        uint64_t from = (sysLog.written() > want) ? sysLog.written() - want : 0;

        if (request->hasParam("from"))
        {
            from = strtoull(request->getParam("from")->value().c_str(), nullptr, 10);
        }
        if (from < sysLog.oldest())
        {
            from = sysLog.oldest();
        }

        auto cursor = std::make_shared<std::pair<uint64_t, size_t>>(from, want);

        AsyncWebServerResponse* response = request->beginChunkedResponse(
            "text/plain", [cursor](uint8_t* buffer, size_t maxLen, size_t) -> size_t {
                if (cursor->second == 0) return 0;
                size_t chunk = (cursor->second < maxLen) ? cursor->second : maxLen;
                size_t n = sysLog.copyFrom(cursor->first, (char*)buffer, chunk);
                cursor->second -= n;
                return n;
            });

        // The client positions its window from these.
        response->addHeader("Cache-Control", "no-store");
        response->addHeader("X-Log-From", String((unsigned long)from));
        response->addHeader("X-Log-Oldest", String((unsigned long)sysLog.oldest()));
        response->addHeader("X-Log-Written", String((unsigned long)sysLog.written()));

        if (request->hasParam("download"))
        {
            response->addHeader("Content-Disposition",
                                "attachment; filename=\"sbip-log.txt\"");
        }

        request->send(response);
    });

    server.on("/api/log/clear", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!mutationAllowed(request)) return;
        sysLog.clear();
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    /* What survived the last reset. Small enough to send in one piece. */
    server.on("/api/log/reset", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/plain", sysLog.rtcTail());
    });

    server.on("/api/log/keep", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!mutationAllowed(request)) return;

        bool enable = true;
        if (request->hasParam("enabled", true))
        {
            String value = request->getParam("enabled", true)->value();
            enable = (value == "1" || value == "true");
        }

        sysLog.keepAcrossReset(enable);
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    server.on("/api/auth", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!mutationAllowed(request)) return;

        String user = request->hasParam("user", true)
                          ? request->getParam("user", true)->value()
                          : String();
        String pass = request->hasParam("password", true)
                          ? request->getParam("password", true)->value()
                          : String();
        String error;

        if (!Auth::set(user, pass, error))
        {
            request->send(400, "application/json",
                          String("{\"error\":\"") + jsonEscape(error) + "\"}");
            return;
        }

        request->send(200, "application/json",
                      "{\"status\":\"ok\",\"reboot_required\":true}");
    });

    registerCaptivePortalRoutes();
    registerWifiRoutes();
    registerKnxRoutes();
    registerTimeRoutes();
    registerHardwareRoutes();
    registerOtaRoutes();
    registerLpcRoutes();

    // After the routes: the middleware wraps whatever is registered.
    Auth::attach(server);

    server.begin();
    sysLog.println("Web server started");
}
