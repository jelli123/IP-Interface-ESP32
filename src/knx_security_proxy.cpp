/*
 *  knx_security_proxy.cpp - The Security Proxy of AN192 on this router.
 *  See knx_security_proxy.h.
 */

#include "knx_security_proxy.h"

#include <Preferences.h>
#include <knx/callback_property.h>
#include <knx/data_property.h>
#include <knx/device_object.h>
#include <knx/function_property.h>
#include <knx/router_object.h>
#include <knx/secure_application_layer.h>
#include <knx/security_interface_object.h>

#include "knx_secure_crypto.h"
#include "knx_seq_backup.h"
#include "log_buffer.h"

KnxSecurityProxy knxSecurityProxy;

using namespace knxproxy;

namespace
{

const char* NVS_NAMESPACE = "sbip-proxy";
const char* KEY_CONFIG    = "cfg";
const char* KEY_GROUPS    = "groups";

/** Configuration and keys reach NVS this long after the last change - ETS writes the table in pieces. */
const uint32_t PERSIST_DELAY_MS = 2000;

/**
 * The last valid sequence numbers of the proxied senders are part of the
 * security object, which is saved with the stack's memory image. Between
 * downloads the proxy moves them on with every telegram it decrypts, and
 * saving the object that often would wear out the flash. Every five minutes
 * at most - knx_seq_backup.cpp keeps every single number in RTC RAM, which
 * covers restarts; only a power cut falls back to this. With a FRAM there
 * is no need for it at all.
 */
const uint32_t SAVE_INTERVAL_MS = 5 * 60 * 1000;

/** Not more than one log line per this, a misconfigured line would flood it. */
const uint32_t REPORT_INTERVAL_MS = 10000;

/**
 * The second router object of AN192 2.3.1 for mask 091A: the secondary
 * side's PTP_CONFIG, BC_CONFIG and BC_ROUTING_TEMP. Not part of the memory
 * image - nothing in it is a DataProperty that saves.
 */
class SecondaryRouterObject : public InterfaceObject
{
public:
    SecondaryRouterObject()
    {
        Property* properties[] =
        {
            new DataProperty(PID_OBJECT_TYPE, false, PDT_UNSIGNED_INT, 1, ReadLv3 | WriteLv0, (uint16_t)OT_ROUTER),
            new DataProperty(PID_MEDIUM, false, PDT_ENUM8, 1, ReadLv3 | WriteLv0, (uint8_t)DptMedium::KNX_TP1),
            new CallbackProperty<SecondaryRouterObject>(this, (PropertyID)80, true, PDT_BITSET8, 1, ReadLv3 | WriteLv3,
                [](SecondaryRouterObject*, uint16_t start, uint8_t count, uint8_t* data) -> uint8_t
                { return knxSecurityProxy.propertyRead(SECONDARY, 80, start, count, data); },
                [](SecondaryRouterObject*, uint16_t start, uint8_t count, const uint8_t* data) -> uint8_t
                { return knxSecurityProxy.propertyWrite(SECONDARY, 80, start, count, data); }),
            new CallbackProperty<SecondaryRouterObject>(this, (PropertyID)82, true, PDT_BITSET8, 1, ReadLv3 | WriteLv3,
                [](SecondaryRouterObject*, uint16_t start, uint8_t count, uint8_t* data) -> uint8_t
                { return knxSecurityProxy.propertyRead(SECONDARY, 82, start, count, data); },
                [](SecondaryRouterObject*, uint16_t start, uint8_t count, const uint8_t* data) -> uint8_t
                { return knxSecurityProxy.propertyWrite(SECONDARY, 82, start, count, data); }),
            new FunctionProperty<SecondaryRouterObject>(this, (PropertyID)84,
                [](SecondaryRouterObject*, uint8_t* data, uint8_t length, uint8_t* result, uint8_t& resultLength) -> void
                { knxSecurityProxy.propertyFunction(SECONDARY, true, 84, data, length, result, resultLength); },
                [](SecondaryRouterObject*, uint8_t* data, uint8_t length, uint8_t* result, uint8_t& resultLength) -> void
                { knxSecurityProxy.propertyFunction(SECONDARY, false, 84, data, length, result, resultLength); }),
        };
        initializeProperties(sizeof(properties), properties);
    }
};

void formatAddress(char* out, size_t size, uint16_t address, bool group)
{
    if (group)
        snprintf(out, size, "%u/%u/%u", address >> 11, (address >> 8) & 0x07, address & 0xFF);
    else
        snprintf(out, size, "%u.%u.%u", address >> 12, (address >> 8) & 0x0F, address & 0xFF);
}

} // namespace

InterfaceObject& KnxSecurityProxy::secondaryRouterObject()
{
    // Built on first use, so it never depends on the order of static constructors.
    static SecondaryRouterObject object;
    return object;
}

void KnxSecurityProxy::begin(DeviceObject& device, RouterObject& primary, SecurityInterfaceObject& security,
                             SecureApplicationLayer& secureLayer)
{
    _device      = &device;
    _primary     = &primary;
    _security    = &security;
    _secureLayer = &secureLayer;

    // Before anything can return early: the backup is needed with or
    // without a stored configuration.
    restoreSequences();

    // Fails on the very first start - the namespace does not exist yet.
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true))
        return;

    uint8_t config[4];
    if (prefs.getBytes(KEY_CONFIG, config, sizeof(config)) == sizeof(config))
    {
        _core.ptpConfig(PRIMARY, config[0]);
        _core.ptpConfig(SECONDARY, config[1]);
        _core.bcConfig(PRIMARY, config[2]);
        _core.bcConfig(SECONDARY, config[3]);
    }

    size_t stored = prefs.getBytesLength(KEY_GROUPS);
    if (stored > 0 && stored <= (size_t)MAX_GROUP_KEYS * GROUP_KEY_SIZE)
    {
        uint8_t* blob = (uint8_t*)malloc(stored);
        if (blob != nullptr)
        {
            prefs.getBytes(KEY_GROUPS, blob, stored);
            _core.groupKeys().deserialize(blob, stored);
            knxsec::wipe(blob, stored);
            free(blob);
        }
    }
    prefs.end();

    if (_core.groupKeys().count() > 0 || _core.ptpConfig(SECONDARY) != ROUTE_RULE ||
        _core.ptpConfig(PRIMARY) != ROUTE_RULE || _core.bcConfig(PRIMARY) != ROUTE_RULE ||
        _core.bcConfig(SECONDARY) != ROUTE_RULE)
    {
        sysLog.printf("PROXY: %u group keys, point-to-point %s/%s, broadcast %s/%s (IP/TP)\n",
                      _core.groupKeys().count(),
                      _core.ptpConfig(PRIMARY) & 0x03 ? "rule" : "block",
                      _core.ptpConfig(SECONDARY) & 0x03 ? "rule" : "block",
                      _core.bcConfig(PRIMARY) & 0x03 ? "rule" : "block",
                      _core.bcConfig(SECONDARY) & 0x03 ? "rule" : "block");
    }
}

void KnxSecurityProxy::restoreSequences()
{
    knxSeqBackup.begin();

    // The larger number wins: whatever the security object says may be up
    // to five minutes old, and a sender's counter only ever goes up.
    uint8_t restored = 0;

    for (uint8_t slot = 0; slot < KnxSeqBackup::SLOTS; slot++)
    {
        uint16_t address;
        uint64_t seq;

        if (!knxSeqBackup.load(slot, address, seq) || _security->indAddressIndex(address) == 0)
            continue;

        if (seq > _security->getLastValidSequenceNumber(address))
        {
            _security->setLastValidSequenceNumber(address, seq);
            restored++;
        }
    }

    if (restored > 0)
    {
        _seqDirty = true;
        sysLog.printf("PROXY: %u last valid sequence numbers taken from %s\n", restored,
                      knxSeqBackup.mediumName());
    }
}

void KnxSecurityProxy::loop()
{
    if (_dirty && millis() - _dirtyAt >= PERSIST_DELAY_MS)
        persist();
}

void KnxSecurityProxy::persist()
{
    _dirty = false;

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false))
    {
        sysLog.println("PROXY: could not open NVS, configuration not saved");
        return;
    }

    uint8_t config[4] = {_core.ptpConfig(PRIMARY), _core.ptpConfig(SECONDARY),
                         _core.bcConfig(PRIMARY), _core.bcConfig(SECONDARY)};
    prefs.putBytes(KEY_CONFIG, config, sizeof(config));

    size_t   size = (size_t)_core.groupKeys().count() * GROUP_KEY_SIZE;
    uint8_t* blob = size ? (uint8_t*)malloc(size) : nullptr;

    if (blob != nullptr && _core.groupKeys().serialize(blob, size) == size)
        prefs.putBytes(KEY_GROUPS, blob, size);
    else
        prefs.remove(KEY_GROUPS);

    if (blob != nullptr)
    {
        knxsec::wipe(blob, size);
        free(blob);
    }
    prefs.end();
}

bool KnxSecurityProxy::groupKeysApply() const
{
    // 2.3.1.5.6: an empty table, or one in a router object that is not
    // loaded, is no table - ETS unloads it while it downloads.
    return _primary != nullptr && _core.groupKeys().count() > 0 &&
           _primary->loadState() == LS_LOADED;
}

void KnxSecurityProxy::report(const char* what, uint16_t source, uint16_t destination, bool group)
{
    uint32_t now = millis();
    if (_reportedAt != 0 && now - _reportedAt < REPORT_INTERVAL_MS)
        return;
    _reportedAt = now | 1;

    char from[12], to[12];
    formatAddress(from, sizeof(from), source, false);
    formatAddress(to, sizeof(to), destination, group);
    sysLog.printf("PROXY: %s, %s -> %s\n", what, from, to);
}

NPDU* KnxSecurityProxy::route(AddressType addrType, uint16_t destination, bool systemBroadcast,
                              NPDU& npdu, uint16_t& source, bool fromPrimary)
{
    uint8_t  from = fromPrimary ? PRIMARY : SECONDARY;
    uint32_t now  = millis();

    if (addrType == IndividualAddress)
    {
        if (_core.routeIndividual(from, source, destination, now))
            return &npdu;

        _stats.ptpBlocked++;
        return nullptr;
    }

    if (destination == 0)
    {
        if (_core.routeBroadcast(from, systemBroadcast, now))
            return &npdu;

        _stats.bcBlocked++;
        return nullptr;
    }

    // Group telegram: the TPDU starts behind the cEMI header, its additional
    // information and the L field.
    uint8_t* frame  = npdu.frame().data();
    uint8_t* tpdu   = frame + frame[1] + TPDU_LPDU_DIFF;
    size_t   length = (size_t)npdu.octetCount() + 1;

    Kind            kind  = classify(tpdu, length);
    const GroupKey* entry = groupKeysApply() ? _core.groupKeys().find(destination) : nullptr;
    Action          action = groupAction(entry, from, kind, length > 2 ? tpdu[2] : 0);

    size_t out = 0;

    switch (action)
    {
        case FORWARD:
            return &npdu;

        case BLOCK:
            _stats.blocked++;
            if (entry != nullptr)
                report(kind == PLAIN ? "plain telegram blocked" : "secured telegram blocked", source, destination, true);
            return nullptr;

        case DECRYPT:
        {
            // The sender has to be known: its last valid sequence number is
            // what keeps a recorded telegram from being played back.
            uint16_t index = _security->indAddressIndex(source);

            if (index == 0)
            {
                _stats.unknownSource++;
                report("sender not in the security IA table", source, destination, true);
                return nullptr;
            }

            uint64_t seq = 0;
            out = open(entry->key, source, destination, tpdu, length, _buffer, seq);

            if (out == 0)
            {
                _stats.macFailures++;
                report("MAC wrong", source, destination, true);
                return nullptr;
            }

            if (seq <= _security->getLastValidSequenceNumber(source))
            {
                _stats.replays++;
                knxsec::wipe(_buffer, out);
                report("sequence number already seen", source, destination, true);
                return nullptr;
            }

            _security->setLastValidSequenceNumber(source, seq);
            knxSeqBackup.store((uint8_t)(index - 1), source, seq);
            _seqDirty = true;
            _stats.decrypted++;
            break;
        }

        case ENCRYPT:
        {
            uint16_t own = _device->individualAddress();
            uint64_t seq = _secureLayer->sbipTakeSequenceNumber();

            out = seal(entry->key, own, destination, seq, (entry->settings & FLAG_CONF) != 0,
                       tpdu, length, _buffer);
            if (out == 0)
            {
                _stats.blocked++;
                report("telegram too long to secure", source, destination, true);
                return nullptr;
            }

            // 2.2.2.3: the secured telegram comes from the proxy.
            source = own;
            _stats.encrypted++;
            break;
        }
    }

    _out = CemiFrame((uint8_t)(out - 1));
    _out.hopCount(npdu.hopCount());
    memcpy(_out.data() + TPDU_LPDU_DIFF, _buffer, out);
    knxsec::wipe(_buffer, out);
    return &_out.npdu();
}

uint8_t KnxSecurityProxy::propertyRead(uint8_t side, uint8_t pid, uint16_t start, uint8_t count, uint8_t* data)
{
    if (pid == 81)
        return side == PRIMARY ? _core.groupKeys().read(start, count, data) : 0;

    if (pid != 80 && pid != 82)
        return 0;

    if (start == 0)
    {
        data[0] = 0;
        data[1] = 1;
        return 1;
    }

    if (start != 1 || count != 1)
        return 0;

    data[0] = pid == 80 ? _core.ptpConfig(side) : _core.bcConfig(side);
    return 1;
}

uint8_t KnxSecurityProxy::propertyWrite(uint8_t side, uint8_t pid, uint16_t start, uint8_t count, const uint8_t* data)
{
    uint8_t written = 0;

    if (pid == 81)
    {
        if (side != PRIMARY)
            return 0;
        written = _core.groupKeys().write(start, count, data);
    }
    else if (pid == 80 || pid == 82)
    {
        if (start != 1 || count != 1)
            return 0;

        // PTP: the reserved bits are kept as written (2.3.1.1.3).
        // BC: they are ignored, but storing them does no harm.
        if (pid == 80)
            _core.ptpConfig(side, data[0]);
        else
            _core.bcConfig(side, data[0]);
        written = 1;
    }

    if (written)
    {
        _dirty   = true;
        _dirtyAt = millis();
    }
    return written;
}

void KnxSecurityProxy::propertyFunction(uint8_t side, bool command, uint8_t pid, uint8_t* data, uint8_t length,
                                        uint8_t* result, uint8_t& resultLength)
{
    uint32_t now = millis();

    if (pid == 83 && side == PRIMARY)
    {
        _core.ptpTemp(command, data, length, result, resultLength, _device->individualAddress(), now);

        uint16_t partners = length >= 8 ? (uint16_t)((data[6] << 8) | data[7]) : 0;

        if (command && data[1] == 0 && partners > 0 && result[0] == E_SUCCESS)
        {
            char user[12], partner[12];
            formatAddress(user, sizeof(user), (uint16_t)((data[2] << 8) | data[3]), false);
            formatAddress(partner, sizeof(partner), (uint16_t)((data[4] << 8) | data[5]), false);
            sysLog.printf("PROXY: point-to-point opened for 60 s, %s <-> %u from %s\n",
                          user, (unsigned)partners, partner);
        }
        return;
    }

    if (pid == 84)
    {
        _core.bcTemp(side, command, data, length, result, resultLength, now);
        return;
    }

    result[0]    = E_COMMAND_INVALID;
    resultLength = 1;
}

void KnxSecurityProxy::masterReset(uint8_t eraseCode)
{
    if (_core.masterReset(eraseCode))
        persist();

    // With the keys go the senders they were for (02h and 07h reset the
    // security object as well).
    if (eraseCode == FactoryReset || eraseCode == FactoryResetWithoutIA)
        knxSeqBackup.clear();
}

void KnxSecurityProxy::clearKeys()
{
    _core.groupKeys().clear();
    persist();
    knxSeqBackup.clear();
}

bool KnxSecurityProxy::sequenceSaveDue()
{
    if (!_seqDirty)
        return false;

    // The FRAM has every number already, at no cost.
    if (knxSeqBackup.medium() == KnxSeqBackup::FRAM)
    {
        _seqDirty = false;
        return false;
    }

    uint32_t now = millis();
    if (_seqSavedAt != 0 && now - _seqSavedAt < SAVE_INTERVAL_MS)
        return false;

    _seqDirty   = false;
    _seqSavedAt = now | 1;
    return true;
}

String KnxSecurityProxy::json() const
{
    auto route = [](uint8_t value) -> const char* { return (value & 0x03) == ROUTE_RULE ? "rule" : "block"; };

    String j = "{";
    j += "\"keys\":" + String(_core.groupKeys().count());
    j += ",\"active\":" + String(groupKeysApply() ? "true" : "false");
    j += ",\"ptp\":[\"" + String(route(_core.ptpConfig(PRIMARY))) + "\",\"" + route(_core.ptpConfig(SECONDARY)) + "\"]";
    j += ",\"bc\":[\"" + String(route(_core.bcConfig(PRIMARY))) + "\",\"" + route(_core.bcConfig(SECONDARY)) + "\"]";
    j += ",\"decrypted\":" + String(_stats.decrypted);
    j += ",\"encrypted\":" + String(_stats.encrypted);
    j += ",\"blocked\":" + String(_stats.blocked);
    j += ",\"mac_fail\":" + String(_stats.macFailures);
    j += ",\"replays\":" + String(_stats.replays);
    j += ",\"unknown\":" + String(_stats.unknownSource);
    j += ",\"ptp_blocked\":" + String(_stats.ptpBlocked);
    j += ",\"bc_blocked\":" + String(_stats.bcBlocked);
    j += ",\"seq_backup\":\"" + String(knxSeqBackup.mediumName()) + "\"";
    j += "}";
    return j;
}
