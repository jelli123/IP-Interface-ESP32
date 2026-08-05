/*
 *  web_server.cpp - Dashboard and REST API.
 */

#include <ESPAsyncWebServer.h>
#include <Update.h>
#include <WiFi.h>

#include "build_info.h"
#include "eth_interface.h"
#include "fw_hash.h"
#include "hw_config.h"
#include "interface_config.h"
#include "index_html.h"
#include "json_util.h"
#include "knx_link.h"
#include "net_manager.h"
#include "ota_service.h"
#include "time_service.h"
#include "web_server.h"

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

static String statusJson()
{
    const KnxLink::Stats& stats = knxLink.stats();

    String json = "{";
    json += "\"uptime\":\"" + uptimeString() + "\",";
    json += "\"iface\":\"" + String(netManager.activeInterface()) + "\",";
    json += "\"is_ap_mode\":" + String(netManager.isApMode() ? "true" : "false") + ",";
    json += "\"ssid\":\"" + jsonEscape(netManager.currentSsid()) + "\",";
    json += "\"ip\":\"" + netManager.currentIp() + "\",";
    json += "\"mac\":\"" + netManager.currentMac() + "\",";
    json += "\"wifi_connected\":" +
            String((netManager.isApMode() || netManager.isOnline()) ? "true" : "false") + ",";
    json += "\"rssi\":" +
            String((netManager.isApMode() || netManager.isEthernetMode()) ? 0 : WiFi.RSSI()) + ",";

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
    json += "\"prog_mode\":" + String(knxLink.progMode() ? "true" : "false") + ",";

    uint16_t pa = knxLink.individualAddress();
    json += "\"knx_pa\":\"" + String((pa >> 12) & 0x0F) + "." +
            String((pa >> 8) & 0x0F) + "." + String(pa & 0xFF) + "\",";
    json += "\"knx_max_tunnels\":" + String(KNX_TUNNELING) + ",";

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
    json += "\"ota_state\":\"" + String(OtaService::runningPartitionState()) + "\"";
    json += "},";

    json += "\"hardware\":{";
    json += "\"chip_model\":\"" + String(ESP.getChipModel()) + "\",";
    json += "\"chip_rev\":" + String(ESP.getChipRevision()) + ",";
    json += "\"cpu_freq\":" + String(ESP.getCpuFreqMHz()) + ",";
    json += "\"heap_total\":" + String(ESP.getHeapSize()) + ",";
    json += "\"heap_free\":" + String(ESP.getFreeHeap()) + ",";
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
            WiFi.scanNetworks(true);
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
        if (param("interval_min", value)) config.intervalMin = (uint16_t)value.toInt();
        if (param("ntp_enabled", value))  config.ntpEnabled = (value == "1" || value == "true");
        if (param("ntp_from_dhcp", value)) config.ntpFromDhcp = (value == "1" || value == "true");
        if (param("ntp_server", value))   strlcpy(config.ntpServer, value.c_str(), sizeof(config.ntpServer));
        if (param("tz", value))           strlcpy(config.tz, value.c_str(), sizeof(config.tz));

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
    server.on(
        "/api/hwconfig", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            // Answered from the body handler below; nothing to do when a
            // request arrives without one.
            if (!request->_tempObject)
            {
                if (!mutationAllowed(request)) return;
                request->send(400, "application/json", "{\"error\":\"empty body\"}");
            }
        },
        nullptr,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len,
           size_t index, size_t total) {
            if (!mutationAllowed(request)) return;

            if (total > 2048)
            {
                request->send(413, "application/json", "{\"error\":\"body too large\"}");
                return;
            }

            // Chunked upload: collect until the last piece arrives.
            if (index == 0)
            {
                request->_tempObject = new String();
                ((String*)request->_tempObject)->reserve(total + 1);
            }
            String* body = (String*)request->_tempObject;
            if (body == nullptr) return;

            for (size_t i = 0; i < len; i++)
            {
                *body += (char)data[i];
            }

            if (index + len < total)
            {
                return; // more to come
            }

            String error;
            bool   ok = hwConfig.applyJson(*body, error);

            delete body;
            request->_tempObject = nullptr;

            if (!ok)
            {
                request->send(400, "application/json",
                              String("{\"error\":\"") + jsonEscape(error) + "\"}");
                return;
            }

            request->send(200, "application/json",
                          "{\"status\":\"ok\",\"reboot_required\":true}");
        });

    server.on("/api/hwconfig/reset", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!mutationAllowed(request)) return;
        hwConfig.resetToDefaults();
        request->send(200, "application/json",
                      "{\"status\":\"ok\",\"reboot_required\":true}");
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

            bool   ok   = !Update.hasError() && !g_uploadHashFailed;
            String body = ok ? String("{\"status\":\"ok\"}")
                             : String("{\"error\":\"") +
                                   (g_uploadHashFailed ? "sha256 mismatch"
                                                       : Update.errorString()) + "\"}";

            AsyncWebServerResponse* response =
                request->beginResponse(ok ? 200 : 500, "application/json", body);
            response->addHeader("Connection", "close");
            request->send(response);

            if (ok)
            {
                Serial.println("OTA: upload accepted, rebooting shortly");
                netManager.scheduleReboot();
            }
        },
        [](AsyncWebServerRequest* request, String filename, size_t index,
           uint8_t* data, size_t len, bool final) {
            if (index == 0)
            {
                // This body handler runs before the response handler above, so
                // a gated request must never reach Update.begin() - otherwise
                // the image is already in flash by the time the 403 goes out.
                // Later chunks drain harmlessly, isRunning() stays false.
                if (!originAllowed(request) || netManager.isApMode())
                {
                    Serial.println("OTA: rejected (cross-origin or AP mode)");
                    return;
                }

                Serial.printf("OTA: upload start: %s\n", filename.c_str());
                g_uploadSha256 = "";
                g_uploadHashFailed = false;

                if (!Update.begin(UPDATE_SIZE_UNKNOWN))
                {
                    Update.printError(Serial);
                    return;
                }

                if (request->hasHeader("X-SHA256"))
                {
                    String sha = request->header("X-SHA256");
                    if (FwHash::isValidHex(sha))
                    {
                        g_uploadSha256 = sha;
                        g_uploadHash.begin();
                        Serial.printf("OTA: SHA-256 target %s\n", sha.c_str());
                    }
                    else
                    {
                        Serial.println("OTA: X-SHA256 ignored (bad format)");
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
                        Serial.printf("OTA: MD5 target %s (legacy)\n", md5.c_str());
                    }
                    else
                    {
                        Serial.println("OTA: X-MD5 ignored (bad format)");
                    }
                }
                else
                {
                    Serial.println("OTA: no checksum header - proceeding unverified");
                }
            }

            if (len && Update.isRunning() && !Update.hasError())
            {
                // Update.begin(UPDATE_SIZE_UNKNOWN) set the limit to the OTA
                // partition size. Reject an oversized image early with a clean
                // abort instead of a generic short-write error near the end.
                if ((index + len) > Update.size())
                {
                    Serial.printf("OTA: image exceeds partition (%u > %u) - abort\n",
                                  (unsigned)(index + len), (unsigned)Update.size());
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
                        Serial.printf("OTA: SHA-256 mismatch\n  expected %s\n  actual   %s\n",
                                      g_uploadSha256.c_str(), g_uploadHash.hex().c_str());
                        g_uploadHashFailed = true;
                        Update.abort();
                        return;
                    }
                    Serial.println("OTA: SHA-256 ok");
                }

                if (!Update.end(true))
                {
                    Update.printError(Serial);
                }
                else
                {
                    Serial.printf("OTA: %u bytes written\n", (unsigned)(index + len));
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

    registerCaptivePortalRoutes();
    registerWifiRoutes();
    registerKnxRoutes();
    registerTimeRoutes();
    registerHardwareRoutes();
    registerOtaRoutes();

    server.begin();
    Serial.println("Web server started");
}
