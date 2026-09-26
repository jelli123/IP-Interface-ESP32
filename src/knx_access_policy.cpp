/*
 *  knx_access_policy.cpp - Who may use which management service.
 *
 *  The values are those of AN193 "Access Policies" v04 (KNX Standard v3.0),
 *  2.2.3 for the services and 2.2.4 for the data, and of 03_08_09 KNX IP
 *  Secure 2.3.1 for the KNXnet/IP Secure properties. Every entry of AN193
 *  that concerns an object or service this device has is here; anything not
 *  listed defaults to 3FF/0CC, which is what AN193 gives almost every
 *  property.
 *
 *  With secure mode off the "off" half applies. It is 3FF - everything -
 *  for all ordinary properties and services, so a device ETS programs
 *  without security notices nothing of this. Only the security material has
 *  a restricted off half.
 */

#include "knx_access_policy.h"

namespace knxaccess
{

namespace
{

constexpr uint32_t policy(uint16_t off, uint16_t on)
{
    return ((uint32_t)off << 10) | on;
}

const uint8_t READ  = 0x01;
const uint8_t WRITE = 0x02;

// Object types
const uint16_t OT_DEVICE   = 0;
const uint16_t OT_ROUTER   = 6;
const uint16_t OT_CEMI     = 8;
const uint16_t OT_IP       = 11;
const uint16_t OT_SECURITY = 17;

// AN193: nearly every property, and memory.
const uint32_t STANDARD = policy(0x3FF, 0x0CC);

struct Entry
{
    uint16_t objectType;
    uint16_t propertyId;
    uint32_t policy;
};

// AN193 2.2.4.6, only the entries that differ from 3FF/0CC.
const Entry PROPERTIES[] = {
    // Device object
    {OT_DEVICE, 56, policy(0x3FF, 0x1FF)},   // PID_MAX_APDU_LENGTH
    {OT_DEVICE, 57, policy(0x3FF, 0x00C)},   // PID_SUBNET_ADDR
    {OT_DEVICE, 58, policy(0x3FF, 0x00C)},   // PID_DEVICE_ADDR
    {OT_DEVICE, 59, policy(0x3FF, 0x00C)},   // PID_PB_CONFIG
    {OT_DEVICE, 70, policy(0x3FF, 0x00C)},   // PID_DOMAIN_ADDRESS
    {OT_DEVICE, 82, policy(0x3FF, 0x00C)},   // PID_RF_DOMAIN_ADDRESS

    // Router object
    {OT_ROUTER, 51, policy(0x3FF, 0x1FF)},   // PID_LINE_STATUS
    {OT_ROUTER, 58, policy(0x3FF, 0x1FF)},   // PID_MAX_APDULENGTH_ROUTING
    {OT_ROUTER, 80, policy(0x00C, 0x00C)},   // PID_SECURITY_ROUTING_CONTROL
    {OT_ROUTER, 81, policy(0x00C, 0x00C)},   // PID_SECURITY_PROXY_GRP_KEY_TABLE
    {OT_ROUTER, 82, policy(0x00C, 0x00C)},   // PID_SECURITY_PROXY_ZONE_KEY_TABLE
    {OT_ROUTER, 83, policy(0x00C, 0x00C)},   // PID_SECURITY_PROXY_INDIVIDUAL_ADDRESS_TABLE

    // cEMI server object
    {OT_CEMI, 51, policy(0x3FF, 0x1FF)},     // PID_MEDIUM_TYPE
    {OT_CEMI, 52, policy(0x3FF, 0x1FF)},     // PID_COMM_MODE
    {OT_CEMI, 53, policy(0x3FF, 0x1FF)},     // PID_MEDIUM_AVAILABILITY
    {OT_CEMI, 57, policy(0x3FF, 0x1FF)},     // PID_CEMI_SERVER_SNA
    {OT_CEMI, 58, policy(0x3FF, 0x1FF)},     // PID_CEMI_SERVER_DEVICE_ADDRESS
    {OT_CEMI, 68, policy(0x3FF, 0x1FF)},     // PID_MAX_INTERFACE_APDU_LENGTH

    // KNXnet/IP parameter object
    {OT_IP, 79, policy(0x15F, 0x04C)},       // PID_TUNNELLING_ADDRESSES
    {OT_IP, 91, policy(0x008, 0x008)},       // PID_BACKBONE_KEY
    {OT_IP, 92, policy(0x008, 0x008)},       // PID_DEVICE_AUTHENTICATION_CODE
    {OT_IP, 93, policy(0x008, 0x008)},       // PID_PASSWORD_HASHES
    {OT_IP, 94, policy(0x15F, 0x04C)},       // PID_SECURED_SERVICE_FAMILIES
    {OT_IP, 95, policy(0x15F, 0x04C)},       // PID_MULTICAST_LATENCY_TOLERANCE
    {OT_IP, 96, policy(0x15F, 0x04C)},       // PID_SYNC_LATENCY_FRACTION
    {OT_IP, 97, policy(0x15F, 0x04C)},       // PID_TUNNELLING_USERS

    // Security interface object
    {OT_SECURITY, 5, policy(0x15F, 0x04C)},  // PID_LOAD_STATE_CONTROL
    {OT_SECURITY, 51, policy(0x15F, 0x04C)}, // PID_SECURITY_MODE
    {OT_SECURITY, 52, policy(0x00C, 0x00C)}, // PID_P2P_KEY_TABLE
    {OT_SECURITY, 53, policy(0x00C, 0x00C)}, // PID_GRP_KEY_TABLE
    {OT_SECURITY, 54, policy(0x00C, 0x00C)}, // PID_SECURITY_INDIVIDUAL_ADDRESS_TABLE
    {OT_SECURITY, 55, policy(0x1FF, 0x0CC)}, // PID_SECURITY_FAILURES_LOG
    {OT_SECURITY, 56, policy(0x008, 0x008)}, // PID_TOOL_KEY
    {OT_SECURITY, 57, policy(0x1FF, 0x0CC)}, // PID_SECURITY_REPORT
    {OT_SECURITY, 58, policy(0x00C, 0x00C)}, // PID_SECURITY_REPORT_CONTROL
    {OT_SECURITY, 59, policy(0x00C, 0x00C)}, // PID_SEQUENCE_NUMBER_SENDING
    {OT_SECURITY, 60, policy(0x00C, 0x00C)}, // PID_ZONE_KEY_TABLE
    {OT_SECURITY, 61, policy(0x00C, 0x00C)}, // PID_GO_SECURITY_FLAGS
    {OT_SECURITY, 62, policy(0x15F, 0x04C)}, // PID_ROLE_TABLE
};

// The stack's own properties of the security object (PID 250, the tool
// sequence number) are not in AN193; anything there belongs behind the
// tool key.
const uint32_t SECURITY_UNLISTED = policy(0x00C, 0x00C);

uint8_t rights(uint32_t value, const Caller& caller)
{
    // Offsets within a half: no key at 8, runtime key at 4, tool key at 0;
    // confidentiality two bits above authentication. Without security there
    // is no key, whatever the tool access bit says.
    uint8_t shift = caller.secureMode ? 0 : 10;

    if (caller.security == 0)
        shift += 8;
    else
        shift += (caller.toolAccess ? 0 : 4) + (caller.security >= 2 ? 2 : 0);

    return (uint8_t)((value >> shift) & 0x03);
}

uint32_t propertyPolicy(uint16_t objectType, uint16_t propertyId)
{
    for (const Entry& e : PROPERTIES)
        if (e.objectType == objectType && e.propertyId == propertyId)
            return e.policy;

    if (objectType == OT_SECURITY && propertyId > 2)
        return SECURITY_UNLISTED;

    return STANDARD;
}

bool allowed(uint32_t value, bool write, const Caller& caller)
{
    uint8_t need = write ? WRITE : READ;
    return (rights(value, caller) & need) == need;
}

// AN193 2.2.3: at service level read and write flags are always set alike,
// so any right means access.
bool serviceAllowed(uint32_t value, const Caller& caller)
{
    return rights(value, caller) != 0;
}

// AN193 2.2.4.3
uint32_t restartPolicy(uint8_t restartType, uint8_t eraseCode)
{
    if (restartType == 0)
        return policy(0x3FF, 0x0CC);

    switch (eraseCode)
    {
        case 0x01: return policy(0x3FF, 0x0CC); // confirmed restart
        case 0x02: return policy(0x3FF, 0x00C); // factory reset
        case 0x05: return policy(0x3FF, 0x00C); // reset application program
        case 0x06: return policy(0x3FF, 0x00C); // reset links
        case 0x07: return policy(0x3FF, 0x00C); // factory reset without IA
        default:   return policy(0x3FF, 0x000); // 03h reset IA, and the undefined ones
    }
}

} // namespace

bool property(uint16_t objectType, uint16_t propertyId, bool write, const Caller& caller)
{
    return allowed(propertyPolicy(objectType, propertyId), write, caller);
}

bool propertyDescription(uint16_t objectType, uint16_t propertyId, const Caller& caller)
{
    // AN193 2.2.4.4: the description is readable by whoever may read or
    // write the value.
    uint32_t value = propertyPolicy(objectType, propertyId);
    return rights(value, caller) != 0;
}

Result service(uint16_t apduType, const uint8_t* data, uint16_t length, const Caller& caller,
               uint16_t (*objectTypeOf)(uint8_t index))
{
    auto verdict = [](bool ok) -> Result { return ok ? ALLOWED : DENIED; };

    auto byIndex = [&](bool write) -> Result {
        if (length < 3) return DENIED;
        uint16_t objectType = objectTypeOf ? objectTypeOf(data[1]) : 0xFFFF;
        // An index that names no object reaches no data; let the stack
        // answer it the way it answers any other unknown index.
        if (objectType == 0xFFFF) return ALLOWED;
        return verdict(property(objectType, data[2], write, caller));
    };

    auto byType = [&](bool write) -> Result {
        if (length < 6) return DENIED;
        uint16_t objectType = (uint16_t)((data[1] << 8) | data[2]);
        uint16_t propertyId = (uint16_t)(((data[4] & 0x0F) << 8) | data[5]);
        return verdict(property(objectType, propertyId, write, caller));
    };

    switch (apduType)
    {
        // Data level: the property decides.
        case 0x3D5: return byIndex(false); // PropertyValueRead
        case 0x3D7: return byIndex(true);  // PropertyValueWrite
        case 0x2C7: return byIndex(true);  // FunctionPropertyCommand
        case 0x2C8: return byIndex(false); // FunctionPropertyState
        case 0x1CC: return byType(false);  // PropertyValueExtRead
        case 0x1CE:                        // PropertyValueExtWriteCon
        case 0x1D0: return byType(true);   // PropertyValueExtWriteUnCon
        case 0x1D4: return byType(true);   // FunctionPropertyExtCommand
        case 0x1D5: return byType(false);  // FunctionPropertyExtState

        case 0x3D8: // PropertyDescriptionRead
        {
            // By index (PID 0) the property is not known here; the stack
            // answers with whatever it finds, as it would for anyone.
            if (length < 4 || data[2] == 0) return ALLOWED;
            uint16_t objectType = objectTypeOf ? objectTypeOf(data[1]) : 0xFFFF;
            if (objectType == 0xFFFF) return ALLOWED;
            return propertyDescription(objectType, data[2], caller) ? ALLOWED : DENIED;
        }

        case 0x1D2: // PropertyExtDescriptionRead
        {
            if (length < 6) return DENIED;
            uint16_t objectType = (uint16_t)((data[1] << 8) | data[2]);
            uint16_t propertyId = (uint16_t)(((data[4] & 0x0F) << 8) | data[5]);
            if (propertyId == 0) return ALLOWED;
            return propertyDescription(objectType, propertyId, caller) ? ALLOWED : DENIED;
        }

        // AN193 2.2.4.5: denied means an answer with FFFFh, not silence.
        case 0x300:
            return serviceAllowed(policy(0x3FF, 0x0CC), caller) ? ALLOWED : DESCRIPTOR_VOID;

        // Memory, the filter table included: data level, 3FF/0CC.
        case 0x200: case 0x1FD: case 0x2C0: case 0x2C5:
            return verdict(allowed(STANDARD, false, caller));
        case 0x280: case 0x1FB: case 0x2C2: case 0x3CA: case 0x3C3:
            return verdict(allowed(STANDARD, true, caller));

        case 0x380: // Restart
        case 0x381:
        {
            uint8_t type  = length >= 1 ? (data[0] & 0x3F) : 0;
            uint8_t erase = (type == 1 && length >= 2) ? data[1] : 0;
            return verdict(serviceAllowed(restartPolicy(type, erase), caller));
        }

        // Service level, AN193 2.2.3.
        case 0x0C0: // IndividualAddress_Write
        case 0x3DE: // IndividualAddress_SerialNumber_Write
        case 0x3E0: // DomainAddress_Write
        case 0x180: // ADC_Read
            return verdict(serviceAllowed(policy(0x3FF, 0x00C), caller));

        case 0x3D3: // Key_Write
        case 0x3C0: // Open_Routing_Table
        case 0x3C1: // Read_Routing_Table
            return verdict(serviceAllowed(policy(0x3FF, 0x0CC), caller));

        default:
            // IndividualAddress(_SerialNumber)_Read, DomainAddress reads,
            // Authorize, group services: 3FF/3FF.
            return ALLOWED;
    }
}

} // namespace knxaccess
