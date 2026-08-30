/*
 *  ota_service.cpp - Firmware update, both manual upload and online pull.
 */

#include <HTTPClient.h>
#include <Preferences.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>

#include "build_info.h"
#include "fw_hash.h"
#include "hw_config.h"
#include "interface_config.h"
#include "json_util.h"
#include "ota_service.h"

#include "log_buffer.h"
OtaService otaService;

// Key under which this build looks up its image in the update manifest.
// Must be distinct per chip - an image built for one target will not run on
// another, and the digest check would only catch that after the download.
#if defined(CONFIG_IDF_TARGET_ESP32C3)
#define UPDATE_CHIP_KEY "ESP32-C3"
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
#define UPDATE_CHIP_KEY "ESP32-C6"
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
#define UPDATE_CHIP_KEY "ESP32-S2"
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#define UPDATE_CHIP_KEY "ESP32-S3"
#else
#define UPDATE_CHIP_KEY "ESP32"
#endif

/*
 * Shared between the async_tcp task (status handler) and the worker tasks.
 * The scalars are 32 bit aligned, so torn reads are impossible; the worst
 * case is a stale value that self-corrects on the next poll.
 *
 * The error message is a fixed char array on purpose. A String here would be
 * a use-after-free: a realloc in the writer frees the buffer the reader is
 * walking.
 */
namespace
{
volatile OtaService::State g_state    = OtaService::IDLE;
volatile size_t            g_progress = 0;
volatile size_t            g_total    = 0;
char                       g_error[96] = {0};
String                     g_latest;
String                     g_url;
String                     g_sha256;
}

void OtaService::setError(const String& message)
{
    strlcpy(g_error, message.c_str(), sizeof(g_error));
}

const char* OtaService::stateName(State state)
{
    switch (state)
    {
    case IDLE:       return "idle";
    case CHECKING:   return "checking";
    case AVAILABLE:  return "available";
    case INSTALLING: return "installing";
    case DONE:       return "done";
    case FAILED:     return "error";
    }
    return "?";
}

/** Numeric MAJOR.MINOR.PATCH comparison. Returns > 0 when a is newer. */
int OtaService::compareVersions(const String& a, const String& b)
{
    int aMajor = 0, aMinor = 0, aPatch = 0;
    int bMajor = 0, bMinor = 0, bPatch = 0;
    sscanf(a.c_str(), "%d.%d.%d", &aMajor, &aMinor, &aPatch);
    sscanf(b.c_str(), "%d.%d.%d", &bMajor, &bMinor, &bPatch);

    if (aMajor != bMajor) return aMajor - bMajor;
    if (aMinor != bMinor) return aMinor - bMinor;
    return aPatch - bPatch;
}

static uint32_t s_sketchSize = 0;

String OtaService::manifestUrl()
{
    return String(hwConfig.active().updateUrl);
}

uint32_t OtaService::sketchSize()
{
    return s_sketchSize;
}

/** True once, on the first boot after switchPartition() set the note. */
static bool takeSwitchFlag();

void OtaService::loop()
{
    if (!_slotRecorded)
    {
        _slotRecorded = true;
        recordOwnSlot();

        // Measured here, in the main task and exactly once, rather than from
        // a web handler. See the note on sketchSize().
        s_sketchSize = ESP.getSketchSize();

        // A slot the user picked on purpose has run here before, so there is
        // nothing to prove and every reason to hurry: while the image sits in
        // PENDING_VERIFY any reset makes the bootloader mark it INVALID, and
        // an INVALID slot can never be selected again.
        if (takeSwitchFlag())
        {
            _validationPending = false;
            markAppValid("after a deliberate slot switch");
        }
    }

    if (!_validationPending || millis() < OTA_VALIDATE_AFTER_MS)
    {
        return;
    }
    _validationPending = false;

    markAppValid("after the proving time");
}

/* Only ever acts when the bootloader is actually waiting for the verdict. */
void OtaService::markAppValid(const char* why)
{
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t   state;

    if (running != nullptr && esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY)
    {
        esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        sysLog.printf("OTA: %s app valid %s\n",
                      err == ESP_OK ? "marked" : "FAILED to mark", why);
    }
}

bool OtaService::startCheck()
{
    if (manifestUrl().length() == 0)
    {
        setError("online update not configured");
        g_state = FAILED;
        return false;
    }
    if (g_state == CHECKING || g_state == INSTALLING)
    {
        return false;
    }

    g_state = CHECKING;
    setError("");

    // The manifest fetch blocks for seconds. Running it inside the request
    // handler would stall the async_tcp task, which serves every HTTP and TCP
    // callback - an unauthenticated denial of service.
    if (xTaskCreate(checkTask, "ota_check", 8192, nullptr, 1, nullptr) != pdPASS)
    {
        setError("could not spawn check task");
        g_state = FAILED;
        return false;
    }
    return true;
}

void OtaService::checkTask(void* arg)
{
    (void)arg;

    WiFiClientSecure client;
    // The manifest carries a SHA-256 for the image, which is the integrity
    // check that matters here. Pinning a CA would need firmware updates
    // whenever the issuer rotates - the exact thing we are trying to enable.
    client.setInsecure();

    HTTPClient https;
    String     manifest = manifestUrl();

    if (!https.begin(client, manifest))
    {
        setError("HTTPS begin failed");
        g_state = FAILED;
        vTaskDelete(nullptr);
        return;
    }

    int code = https.GET();
    if (code != HTTP_CODE_OK)
    {
        setError("HTTP " + String(code));
        g_state = FAILED;
        https.end();
        vTaskDelete(nullptr);
        return;
    }

    String body = https.getString();
    https.end();

    String latest = jsonGetString(body, "version");
    if (latest.length() == 0)
    {
        setError("no version in manifest");
        g_state = FAILED;
        vTaskDelete(nullptr);
        return;
    }
    g_latest = latest;

    String path   = jsonGetNestedString(body, "ota", UPDATE_CHIP_KEY, "path");
    String sha256 = jsonGetNestedString(body, "ota", UPDATE_CHIP_KEY, "sha256");

    if (path.length() == 0)
    {
        setError("no ota entry for " UPDATE_CHIP_KEY);
        g_state = FAILED;
        vTaskDelete(nullptr);
        return;
    }
    if (!FwHash::isValidHex(sha256))
    {
        // Refuse rather than fall back to an unverified download: without a
        // usable digest there is nothing to check the image against.
        setError("manifest has no valid sha256");
        g_state = FAILED;
        vTaskDelete(nullptr);
        return;
    }

    String base = manifest;
    g_url = base.substring(0, base.lastIndexOf('/') + 1) + path;
    g_sha256 = sha256;
    g_state = (compareVersions(latest, FIRMWARE_VERSION) > 0) ? AVAILABLE : IDLE;

    sysLog.printf("Update check: current=%s latest=%s state=%s\n",
                  FIRMWARE_VERSION, latest.c_str(), stateName(g_state));

    vTaskDelete(nullptr);
}

bool OtaService::startInstall()
{
    if (g_state == INSTALLING)
    {
        return false;
    }
    if (g_url.length() == 0 || !FwHash::isValidHex(g_sha256))
    {
        setError("no pending update - run the check first");
        g_state = FAILED;
        return false;
    }

    // Set the state before spawning, otherwise the HTTP response we are about
    // to send still says "available" and the frontend never starts polling.
    g_state    = INSTALLING;
    g_progress = 0;
    g_total    = 0;
    setError("");

    if (xTaskCreate(installTask, "ota_install", 8192, nullptr, 1, nullptr) != pdPASS)
    {
        setError("could not spawn install task");
        g_state = FAILED;
        return false;
    }
    return true;
}

void OtaService::installTask(void* arg)
{
    (void)arg;

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient https;
    if (!https.begin(client, g_url))
    {
        setError("HTTPS begin failed");
        g_state = FAILED;
        vTaskDelete(nullptr);
        return;
    }

    int code = https.GET();
    if (code != HTTP_CODE_OK)
    {
        setError("HTTP " + String(code));
        g_state = FAILED;
        https.end();
        vTaskDelete(nullptr);
        return;
    }

    int length = https.getSize();
    if (length <= 0)
    {
        setError("server did not report a content length");
        g_state = FAILED;
        https.end();
        vTaskDelete(nullptr);
        return;
    }
    g_total = (size_t)length;

    if (!Update.begin((size_t)length))
    {
        setError(String("Update.begin: ") + Update.errorString());
        g_state = FAILED;
        https.end();
        vTaskDelete(nullptr);
        return;
    }

    // The digest is computed here rather than handed to Update::setMD5():
    // that API only accepts MD5, whose collision resistance has been broken
    // since 2004. SHA-256 runs on the ESP32 hardware accelerator, so it costs
    // nothing next to the flash write.
    FwHash hash;
    hash.begin();

    // Read explicitly instead of using Update.writeStream(): that helper peeks
    // the stream before its read loop, and on some WiFiClientSecure versions
    // the peek desyncs the stream by one byte, producing a bogus digest.
    uint8_t* buffer = (uint8_t*)malloc(2048);
    if (buffer == nullptr)
    {
        setError("out of memory");
        g_state = FAILED;
        Update.abort();
        https.end();
        vTaskDelete(nullptr);
        return;
    }

    // NetworkClient rather than WiFiClient: HTTPClient::getStreamPtr() returns
    // the interface-agnostic type in Arduino-ESP32 3.x, which is what lets the
    // update run over Ethernet just as well as over WiFi. WiFiClient is only a
    // typedef for it and would drag in WiFi.h for no reason.
    NetworkClient* stream  = https.getStreamPtr();
    size_t         written = 0;
    int            stalls  = 0;

    while (written < (size_t)length)
    {
        size_t chunk = ((size_t)length - written < 2048) ? ((size_t)length - written) : 2048;
        int    got   = stream->readBytes((char*)buffer, chunk);

        if (got <= 0)
        {
            if (++stalls >= 300) // 30 s without a single byte
            {
                setError("stream read timeout");
                break;
            }
            delay(100);
            continue;
        }
        stalls = 0;

        if (Update.write(buffer, (size_t)got) != (size_t)got)
        {
            setError(String("short write: ") + Update.errorString());
            break;
        }
        hash.update(buffer, (size_t)got);
        written    += (size_t)got;
        g_progress  = written;
    }

    free(buffer);

    if (written != (size_t)length)
    {
        Update.abort();
        https.end();
        g_state = FAILED;
        vTaskDelete(nullptr);
        return;
    }

    // Verify BEFORE Update.end(): that call is what switches the boot
    // partition. Aborting here leaves the previous firmware in charge.
    hash.finish();
    if (!hash.matches(g_sha256))
    {
        sysLog.printf("OTA: SHA-256 mismatch\n  expected %s\n  actual   %s\n",
                      g_sha256.c_str(), hash.hex().c_str());
        setError("sha256 mismatch - image rejected");
        Update.abort();
        https.end();
        g_state = FAILED;
        vTaskDelete(nullptr);
        return;
    }

    if (!Update.end(true))
    {
        setError(String("Update.end: ") + Update.errorString());
        g_state = FAILED;
        https.end();
        vTaskDelete(nullptr);
        return;
    }

    https.end();
    g_state = DONE;
    sysLog.printf("Online OTA: %u bytes written, SHA-256 ok - rebooting\n", (unsigned)written);

    delay(2000); // let the frontend poll the status one last time
    ESP.restart();
}

const char* OtaService::runningPartition()
{
    const esp_partition_t* running = esp_ota_get_running_partition();
    return (running != nullptr) ? running->label : "?";
}

uint32_t OtaService::partitionSize()
{
    const esp_partition_t* running = esp_ota_get_running_partition();
    return (running != nullptr) ? running->size : 0;
}

const char* OtaService::runningPartitionState()
{
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t   state;

    if (running == nullptr || esp_ota_get_state_partition(running, &state) != ESP_OK)
    {
        return "?";
    }

    switch (state)
    {
    case ESP_OTA_IMG_NEW:            return "new";
    case ESP_OTA_IMG_PENDING_VERIFY: return "pending_verify";
    case ESP_OTA_IMG_VALID:          return "valid";
    case ESP_OTA_IMG_INVALID:        return "invalid";
    case ESP_OTA_IMG_ABORTED:        return "aborted";
    case ESP_OTA_IMG_UNDEFINED:      return "undefined";
    }
    return "?";
}

/*
 * Which firmware sits in which slot. The image's own descriptor cannot answer
 * that (see ota_service.h), so every boot writes its own details here under
 * the label of the slot it runs from.
 */
static Preferences prefs;
static const char* FW_NS = "sbip-fw";

/*
 * The notes, read once.
 *
 * A note only changes when a firmware boots from that slot, which means a
 * restart - so re-reading them per status poll bought nothing and printed an
 * NVS error line for every slot that has none yet. isKey() answers the same
 * question without the log noise, getString() logs at error level on a miss.
 */
static String s_slotLabel[2];
static String s_slotRecord[2];
static bool   s_slotsLoaded = false;

static const esp_partition_t* otherSlot();

static void loadSlotRecords()
{
    if (s_slotsLoaded) return;
    s_slotsLoaded = true;

    const esp_partition_t* slots[2] = {esp_ota_get_running_partition(), otherSlot()};

    if (!prefs.begin(FW_NS, false)) return;

    for (int i = 0; i < 2; i++)
    {
        if (slots[i] == nullptr) continue;

        s_slotLabel[i] = slots[i]->label;

        if (prefs.isKey(slots[i]->label))
        {
            s_slotRecord[i] = prefs.getString(slots[i]->label, "");
        }
    }

    prefs.end();
}

/** The note for a slot, or an empty string when there is none. */
static String slotRecord(const char* label)
{
    loadSlotRecords();

    for (int i = 0; i < 2; i++)
    {
        if (s_slotLabel[i] == label) return s_slotRecord[i];
    }

    return String();
}

/*
 * The slot a firmware upload would land in - which is always the one we are
 * not executing from. esp_ota_get_next_update_partition() picks exactly that.
 */
static const esp_partition_t* otherSlot()
{
    return esp_ota_get_next_update_partition(nullptr);
}

/*
 * Survives exactly one restart: switchPartition() writes it, the next boot
 * reads and clears it. NVS rather than RTC memory because the switch is meant
 * to survive a power cycle too.
 */
static const char* KEY_SWITCHED = "switched";

static bool takeSwitchFlag()
{
    if (!prefs.begin(FW_NS, false)) return false;

    bool pending = prefs.isKey(KEY_SWITCHED) && prefs.getBool(KEY_SWITCHED, false);
    if (pending) prefs.remove(KEY_SWITCHED);

    prefs.end();
    return pending;
}

/** One slot as a JSON object. */
static String partitionJson(const esp_partition_t* part, bool running)
{
    if (part == nullptr) return String("null");

    String json = "{\"label\":\"" + String(part->label) + "\",";
    json += "\"running\":" + String(running ? "true" : "false") + ",";

    esp_ota_img_states_t state;
    const char*          stateName = "?";

    if (esp_ota_get_state_partition(part, &state) == ESP_OK)
    {
        switch (state)
        {
        case ESP_OTA_IMG_NEW:            stateName = "new";            break;
        case ESP_OTA_IMG_PENDING_VERIFY: stateName = "pending_verify"; break;
        case ESP_OTA_IMG_VALID:          stateName = "valid";          break;
        case ESP_OTA_IMG_INVALID:        stateName = "invalid";        break;
        case ESP_OTA_IMG_ABORTED:        stateName = "aborted";        break;
        case ESP_OTA_IMG_UNDEFINED:      stateName = "undefined";      break;
        }
    }

    json += "\"state\":\"" + String(stateName) + "\",";

    // Does the slot hold a bootable image at all? That much the IDF descriptor
    // still tells us reliably, even though its contents do not.
    esp_app_desc_t desc;
    bool           valid = esp_ota_get_partition_description(part, &desc) == ESP_OK;

    json += "\"valid\":" + String(valid ? "true" : "false") + ",";
    json += "\"firmware\":\"" + jsonEscape(slotRecord(part->label)) + "\"}";
    return json;
}

void OtaService::recordOwnSlot()
{
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running == nullptr) return;

    // Our own translation unit, so these really are this firmware's numbers -
    // unlike esp_app_desc_t, which the prebuilt IDF fills with its own.
    String record = String(FIRMWARE_VERSION) + " #" + String(BUILD_NUMBER) +
                    " " + BUILD_GIT + ", " + __DATE__ " " __TIME__;

    loadSlotRecords();

    if (slotRecord(running->label) != record && prefs.begin(FW_NS, false))
    {
        prefs.putString(running->label, record);
        prefs.end();
        sysLog.printf("OTA: %s holds %s\n", running->label, record.c_str());
    }

    // Keep the cache in step so the dashboard shows it without a restart.
    for (int i = 0; i < 2; i++)
    {
        if (s_slotLabel[i] == running->label) s_slotRecord[i] = record;
    }
}

String OtaService::partitionTableJson()
{
    static String cached;

    if (cached.length())
    {
        return cached;
    }

    String json = "[";
    bool   first = true;

    for (esp_partition_type_t type : { ESP_PARTITION_TYPE_APP, ESP_PARTITION_TYPE_DATA })
    {
        esp_partition_iterator_t it =
            esp_partition_find(type, ESP_PARTITION_SUBTYPE_ANY, nullptr);

        while (it != nullptr)
        {
            const esp_partition_t* part = esp_partition_get(it);

            if (!first) json += ",";
            json += "{\"label\":\"" + jsonEscape(part->label) + "\"";
            json += ",\"type\":\"";
            json += (type == ESP_PARTITION_TYPE_APP) ? "app" : "data";
            json += "\",\"addr\":" + String(part->address);
            json += ",\"size\":" + String(part->size) + "}";
            first = false;

            it = esp_partition_next(it);
        }

        esp_partition_iterator_release(it);
    }

    json += "]";
    cached = json;
    return cached;
}

String OtaService::partitionsJson()
{
    /*
     * esp_partition_read() has to disable the flash cache, which stalls both
     * cores. Harmless once, but the dashboard asks every two seconds, so the
     * answer is kept and only refreshed occasionally - the rollback state is
     * the only part that changes while running.
     */
    static String   cached;
    static uint32_t cachedAt = 0;

    if (cached.length() && (millis() - cachedAt) < 10000) return cached;

    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* other   = otherSlot();

    cached = "[" + partitionJson(running, true) + "," +
             partitionJson(other, false) + "]";
    cachedAt = millis();

    return cached;
}

bool OtaService::switchPartition(String& error)
{
    const esp_partition_t* other = otherSlot();
    esp_app_desc_t         desc;

    if (other == nullptr ||
        esp_ota_get_partition_description(other, &desc) != ESP_OK)
    {
        error = "the other slot holds no valid firmware";
        sysLog.println("OTA: " + error);
        return false;
    }

    /*
     * Worth naming separately. With the bootloader rollback enabled a slot
     * that was left in PENDING_VERIFY across a reset is marked INVALID, and
     * from then on esp_ota_set_boot_partition() refuses it for good - the
     * image is still there and still reads as valid, which is exactly why
     * "nothing happens" is the wrong thing to report.
     */
    esp_ota_img_states_t state;

    if (esp_ota_get_state_partition(other, &state) == ESP_OK &&
        (state == ESP_OTA_IMG_INVALID || state == ESP_OTA_IMG_ABORTED))
    {
        error = String("the bootloader has rejected ") + other->label +
                " (state " + (state == ESP_OTA_IMG_INVALID ? "invalid" : "aborted") +
                ") - only a fresh upload can bring it back";
        sysLog.println("OTA: " + error);
        return false;
    }

    esp_err_t err = esp_ota_set_boot_partition(other);

    if (err != ESP_OK)
    {
        error = String("could not switch the boot partition: ") + esp_err_to_name(err);
        sysLog.println("OTA: " + error);
        return false;
    }

    // Read back rather than trust the return code: this writes the otadata
    // sector, and a switch that silently did not stick is the whole complaint.
    const esp_partition_t* boot = esp_ota_get_boot_partition();

    if (boot != other)
    {
        error = "the boot partition did not change";
        sysLog.println("OTA: " + error);
        return false;
    }

    if (prefs.begin(FW_NS, false))
    {
        prefs.putBool(KEY_SWITCHED, true);
        prefs.end();
    }

    error = "";
    sysLog.printf("OTA: next boot from %s (%s)\n", other->label, desc.version);
    return true;
}

String OtaService::statusJson() const
{    String json = "{";
    json += "\"state\":\"" + String(stateName(g_state)) + "\",";
    json += "\"current\":\"" FIRMWARE_VERSION "\",";
    json += "\"latest\":\"" + jsonEscape(g_latest) + "\",";
    json += "\"available\":" + String(g_state == AVAILABLE ? "true" : "false") + ",";
    json += "\"progress\":" + String((unsigned)g_progress) + ",";
    json += "\"total\":" + String((unsigned)g_total) + ",";
    json += "\"error\":\"" + jsonEscape(String(g_error)) + "\"";
    json += "}";
    return json;
}
