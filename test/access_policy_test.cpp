/*
 *  access_policy_test.cpp - Host test of the management access policies
 *  against AN193 "Access Policies" v04 and 03_08_09 2.3.1.
 *
 *  The one property that matters most: with secure mode off, a device ETS
 *  programs without security must accept exactly what it accepted before -
 *  only the security material itself is guarded.
 */

#include <stdio.h>

#include "knx_access_policy.h"

using namespace knxaccess;

static int failures = 0;

static void expect(const char* name, bool actual, bool expected)
{
    bool ok = actual == expected;
    printf("%s %s\n", ok ? "ok   " : "FAIL ", name);
    if (!ok) failures++;
}

static void expectResult(const char* name, Result actual, Result expected)
{
    expect(name, actual == expected, true);
}

static uint16_t objectTypeOf(uint8_t index)
{
    // Object list of the 091A BAU: device, router, application, IP, security, cEMI.
    static const uint16_t types[] = {0, 6, 3, 11, 17, 8};
    return index < 6 ? types[index] : 0xFFFF;
}

int main()
{
    const Caller plainOff{false, false, 0};
    const Caller plainOn{true, false, 0};
    const Caller toolOff{false, true, 2};
    const Caller toolOn{true, true, 2};
    const Caller toolAuthOn{true, true, 1};
    const Caller runtimeOn{true, false, 2};

    // Secure mode off: everything as before...
    expect("off: plain write router load control", property(6, 5, true, plainOff), true);
    expect("off: plain write filter table use", property(6, 67, true, plainOff), true);
    expect("off: plain write IP address", property(11, 60, true, plainOff), true);
    expect("off: plain write tunnel addresses", property(11, 53, true, plainOff), true);
    expect("off: plain read serial number", property(0, 11, false, plainOff), true);
    expect("off: plain read object type", property(0, 1, false, plainOff), true);

    // ...except the security material (AN193 2.2.4.6, 03_08_09 2.3.1).
    expect("off: plain write backbone key", property(11, 91, true, plainOff), false);
    expect("off: tool write backbone key", property(11, 91, true, toolOff), true);
    expect("off: tool read backbone key", property(11, 91, false, toolOff), false);
    expect("off: plain read tunnelling users (15F)", property(11, 97, false, plainOff), true);
    expect("off: plain write tunnelling users (15F)", property(11, 97, true, plainOff), false);
    expect("off: tool write tunnelling users", property(11, 97, true, toolOff), true);
    expect("off: plain read secured families", property(11, 94, false, plainOff), true);
    expect("off: plain set secured families", property(11, 94, true, plainOff), false);
    expect("off: plain read tunnelling addresses", property(11, 79, false, plainOff), true);
    expect("off: plain write tool key", property(17, 56, true, plainOff), false);
    expect("off: tool write tool key", property(17, 56, true, toolOff), true);
    expect("off: tool read tool key", property(17, 56, false, toolOff), false);
    expect("off: plain security load control write", property(17, 5, true, plainOff), false);
    expect("off: plain security load state read (15F)", property(17, 5, false, plainOff), true);
    expect("off: tool security load control", property(17, 5, true, toolOff), true);
    expect("off: plain read security object type", property(17, 1, false, plainOff), true);
    expect("off: plain read security mode", property(17, 51, false, plainOff), true);
    expect("off: plain set security mode", property(17, 51, true, plainOff), false);
    expect("off: tool set security mode", property(17, 51, true, toolOff), true);
    expect("off: plain read failures log (1FF)", property(17, 55, false, plainOff), true);
    expect("off: stack-only security property, tool", property(17, 250, true, toolOff), true);
    expect("off: stack-only security property, plain", property(17, 250, false, plainOff), false);

    // Secure mode on.
    expect("on: plain read object type (3FF/0CC)", property(0, 1, false, plainOn), false);
    expect("on: plain write router load control", property(6, 5, true, plainOn), false);
    expect("on: tool write router load control", property(6, 5, true, toolOn), true);
    expect("on: tool auth-only write load control", property(6, 5, true, toolAuthOn), false);
    expect("on: plain read max APDU (1FF)", property(0, 56, false, plainOn), true);
    expect("on: plain read routing max APDU (1FF)", property(6, 58, false, plainOn), true);
    expect("on: plain read serial number", property(0, 11, false, plainOn), false);
    expect("on: tool read serial number", property(0, 11, false, toolOn), true);
    expect("on: runtime key write IP address", property(11, 60, true, runtimeOn), true);
    expect("on: runtime key write backbone key", property(11, 91, true, runtimeOn), false);
    expect("on: runtime key read tunnelling users (04C)", property(11, 97, false, runtimeOn), true);
    expect("on: runtime key write tunnelling users (04C)", property(11, 97, true, runtimeOn), false);
    expect("on: tool write secured families", property(11, 94, true, toolOn), true);
    expect("on: plain read device address (00C)", property(0, 58, false, plainOn), false);
    expect("on: runtime key read device address (00C)", property(0, 58, false, runtimeOn), false);
    expect("on: description of tool key, tool", propertyDescription(17, 56, toolOn), true);
    expect("on: description of tool key, runtime key", propertyDescription(17, 56, runtimeOn), false);

    // Services, decoded from the APDU as the stack hands it over.
    uint8_t propWrite[] = {0xD7, 1, 5, 0x10, 0x01, 0x01};  // object 1 (router), PID 5
    expectResult("off: plain A_PropertyValue_Write router PID 5",
                 service(0x3D7, propWrite, sizeof(propWrite), plainOff, objectTypeOf), ALLOWED);
    expectResult("on: plain A_PropertyValue_Write router PID 5",
                 service(0x3D7, propWrite, sizeof(propWrite), plainOn, objectTypeOf), DENIED);

    uint8_t extWrite[] = {0xCE, 0x00, 0x11, 0x00, 0x10, 0x38, 1, 0, 1};  // OT 17 inst 1 PID 56
    expectResult("off: plain A_PropertyExtValue_WriteCon tool key",
                 service(0x1CE, extWrite, sizeof(extWrite), plainOff, objectTypeOf), DENIED);
    expectResult("off: tool A_PropertyExtValue_WriteCon tool key",
                 service(0x1CE, extWrite, sizeof(extWrite), toolOff, objectTypeOf), ALLOWED);

    uint8_t descRead[] = {0xD8, 4, 56, 0};  // object 4 (security), PID 56
    expectResult("off: plain A_PropertyDescription_Read tool key",
                 service(0x3D8, descRead, sizeof(descRead), plainOff, objectTypeOf), DENIED);

    uint8_t none[1] = {0};
    expectResult("off: plain A_IndividualAddress_Write", service(0x0C0, none, 1, plainOff, objectTypeOf), ALLOWED);
    expectResult("on: plain A_IndividualAddress_Write", service(0x0C0, none, 1, plainOn, objectTypeOf), DENIED);
    expectResult("on: tool A_IndividualAddress_Write", service(0x0C0, none, 1, toolOn, objectTypeOf), ALLOWED);
    expectResult("on: plain A_IndividualAddress_Read", service(0x100, none, 1, plainOn, objectTypeOf), ALLOWED);
    expectResult("on: plain A_DeviceDescriptor_Read answered FFFFh",
                 service(0x300, none, 1, plainOn, objectTypeOf), DESCRIPTOR_VOID);
    expectResult("on: tool A_DeviceDescriptor_Read", service(0x300, none, 1, toolOn, objectTypeOf), ALLOWED);
    expectResult("off: plain A_DeviceDescriptor_Read", service(0x300, none, 1, plainOff, objectTypeOf), ALLOWED);

    uint8_t restart[1]      = {0x80};            // basic restart
    uint8_t masterReset2[3] = {0x81, 0x02, 0};   // factory reset
    uint8_t masterReset1[3] = {0x81, 0x01, 0};   // confirmed restart
    uint8_t masterReset3[3] = {0x81, 0x03, 0};   // reset IA
    expectResult("on: plain A_Restart", service(0x380, restart, 1, plainOn, objectTypeOf), DENIED);
    expectResult("on: runtime key A_Restart", service(0x380, restart, 1, runtimeOn, objectTypeOf), ALLOWED);
    expectResult("on: runtime key confirmed restart", service(0x381, masterReset1, 3, runtimeOn, objectTypeOf), ALLOWED);
    expectResult("on: runtime key factory reset", service(0x381, masterReset2, 3, runtimeOn, objectTypeOf), DENIED);
    expectResult("on: tool factory reset", service(0x381, masterReset2, 3, toolOn, objectTypeOf), ALLOWED);
    expectResult("on: tool reset IA (000)", service(0x381, masterReset3, 3, toolOn, objectTypeOf), DENIED);
    expectResult("off: plain factory reset", service(0x381, masterReset2, 3, plainOff, objectTypeOf), ALLOWED);
    expectResult("on: plain A_Memory_Write", service(0x280, none, 1, plainOn, objectTypeOf), DENIED);
    expectResult("off: plain A_Memory_Write", service(0x280, none, 1, plainOff, objectTypeOf), ALLOWED);
    expectResult("on: plain A_Key_Write", service(0x3D3, none, 1, plainOn, objectTypeOf), DENIED);
    expectResult("on: plain group read", service(0x000, none, 1, plainOn, objectTypeOf), ALLOWED);

    printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
