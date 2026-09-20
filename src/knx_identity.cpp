/*
 *  knx_identity.cpp - What this device claims to be towards ETS.
 */

#include <Preferences.h>

#include <cstring>

#include "interface_config.h"
#include "json_util.h"
#include "knx_identity.h"

#include "log_buffer.h"
KnxIdentityStore knxIdentity;

static Preferences prefs;

/** NVS namespace. Separate from the hardware profile and from the stack. */
static const char* NS = "knxid";

/**
 * Layout marker for the stored blob.
 *
 * The identity is written as a raw structure, so a change to KnxIdentity
 * makes the old bytes meaningless. Reading them anyway would hand ETS a
 * manufacturer assembled from whatever moved - bump this and the identity
 * falls back to the built-in defaults, which is a device that has to be
 * reprogrammed rather than one that lies about itself.
 */
static const uint8_t LAYOUT_VERSION = 1;

/* ------------------------------------------------------------------------- *
 * Helpers
 * ------------------------------------------------------------------------- */

/**
 * Printable ASCII without the two characters that would have to be escaped
 * on the way out.
 *
 * The whitelist is not cosmetic: the name is echoed into JSON, into the
 * dashboard and into the log, and refusing quotes, backslashes and control
 * characters at the boundary is what makes all three safe.
 */
static bool textValid(const char* text, uint8_t maxLen)
{
    size_t n = strlen(text);
    if (n > maxLen) return false;

    for (size_t i = 0; i < n; i++)
    {
        char c = text[i];
        if (c < 0x20 || c > 0x7E) return false;
        if (c == '"' || c == '\\') return false;
    }
    return true;
}

/**
 * True when the value of @p key is written as a JSON string.
 *
 * Has to be asked before jsonGetString(): that one takes the next quoted run
 * after the colon, and for a numeric value the next quoted run is the
 * *following key*. Without this check "manufacturer": 250 would be read as
 * the text "app_number" and silently fall back.
 */
static bool valueIsText(const String& json, const char* key)
{
    String pattern = String("\"") + key + "\"";

    for (int at = json.indexOf(pattern); at >= 0; at = json.indexOf(pattern, at + 1))
    {
        int i = at + (int)pattern.length();
        while (i < (int)json.length() && isspace((int)json[i])) i++;

        if (i >= (int)json.length() || json[i] != ':') continue;

        i++;
        while (i < (int)json.length() && isspace((int)json[i])) i++;

        return i < (int)json.length() && json[i] == '"';
    }
    return false;
}

/** Read a number that may be written as decimal or as a hex string. */
static uint32_t readNum(const String& json, const char* key, uint32_t fallback)
{
    if (!jsonHasKey(json, key)) return fallback;

    // "manufacturer": "00FA" and "manufacturer": 250 both have to work: the
    // dashboard sends numbers, a hand written file tends to carry the hex
    // spelling the product data uses.
    if (valueIsText(json, key))
    {
        String text = jsonGetString(json, key);
        if (text.startsWith("0x") || text.startsWith("0X")) text = text.substring(2);
        if (text.length() == 0) return fallback;

        char*    end   = nullptr;
        uint32_t value = (uint32_t)strtoul(text.c_str(), &end, 16);
        return (end != nullptr && *end == '\0') ? value : fallback;
    }

    int32_t number = jsonGetInt(json, key, (int32_t)fallback);
    return number < 0 ? fallback : (uint32_t)number;
}

/** Six octets as twelve hex digits, separators allowed but not required. */
static bool parseHardwareType(const String& text, uint8_t* out)
{
    String hex;
    for (unsigned i = 0; i < text.length(); i++)
    {
        char c = text[i];
        if (c == ' ' || c == ':' || c == '-' || c == '.') continue;
        hex += c;
    }

    if (hex.length() == 0)
    {
        memset(out, 0, KNX_ID_HW_TYPE_LEN);
        return true;
    }
    if (hex.length() != KNX_ID_HW_TYPE_LEN * 2) return false;

    for (uint8_t i = 0; i < KNX_ID_HW_TYPE_LEN; i++)
    {
        char pair[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};
        char* end    = nullptr;
        unsigned long value = strtoul(pair, &end, 16);
        if (end == nullptr || *end != '\0') return false;
        out[i] = (uint8_t)value;
    }
    return true;
}

static String hardwareTypeToHex(const uint8_t* hw)
{
    char text[KNX_ID_HW_TYPE_LEN * 2 + 1];
    for (uint8_t i = 0; i < KNX_ID_HW_TYPE_LEN; i++)
    {
        snprintf(text + i * 2, 3, "%02X", hw[i]);
    }
    return String(text);
}

/* ------------------------------------------------------------------------- *
 * Defaults, storage
 * ------------------------------------------------------------------------- */

KnxIdentity KnxIdentityStore::defaults()
{
    KnxIdentity id;

    strlcpy(id.name, SBIP_KNX_PRODUCT_NAME, sizeof(id.name));
    id.manufacturer  = SBIP_KNX_MANUFACTURER_ID;
    id.appNumber     = SBIP_KNX_APP_NUMBER;
    id.appVersion    = SBIP_KNX_APP_VERSION;
    id.deviceVersion = SBIP_KNX_DEVICE_VERSION;
    id.maskVersion   = KNX_ID_MASK;
    id.tunnels       = SBIP_KNX_TUNNELS;

    const uint8_t hw[KNX_ID_HW_TYPE_LEN] = {SBIP_KNX_HARDWARE_TYPE};
    memcpy(id.hardwareType, hw, sizeof(id.hardwareType));

    strlcpy(id.orderInfo, SBIP_KNX_ORDER_INFO, sizeof(id.orderInfo));

    return id;
}

void KnxIdentityStore::load()
{
    if (!prefs.begin(NS, true))
    {
        _hasStored = false;
        return;
    }

    uint8_t layout = prefs.getUChar("ver", 0);
    size_t  length = prefs.getBytesLength("id");

    if (layout == LAYOUT_VERSION && length == sizeof(KnxIdentity))
    {
        prefs.getBytes("id", &_stored, sizeof(_stored));
        _hasStored = true;

        // A blob is trusted only as far as its own fields; the terminators
        // are what every strlen() below depends on.
        _stored.name[KNX_ID_NAME_MAX]       = '\0';
        _stored.orderInfo[KNX_ID_ORDER_MAX] = '\0';
    }
    else
    {
        _hasStored = false;
        if (length != 0)
        {
            sysLog.println("KNX: stored identity has an older layout - using the defaults");
        }
    }

    prefs.end();
}

bool KnxIdentityStore::store(const KnxIdentity& id)
{
    if (!prefs.begin(NS, false)) return false;

    bool ok = prefs.putBytes("id", &id, sizeof(id)) == sizeof(id);
    ok = ok && prefs.putUChar("ver", LAYOUT_VERSION) == sizeof(uint8_t);
    prefs.end();

    return ok;
}

void KnxIdentityStore::begin()
{
    _active = defaults();
    load();

    if (!_hasStored)
    {
        _stored   = _active;
        _fallback = FB_UNCONFIGURED;
        return;
    }

    String error;
    if (!validate(_stored, error))
    {
        _fallback = FB_INVALID;
        sysLog.printf("KNX: stored identity rejected (%s) - using the defaults\n",
                      error.c_str());
        return;
    }

    _active   = _stored;
    _fallback = FB_NONE;
}

bool KnxIdentityStore::resetToDefaults()
{
    if (!prefs.begin(NS, false)) return false;
    bool ok = prefs.clear();
    prefs.end();

    if (!ok) return false;

    _hasStored     = false;
    _stored        = defaults();
    _rebootPending = !same(_active, _stored);
    return true;
}

const char* KnxIdentityStore::fallbackReason() const
{
    switch (_fallback)
    {
        case FB_UNCONFIGURED: return "nichts gespeichert";
        case FB_INVALID:      return "gespeicherte Kennung abgelehnt";
        default:              return "";
    }
}

/* ------------------------------------------------------------------------- *
 * Validation
 * ------------------------------------------------------------------------- */

bool KnxIdentityStore::validate(const KnxIdentity& id, String& error)
{
    if (id.maskVersion != KNX_ID_MASK)
    {
        error = "Maskenversion " + String(id.maskVersion, HEX) +
                " - diese Firmware ist ein Koppler der Maske 091A. Ein "
                "Produkt für eine andere Maske lässt sich nicht nachbilden.";
        return false;
    }

    if (id.manufacturer == 0)
    {
        error = "Herstellerkennung 0 ist nicht vergeben.";
        return false;
    }

    if (id.appNumber == 0)
    {
        error = "Applikationsnummer 0 - ein knxprod kann darauf nicht "
                "verweisen.";
        return false;
    }

    if (id.appVersion == 0)
    {
        error = "Applikationsversion 0 ist keine gültige Version.";
        return false;
    }

    if (id.tunnels == 0 || id.tunnels > KNX_TUNNELING)
    {
        error = "Das Produkt verwaltet " + String(id.tunnels) +
                " Tunneladressen, dieses Image bietet " + String(KNX_TUNNELING) +
                ". Mehr als einkompiliert geht nicht: die Adressen liegen in "
                "Feldern fester Größe. Entweder ein Produkt mit weniger "
                "wählen oder KNX_TUNNELING in platformio.ini anheben.";
        return false;
    }

    if (!textValid(id.name, KNX_ID_NAME_MAX))
    {
        error = "Der Produktname ist zu lang oder enthält Sonderzeichen.";
        return false;
    }

    if (!textValid(id.orderInfo, KNX_ID_ORDER_MAX))
    {
        error = "Die Bestellnummer ist länger als zehn Zeichen oder enthält "
                "Sonderzeichen.";
        return false;
    }

    return true;
}

bool KnxIdentityStore::invalidatesImage(const KnxIdentity& from, const KnxIdentity& to)
{
    return from.manufacturer != to.manufacturer ||
           from.deviceVersion != to.deviceVersion ||
           memcmp(from.hardwareType, to.hardwareType, KNX_ID_HW_TYPE_LEN) != 0;
}

bool KnxIdentityStore::same(const KnxIdentity& a, const KnxIdentity& b)
{
    return !invalidatesImage(a, b) &&
           a.appNumber == b.appNumber &&
           a.appVersion == b.appVersion &&
           a.maskVersion == b.maskVersion &&
           a.tunnels == b.tunnels &&
           strncmp(a.name, b.name, KNX_ID_NAME_MAX + 1) == 0 &&
           strncmp(a.orderInfo, b.orderInfo, KNX_ID_ORDER_MAX + 1) == 0;
}

/* ------------------------------------------------------------------------- *
 * JSON
 * ------------------------------------------------------------------------- */

bool KnxIdentityStore::applyJson(const String& json, String& error)
{
    // Start from what is running, so a partial document acts as a patch -
    // the knxprod import sends only the fields it could read.
    KnxIdentity id = _active;

    /*
     * Read wide, then check, then narrow. Casting first would turn 65538
     * into a 2 and store a manufacturer the user never typed - a silent
     * wrong identity is worse than a refused document.
     */
    struct
    {
        uint32_t    value;
        uint32_t    limit;
        const char* label;
    } numbers[] = {
        {readNum(json, "manufacturer",   id.manufacturer),  0xFFFF, "Herstellerkennung"},
        {readNum(json, "app_number",     id.appNumber),     0xFFFF, "Applikationsnummer"},
        {readNum(json, "app_version",    id.appVersion),    0xFF,   "Applikationsversion"},
        {readNum(json, "device_version", id.deviceVersion), 0xFFFF, "Geräteversion"},
        {readNum(json, "mask",           id.maskVersion),   0xFFFF, "Maskenversion"},
        {readNum(json, "tunnels",        id.tunnels),       0xFF,   "Tunnelanzahl"},
    };

    for (const auto& number : numbers)
    {
        if (number.value > number.limit)
        {
            error = String(number.label) + " ist zu groß.";
            return false;
        }
    }

    id.manufacturer  = (uint16_t)numbers[0].value;
    id.appNumber     = (uint16_t)numbers[1].value;
    id.appVersion    = (uint8_t) numbers[2].value;
    id.deviceVersion = (uint16_t)numbers[3].value;
    id.maskVersion   = (uint16_t)numbers[4].value;
    id.tunnels       = (uint8_t) numbers[5].value;

    // Text only where the document really holds text - see valueIsText().
    if (valueIsText(json, "name"))
    {
        strlcpy(id.name, jsonGetString(json, "name").c_str(), sizeof(id.name));
    }
    if (valueIsText(json, "order_info"))
    {
        strlcpy(id.orderInfo, jsonGetString(json, "order_info").c_str(),
                sizeof(id.orderInfo));
    }
    if (valueIsText(json, "hardware_type") &&
        !parseHardwareType(jsonGetString(json, "hardware_type"), id.hardwareType))
    {
        error = "Der Hardwaretyp braucht zwölf Hexziffern oder muss leer bleiben.";
        return false;
    }

    if (!validate(id, error)) return false;

    if (!store(id))
    {
        error = "Die Kennung ließ sich nicht speichern.";
        return false;
    }

    _stored        = id;
    _hasStored     = true;
    _rebootPending = !same(_active, id);

    return true;
}

String KnxIdentityStore::identityToJson(const KnxIdentity& id)
{
    String j = "{";
    j += "\"name\":\"" + jsonEscape(String(id.name)) + "\",";
    j += "\"manufacturer\":" + String(id.manufacturer) + ",";
    j += "\"app_number\":" + String(id.appNumber) + ",";
    j += "\"app_version\":" + String(id.appVersion) + ",";
    j += "\"device_version\":" + String(id.deviceVersion) + ",";
    j += "\"mask\":" + String(id.maskVersion) + ",";
    j += "\"tunnels\":" + String(id.tunnels) + ",";
    j += "\"hardware_type\":\"" + hardwareTypeToHex(id.hardwareType) + "\",";
    j += "\"order_info\":\"" + jsonEscape(String(id.orderInfo)) + "\"";
    j += "}";
    return j;
}

String KnxIdentityStore::toJson() const
{
    String j = "{";
    j += "\"active\":" + identityToJson(_active) + ",";
    j += "\"stored\":" + identityToJson(_stored) + ",";
    j += "\"defaults\":" + identityToJson(defaults()) + ",";
    j += "\"has_stored\":" + String(_hasStored ? "true" : "false") + ",";
    j += "\"using_defaults\":" + String(usingDefaults() ? "true" : "false") + ",";
    j += "\"fallback\":\"" + jsonEscape(String(fallbackReason())) + "\",";
    j += "\"reboot_pending\":" + String(_rebootPending ? "true" : "false") + ",";

    // What the image can carry, so the dashboard can refuse a knxprod before
    // the device has to.
    j += "\"max_tunnels\":" + String(KNX_TUNNELING) + ",";
    j += "\"supported_mask\":" + String(KNX_ID_MASK);
    j += "}";
    return j;
}
