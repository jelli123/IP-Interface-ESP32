/*
 *  knx_access_policy.h - Who may use which management service, 3/5/1.
 *
 *  Every management service and every property has an access policy: for
 *  each role (no key, a runtime key, the tool key) and each security level
 *  (none, authentication, authentication + confidentiality) whether it may
 *  read and whether it may write - once for secure mode off, once for secure
 *  mode on. The stack decrypts S-A_Data and passes on what security it found,
 *  but never looked at it; scripts/patch_knx.py (patch 23) asks here.
 *
 *  Source: AN193 "Access Policies" v04 and 03_08_09 "KNX IP Secure" 2.3.1,
 *  both part of the KNX Standard v3.0.
 *
 *  Encoding as in AN158/AN193: two 10 bit halves, "off/on", each
 *
 *      bits 9..8  no key          R W
 *      bits 7..4  runtime key     auth-conf R W, auth R W
 *      bits 3..0  tool key        auth-conf R W, auth R W
 *
 *  so "008/008" means: write with the tool key and confidentiality, nothing
 *  else, whether secure mode is on or not.
 *
 *  With secure mode off only the security material itself is guarded - the
 *  keys, the security object, the IP Secure configuration. Everything else
 *  has 3FF there and behaves exactly as before, so a device ETS programs
 *  without security notices nothing of this. In secure mode the policies of
 *  AN193 apply in full.
 *
 *  Pure logic without Arduino, testable on the host.
 */
#pragma once

#include <stdint.h>

namespace knxaccess
{

/** What the frame arrived with. */
struct Caller
{
    bool    secureMode;   //!< PID_SECURITY_MODE of the security object
    bool    toolAccess;   //!< SCF tool access bit
    uint8_t security;     //!< 0 none, 1 authentication, 2 auth + confidentiality
};

/** What happens to a service. */
enum Result : uint8_t
{
    ALLOWED,
    DENIED,          //!< answered as an inaccessible resource, or dropped
    DESCRIPTOR_VOID, //!< A_DeviceDescriptor_Read: answer FFFFh (type 0) or type 3Fh
};

/** A property service on object type and property id. */
bool property(uint16_t objectType, uint16_t propertyId, bool write, const Caller& caller);

/** A_PropertyDescription_Read: readable for whoever may read or write the value. */
bool propertyDescription(uint16_t objectType, uint16_t propertyId, const Caller& caller);

/**
 * Any application layer service, as the stack hands it to the BAU.
 *
 * @param data          apdu.data(): the APCI's low octet, then the service data
 * @param objectTypeOf  resolves an object index for the services that address
 *                      objects by index; returns 0xFFFF for an unknown index
 */
Result service(uint16_t apduType, const uint8_t* data, uint16_t length, const Caller& caller,
               uint16_t (*objectTypeOf)(uint8_t index));

} // namespace knxaccess
