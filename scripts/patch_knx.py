"""Patches six spots in the KNX stack.

1. Tunnel routing for cEMI management responses
-----------------------------------------------

Symptom: the ETS connection manager cannot assign tunnel addresses. It reads
PID_ADDITIONAL_INDIVIDUAL_ADDRESSES, never gets an answer and reports a
timeout - although the device answered.

Cause: IpDataLinkLayer picks the tunnel for a response by matching
frame.destinationAddress(). A cEMI management frame carries no addresses at
all, so that call reads whatever sits at the address offset of an L_Data
frame. For a M_PropRead.con it lands on the start index, which ETS sets to 0
when it asks how many elements the property has. An unused tunnel slot also
holds address 0, so the loop matches a closed channel and the answer is sent
to 0.0.0.0. Visible in the log as a reply without the preceding
"Found config Channel":

    M_PropRead_req: ObjType: 11 ObjInst: 1 PropId: 53 NoE: 1 startIdx: 0
    Send to Channel: 0

Fix: skip slots with ChannelId 0. The group address branches of the same two
functions already do exactly that; only the individual address branches were
missing it.

2. Optional unfiltered routing
------------------------------

RouterObject::isGroupAddressInFilterTable() answers "not in the table" for
every address until an ETS download has set the load state, so an
unprogrammed coupler forwards no group telegram at all. That is correct per
spec, but it makes the device useless as a plain TP-to-IP gateway without a
product database - and it is a useful override even with one, for
commissioning. The firmware exposes it as a setting; here the hook is put in
place.

Both patched here rather than in a fork so that `lib_deps` can keep tracking
upstream. Remove once upstream carries them.
"""

import hashlib
import os
import shutil
import sys

Import("env")  # noqa: F821  (injected by SCons)


def check_patch_stamp():
    """
    Refuse to build against a library patched by a different version of this
    script.

    Every patch below finds its anchor once and replaces it. Afterwards the
    anchor is gone, so a *changed* patch silently does nothing and the build
    keeps using the old instrumentation - which cost a full round of testing
    against a firmware that did not contain the fix under test.

    Comparing a hash of this file against a stamp next to the library catches
    that. The library is removed so the next run starts from a clean copy;
    doing it in-place would not help, because PlatformIO has already resolved
    the dependency for this build.
    """
    knx_dir = os.path.join(
        env["PROJECT_LIBDEPS_DIR"],  # noqa: F821
        env["PIOENV"],  # noqa: F821
        "knx",
    )

    if not os.path.isdir(knx_dir):
        return

    # __file__ does not exist in a SCons script, so take the known location.
    script = os.path.join(env["PROJECT_DIR"], "scripts", "patch_knx.py")  # noqa: F821

    if not os.path.isfile(script):
        return

    with open(script, "rb") as handle:
        digest = hashlib.sha256(handle.read()).hexdigest()

    stamp = os.path.join(knx_dir, ".sbip-patch-stamp")

    if os.path.isfile(stamp):
        with open(stamp, "r", encoding="utf-8") as handle:
            if handle.read().strip() == digest:
                return

        shutil.rmtree(knx_dir, ignore_errors=True)
        sys.stderr.write(
            "\npatch_knx.py: the KNX library was patched by an older version "
            "of this script.\nIt has been removed - run the build again and "
            "PlatformIO will fetch and patch a fresh copy.\n\n"
        )
        env.Exit(1)  # noqa: F821

    with open(stamp, "w", encoding="utf-8") as handle:
        handle.write(digest)


check_patch_stamp()

TARGET = os.path.join(
    env["PROJECT_LIBDEPS_DIR"],  # noqa: F821
    env["PIOENV"],  # noqa: F821
    "knx",
    "src",
    "knx",
    "ip_data_link_layer.cpp",
)

ROUTER = os.path.join(
    env["PROJECT_LIBDEPS_DIR"],  # noqa: F821
    env["PIOENV"],  # noqa: F821
    "knx",
    "src",
    "knx",
    "router_object.cpp",
)

MARKER = "// sbip: only an established channel can receive a response"

LOOP_HEAD = "    {\n"

GUARD = (
    LOOP_HEAD
    + "        " + MARKER + "\n"
    + "        if (tunnels[i].ChannelId == 0)\n"
    + "            continue;\n"
    + "\n"
)

# Both loops open the same way and differ only in which address they compare
# first, so each anchor stays unique within the file.
ANCHORS = (
    LOOP_HEAD + "        if (tunnels[i].IndividualAddress == frame.sourceAddress())",
    LOOP_HEAD + "        if (tunnels[i].IndividualAddress == frame.destinationAddress())",
)


def patch():
    if not os.path.isfile(TARGET):
        # Nothing to do for an environment that pulls no KNX stack.
        return

    with open(TARGET, "r", encoding="utf-8") as handle:
        source = handle.read()

    if MARKER in source:
        return

    patched = source
    for anchor in ANCHORS:
        if patched.count(anchor) != 1:
            sys.stderr.write(
                "patch_knx.py: anchor no longer unique, tunnel routing fix NOT "
                "applied - check whether upstream fixed it:\n  %s\n" % anchor
            )
            return
        patched = patched.replace(anchor, GUARD + anchor[len(LOOP_HEAD):])

    with open(TARGET, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(patched)

    print("patch_knx.py: tunnel routing fix applied to ip_data_link_layer.cpp")


# --------------------------------------------------------------------------
# 2. Optional unfiltered routing
# --------------------------------------------------------------------------

ROUTER_MARKER = "// sbip: firmware override, see KnxLink::routeUnfiltered()"

ROUTER_OLD = (
    "bool RouterObject::isGroupAddressInFilterTable(uint16_t groupAddress)\n"
    "{\n"
    "    if (loadState() != LS_LOADED)\n"
    "        return false;\n"
)

ROUTER_NEW = (
    ROUTER_MARKER + "\n"
    "bool sbipRouteUnfiltered = false;\n"
    "\n"
    "bool RouterObject::isGroupAddressInFilterTable(uint16_t groupAddress)\n"
    "{\n"
    "    // Deliberately ahead of the load state check: the switch means\n"
    "    // \"forward everything\", and a downloaded filter table is exactly the\n"
    "    // case where the user needs to be able to say that. Without an ETS\n"
    "    // download there is no table at all, which per spec would block every\n"
    "    // group telegram - the same switch covers that too.\n"
    "    if (sbipRouteUnfiltered)\n"
    "        return true;\n"
    "\n"
    "    if (loadState() != LS_LOADED)\n"
    "        return false;\n"
)


def patch_router():
    if not os.path.isfile(ROUTER):
        return

    with open(ROUTER, "r", encoding="utf-8") as handle:
        source = handle.read()

    if ROUTER_MARKER in source:
        return

    if source.count(ROUTER_OLD) != 1:
        sys.stderr.write(
            "patch_knx.py: filter table guard not found, unfiltered routing NOT "
            "available - the setting will have no effect\n"
        )
        return

    with open(ROUTER, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(source.replace(ROUTER_OLD, ROUTER_NEW))

    print("patch_knx.py: unfiltered routing hook applied to router_object.cpp")


patch()
patch_router()


# --------------------------------------------------------------------------
# 3. Manufacturer specific properties a foreign load procedure may write
# --------------------------------------------------------------------------

IPPARAM = os.path.join(
    env["PROJECT_LIBDEPS_DIR"],  # noqa: F821
    env["PIOENV"],  # noqa: F821
    "knx",
    "src",
    "knx",
    "ip_parameter_object.cpp",
)

IPPARAM_MARKER = ("// sbip: manufacturer specific, written by some products' "
                  "load procedures")

IPPARAM_ANCHOR = (
    "        new DataProperty(PID_IP_ASSIGNMENT_METHOD, true, PDT_UNSIGNED_CHAR, 1,"
    " ReadLv3 | WriteLv3),\n"
)

# Property ids from 200 up are the manufacturer's own by definition, and a
# product's load procedure may write them:
#     <LdCtrlWriteProp ObjType="11" PropId="204" Verify="false" />
#     <LdCtrlWriteProp ObjType="11" PropId="209" Verify="false" />
# Such a write goes to a property the stack does not have, the device answers
# with zero elements and the download stops there. 204 and 209 are the two
# that turned up on mask 091A routers, so they exist here - what goes in them
# is nobody's business but the vendor's, and Verify="false" means ETS never
# reads them back. Declared as byte arrays so any length fits.
#
# This is what the knxprod check in the dashboard reports on: it names every
# property a load procedure writes that this stack does not have, before the
# download runs into it. Nothing here emulates a particular product - the
# identity for that comes from the knxprod the user loads, see
# src/knx_identity.cpp.
IPPARAM_EXTRA = (
    "        " + IPPARAM_MARKER + "\n"
    "        new DataProperty((PropertyID)204, true, PDT_UNSIGNED_CHAR, 16,"
    " ReadLv3 | WriteLv3),\n"
    "        new DataProperty((PropertyID)209, true, PDT_UNSIGNED_CHAR, 16,"
    " ReadLv3 | WriteLv3),\n"
)


def patch_ipparam():
    if not os.path.isfile(IPPARAM):
        return

    with open(IPPARAM, "r", encoding="utf-8") as handle:
        source = handle.read()

    # Checked on the code, not on the comment: a copy patched by an older
    # revision of this script carries the properties but an older marker, and
    # inserting them a second time is a silent duplicate.
    if "(PropertyID)204" in source:
        return

    if source.count(IPPARAM_ANCHOR) != 1:
        sys.stderr.write(
            "patch_knx.py: anchor for the manufacturer properties not found, "
            "a download that writes ObjType 11 PropId 204/209 will fail\n"
        )
        return

    with open(IPPARAM, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(source.replace(IPPARAM_ANCHOR, IPPARAM_ANCHOR + IPPARAM_EXTRA))

    print("patch_knx.py: manufacturer properties added to ip_parameter_object.cpp")


patch_ipparam()


# --------------------------------------------------------------------------
# 4. Uninitialised IpParameterObject pointer in DataLinkLayer
# --------------------------------------------------------------------------

DLL_H = os.path.join(
    env["PROJECT_LIBDEPS_DIR"],  # noqa: F821
    env["PIOENV"],  # noqa: F821
    "knx", "src", "knx", "data_link_layer.h",
)

DLL_C = os.path.join(
    env["PROJECT_LIBDEPS_DIR"],  # noqa: F821
    env["PIOENV"],  # noqa: F821
    "knx", "src", "knx", "data_link_layer.cpp",
)

DLL_MARKER = "// sbip: never assigned by the library, see scripts/patch_knx.py"

DLL_H_OLD = "        IpParameterObject* _ipParameters;"
DLL_H_NEW = (
    "        " + DLL_MARKER + "\n"
    "        IpParameterObject* _ipParameters = nullptr;"
)

DLL_C_OLD = (
    "    uint8_t numAddresses = 0;\n"
    "    uint16_t* addresses = _ipParameters->additionalIndivualAddresses(numAddresses);\n"
)
DLL_C_NEW = (
    "    // sbip: DataLinkLayer::_ipParameters is declared, never assigned and\n"
    "    // shadowed by a reference of the same name in IpDataLinkLayer. The\n"
    "    // setter ipParameterObject() has no definition and no caller, so this\n"
    "    // dereferenced a wild pointer as soon as the TP layer forwarded a\n"
    "    // unicast frame - LoadProhibited in InterfaceObject::property().\n"
    "    // Answering \"not a tunnel address\" only disables an optimisation:\n"
    "    // the frame goes to TP1 as it would without KNX_TUNNELING.\n"
    "    if (_ipParameters == nullptr)\n"
    "        return false;\n"
    "\n"
    "    uint8_t numAddresses = 0;\n"
    "    uint16_t* addresses = _ipParameters->additionalIndivualAddresses(numAddresses);\n"
)


def patch_datalinklayer():
    for path, old, new in ((DLL_H, DLL_H_OLD, DLL_H_NEW),
                           (DLL_C, DLL_C_OLD, DLL_C_NEW)):
        if not os.path.isfile(path):
            return

        with open(path, "r", encoding="utf-8") as handle:
            source = handle.read()

        if DLL_MARKER in source or "sbip: DataLinkLayer::_ipParameters" in source:
            continue

        if source.count(old) != 1:
            sys.stderr.write(
                "patch_knx.py: anchor not found in %s, the wild pointer in "
                "isTunnelingPA() is NOT guarded\n" % os.path.basename(path)
            )
            return

        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(source.replace(old, new))

        print("patch_knx.py: null guard applied to %s" % os.path.basename(path))


patch_datalinklayer()


# --------------------------------------------------------------------------
# 5. Tunnel addresses read from a buffer that has already gone out of scope
# --------------------------------------------------------------------------

SCOPE_MARKER = "// sbip: addrbuffer dies with this block"

SCOPE_OLD = (
    "        uint8_t count = KNX_TUNNELING;\n"
    "        _ipParameters.writeProperty(PID_ADDITIONAL_INDIVIDUAL_ADDRESSES, 1,"
    " addrbuffer, count);\n"
)

SCOPE_NEW = (
    SCOPE_OLD
    + "\n"
    + "        " + SCOPE_MARKER + ", but `addresses` is read\n"
    "        // after it. The values just went into the property, so point at\n"
    "        // its own storage rather than at a dead stack frame.\n"
    "        addresses = _ipParameters.propertyData(PID_ADDITIONAL_INDIVIDUAL_ADDRESSES);\n"
)


def patch_scope():
    if not os.path.isfile(TARGET):
        return

    with open(TARGET, "r", encoding="utf-8") as handle:
        source = handle.read()

    if SCOPE_MARKER in source:
        return

    if source.count(SCOPE_OLD) != 1:
        sys.stderr.write(
            "patch_knx.py: anchor for the tunnel address buffer not found, the "
            "use-after-scope on the first connection is NOT fixed\n"
        )
        return

    with open(TARGET, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(source.replace(SCOPE_OLD, SCOPE_NEW))

    print("patch_knx.py: use-after-scope fixed in ip_data_link_layer.cpp")


patch_scope()


# --------------------------------------------------------------------------
# 6. Bus monitor hook
# --------------------------------------------------------------------------
#
# The stack has no way to hand a telegram out. KNX_ACTIVITYCALLBACK comes
# closest, but its callback carries a direction and a network index and no
# frame at all - enough to count bus load, not to show one.
#
# Three call sites cover everything that crosses a medium:
#
#   frameReceived()         one frame in, on whichever layer received it
#   sendTelegram()          one frame out, just before it reaches the medium
#   dataRequestFromTunnel() a frame handed to us over a tunnel
#
# The third needs the suppression flag: it calls frameReceived() for local
# delivery, which without it would log an IP-side telegram as a TP reception.
#
# Both symbols are defined in src/bus_monitor.cpp, not here. A patch that no
# longer applies then costs the monitor its input, not the build - and the
# CPPDEFINES below tells the firmware which of the two happened.

MON_MARKER = "// sbip: bus monitor, defined in src/bus_monitor.cpp"

MON_DECL = (
    MON_MARKER + "\n"
    "extern void (*sbipMonitorHook)(uint8_t side, bool outgoing,\n"
    "                               const uint8_t* cemi, uint16_t length);\n"
    "extern bool sbipMonitorSuppress;\n"
    "\n"
)

MON_ANCHOR_DECL = "void DataLinkLayerCallbacks::activity(uint8_t info)\n"

MON_ANCHOR_RX = (
    "void DataLinkLayer::frameReceived(CemiFrame& frame)\n"
    "{\n"
    "    AckType ack = frame.ack();\n"
)

MON_NEW_RX = (
    "void DataLinkLayer::frameReceived(CemiFrame& frame)\n"
    "{\n"
    "    if (sbipMonitorHook && !sbipMonitorSuppress)\n"
    "        sbipMonitorHook(_networkLayerEntity.getEntityIndex(), false,\n"
    "                        frame.data(), frame.totalLenght());\n"
    "\n"
    "    AckType ack = frame.ack();\n"
)

MON_ANCHOR_TX = (
    "    if (sendTheFrame)\n"
    "        success = sendFrame(frame);\n"
)

MON_NEW_TX = (
    "    if (sendTheFrame)\n"
    "    {\n"
    "        if (sbipMonitorHook)\n"
    "            sbipMonitorHook(_networkLayerEntity.getEntityIndex(), true,\n"
    "                            frame.data(), frame.totalLenght());\n"
    "\n"
    "        success = sendFrame(frame);\n"
    "    }\n"
)

MON_ANCHOR_TUNNEL = (
    "    // Send to local stack ( => cemiServer for potential other tunnel and"
    " network layer for routing)\n"
    "    frameReceived(frame);\n"
)

MON_NEW_TUNNEL = (
    "    // sbip: side 2 = handed over by a tunnel client. Neither IP nor TP\n"
    "    // is honest here - it arrives over IP but is delivered on the layer\n"
    "    // the cEMI server sits on, which bau091A.cpp sets to the TP one.\n"
    "    if (sbipMonitorHook)\n"
    "        sbipMonitorHook(2, false, frame.data(), frame.totalLenght());\n"
    "\n"
    "    // Send to local stack ( => cemiServer for potential other tunnel and"
    " network layer for routing)\n"
    "    sbipMonitorSuppress = true;\n"
    "    frameReceived(frame);\n"
    "    sbipMonitorSuppress = false;\n"
)

MON_ANCHOR_TUN_TX = (
    "    _platform.sendBytesUniCast(tunnel->IpAddress, tunnel->PortData,"
    " req.data(), req.totalLength());\n"
)

MON_NEW_TUN_TX = (
    "    // sbip: side 2 = tunnel. Without this the monitor shows everything\n"
    "    // the coupler emits but not what actually reaches a tunnel client,\n"
    "    // which is the one thing that matters when ETS sees nothing.\n"
    "    if (sbipMonitorHook)\n"
    "        sbipMonitorHook(2, true, frame.data(), frame.totalLenght());\n"
    "\n"
    "    if (!_platform.sendBytesUniCast(tunnel->IpAddress, tunnel->PortData,"
    " req.data(), req.totalLength()))\n"
    "        println(\"sbip: sending to the tunnel failed\");\n"
)

MON_EDITS = (
    (MON_ANCHOR_DECL, MON_DECL + MON_ANCHOR_DECL),
    (MON_ANCHOR_RX, MON_NEW_RX),
    (MON_ANCHOR_TX, MON_NEW_TX),
    (MON_ANCHOR_TUNNEL, MON_NEW_TUNNEL),
)


def patch_monitor():
    """@return True when the firmware may rely on the hook."""
    if not os.path.isfile(DLL_C):
        return False

    with open(DLL_C, "r", encoding="utf-8") as handle:
        source = handle.read()

    if MON_MARKER in source:
        return True

    patched = source
    for anchor, replacement in MON_EDITS:
        if patched.count(anchor) != 1:
            sys.stderr.write(
                "patch_knx.py: anchor no longer unique, the bus monitor gets NO "
                "telegrams - check whether upstream changed data_link_layer.cpp:"
                "\n  %s\n" % anchor.strip().splitlines()[0]
            )
            return False
        patched = patched.replace(anchor, replacement)

    with open(DLL_C, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(patched)

    print("patch_knx.py: bus monitor hook applied to data_link_layer.cpp")
    return True


def patch_monitor_tunnel_tx():
    """The one direction data_link_layer.cpp cannot see."""
    if not os.path.isfile(TARGET):
        return

    with open(TARGET, "r", encoding="utf-8") as handle:
        source = handle.read()

    if "sbip: side 2 = tunnel" in source:
        return

    if source.count(MON_ANCHOR_TUN_TX) != 1:
        sys.stderr.write(
            "patch_knx.py: anchor no longer unique, telegrams sent to a tunnel "
            "stay invisible in the bus monitor:\n  %s\n"
            % MON_ANCHOR_TUN_TX.strip()
        )
        return

    patched = source.replace(MON_ANCHOR_TUN_TX, MON_NEW_TUN_TX)

    # The declaration lives in data_link_layer.cpp; this file needs its own.
    patched = patched.replace(
        "void IpDataLinkLayer::sendFrameToTunnel(",
        MON_DECL + "void IpDataLinkLayer::sendFrameToTunnel(",
        1,
    )

    # Say why the ETS bus monitor stays empty. The stack only speaks
    # TUNNEL_LINKLAYER and turns down a busmonitor connection - silently,
    # because the explanation sits behind KNX_LOG_TUNNELING.
    layer_anchor = (
        "        //We only support 0x02!\n"
        "#ifdef KNX_LOG_TUNNELING\n"
        '        println("Only LinkLayer ist supported!");\n'
        "#endif\n"
    )

    if patched.count(layer_anchor) == 1:
        patched = patched.replace(
            layer_anchor,
            "        //We only support 0x02!\n"
            '        println("sbip: tunnel refused - only LinkLayer is '
            'supported, the ETS bus monitor cannot work over this device");\n',
        )

    # A reply that finds no tunnel is dropped without a word, which is
    # indistinguishable from never having been generated. Same treatment.
    patched = with_drop_log(patched)

    with open(TARGET, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(patched)

    print("patch_knx.py: tunnel transmit hook applied to ip_data_link_layer.cpp")


DROP_MARKER = "sbip: no tunnel to take the "

DROP_ANCHOR = (
    "    if (tun == nullptr)\n"
    "    {\n"
    "#ifdef KNX_LOG_TUNNELING\n"
    '        print("Found no Tunnel for IA: ");\n'
    "        println(frame.destinationAddress(), 16);\n"
    "#endif\n"
    "        return;\n"
    "    }\n"
)

# The same block sits in dataRequestToTunnel, dataConfirmationToTunnel and
# dataIndicationToTunnel, in that order. Naming them apart is the whole point:
# a missing confirmation and a missing reply look identical from outside but
# mean very different things.
DROP_KINDS = ("request", "confirmation", "indication")


def print_address(expression):
    """Three prints, because the stack has no formatting helper."""
    return (
        "        print(" + expression + " >> 12);\n"
        '        print(".");\n'
        "        print((" + expression + " >> 8) & 0x0F);\n"
        '        print(".");\n'
        "        print(" + expression + " & 0xFF);\n"
    )


def drop_block(kind):
    return (
        "    if (tun == nullptr)\n"
        "    {\n"
        "#ifdef SBIP_KNX_TRACE\n"
        '        print("' + DROP_MARKER + kind + ' for ");\n'
        + print_address("frame.sourceAddress()") +
        '        print(" -> ");\n'
        + print_address("frame.destinationAddress()") +
        '        println(" - dropped");\n'
        "#endif\n"
        "        return;\n"
        "    }\n"
    )


# --------------------------------------------------------------------------
# Why a reply never reaches the tunnel client
# --------------------------------------------------------------------------
#
# Measured: a reply to a point-to-point request is generated (it appears as
# TP;TX in the bus monitor) but never produces a Tunnel;TX line, so
# sendFrameToTunnel() is not reached. The loop below is the only thing
# between the two, and every one of its three conditions looks like it should
# match. One of the values must therefore be different from what the log
# suggests - so print them rather than reason about them.

TRACE_MARKER = "sbip: mirror "

TRACE_ANCHOR = (
    "    KnxIpTunnelConnection* tun = nullptr;\n"
    "\n"
    "    for (int i = 0; i < KNX_TUNNELING; i++)\n"
    "    {\n"
    "        if (tunnels[i].ChannelId == 0 || tunnels[i].IndividualAddress =="
    " frame.sourceAddress())\n"
    "            continue;\n"
)

TRACE_NEW = (
    "    KnxIpTunnelConnection* tun = nullptr;\n"
    "\n"
    "#ifdef SBIP_KNX_TRACE\n"
    '    print("' + TRACE_MARKER + '");\n'
    + print_address("frame.sourceAddress()") +
    '    print(" -> ");\n'
    + print_address("frame.destinationAddress()") +
    '    println(frame.addressType() == AddressType::GroupAddress'
    ' ? " (group)" : " (individual)");\n'
    "\n"
    "    for (int i = 0; i < KNX_TUNNELING; i++)\n"
    "    {\n"
    '        print("  slot ");\n'
    "        print(i);\n"
    '        print(" channel 0x");\n'
    "        print(tunnels[i].ChannelId, 16);\n"
    '        print(" address ");\n'
    + print_address("tunnels[i].IndividualAddress") +
    '        println(tunnels[i].IsConfig ? " config" : " data");\n'
    "    }\n"
    "#endif\n"
    "\n"
    "    for (int i = 0; i < KNX_TUNNELING; i++)\n"
    "    {\n"
    "        if (tunnels[i].ChannelId == 0 || tunnels[i].IndividualAddress =="
    " frame.sourceAddress())\n"
    "            continue;\n"
)


def with_drop_log(source):
    if DROP_MARKER in source or DROP_ANCHOR not in source:
        return source

    for kind in DROP_KINDS:
        source = source.replace(DROP_ANCHOR, drop_block(kind), 1)

    return source


def with_mirror_trace(source):
    # Only in dataIndicationToTunnel - that is the one with this exact loop
    # head, and the only one that carries a reply to a tunnel client.
    if TRACE_MARKER in source or source.count(TRACE_ANCHOR) != 1:
        return source
    return source.replace(TRACE_ANCHOR, TRACE_NEW)


def patch_tunnel_drop_log():
    """Runs on its own, so an already patched file still gets it."""
    if not os.path.isfile(TARGET):
        return

    with open(TARGET, "r", encoding="utf-8") as handle:
        source = handle.read()

    patched = with_mirror_trace(with_drop_log(source))

    if patched == source:
        return

    with open(TARGET, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(patched)

    print("patch_knx.py: KNX trace points added to ip_data_link_layer.cpp")


MONITOR_OK = patch_monitor()

if MONITOR_OK:
    patch_monitor_tunnel_tx()
    patch_tunnel_drop_log()
    env.Append(CPPDEFINES=["SBIP_MONITOR_HOOK"])  # noqa: F821


# --------------------------------------------------------------------------
# 7. Ignore routing indications that came from us
# --------------------------------------------------------------------------
#
# Symptom: ETS refuses to program the individual address with "more than one
# device in programming mode", although only this one device has it enabled.
#
# Measured with the bus monitor: the device sends its IndividualAddress_
# Response with hop count 6, and some 80 ms later reads the very same frame
# back in over the multicast socket - with hop count 4. Two decrements means
# it travelled through couplers on the way, so this is not a plain socket
# loopback. The coupler then routes our own answer on to TP, where ETS sees
# it a second time.
#
# Two independent signs that a frame is ours, both checked here:
#
#   the sender IP is our own          - a real loopback
#   the source address is our own PA  - it went around and came back
#
# Deliberately confined to the multicast receive path. An earlier attempt put
# the same test into DataLinkLayer::frameReceived(), which also covers the
# tunnel path - and a tunnelled frame has to be delivered locally no matter
# what address it carries, or ETS loses the device entirely.
#
# Byte order matters: readBytesMultiCast() runs the sender through htonl(),
# while currentIpAddress() passes on the network order that Arduino's
# IPAddress carries. Comparing them raw would never match.
#
# sbipLoopHook is defined in src/knx_link.cpp; it names the sender in the log
# so the second coupler can be found rather than guessed at.

LOOP_MARKER = "// sbip: this frame is ours - it must not go round again"

LOOP_DECL = (
    "// sbip: routing loop reporter, defined in src/knx_link.cpp\n"
    "extern void (*sbipLoopHook)(uint32_t fromIp, uint16_t source, bool ownIp);\n"
    "\n"
)

LOOP_ANCHOR_DECL = "void IpDataLinkLayer::loop()\n"

LOOP_ANCHOR = (
    "        case RoutingIndication:\n"
    "        {\n"
    "            KnxIpRoutingIndication routingIndication(buffer, len);\n"
    "            frameReceived(routingIndication.frame());\n"
    "            break;\n"
    "        }\n"
)

LOOP_NEW = (
    "        case RoutingIndication:\n"
    "        {\n"
    "            KnxIpRoutingIndication routingIndication(buffer, len);\n"
    "\n"
    "            " + LOOP_MARKER + "\n"
    "            uint32_t sbipOwnIp = _platform.currentIpAddress();\n"
    "            uint32_t sbipOwnSwapped =\n"
    "                ((sbipOwnIp & 0xFF) << 24) | ((sbipOwnIp & 0xFF00) << 8) |\n"
    "                ((sbipOwnIp >> 8) & 0xFF00) | ((sbipOwnIp >> 24) & 0xFF);\n"
    "            bool sbipFromOwnIp = (sbipOwnIp != 0) &&"
    " (remoteAddr == sbipOwnSwapped);\n"
    "            bool sbipFromOwnPa =\n"
    "                routingIndication.frame().sourceAddress() ==\n"
    "                _deviceObject.individualAddress();\n"
    "\n"
    "            if (sbipFromOwnIp || sbipFromOwnPa)\n"
    "            {\n"
    "                if (sbipLoopHook)\n"
    "                    sbipLoopHook(remoteAddr,\n"
    "                                 routingIndication.frame().sourceAddress(),\n"
    "                                 sbipFromOwnIp);\n"
    "\n"
    "                break;\n"
    "            }\n"
    "\n"
    "            frameReceived(routingIndication.frame());\n"
    "            break;\n"
    "        }\n"
)


def patch_loopback():
    if not os.path.isfile(TARGET):
        return

    with open(TARGET, "r", encoding="utf-8") as handle:
        source = handle.read()

    if LOOP_MARKER in source:
        return

    for anchor in (LOOP_ANCHOR_DECL, LOOP_ANCHOR):
        if source.count(anchor) != 1:
            sys.stderr.write(
                "patch_knx.py: anchor no longer unique, the routing loop guard "
                "is NOT applied - ETS may report more than one device in "
                "programming mode:\n  %s\n" % anchor.strip().splitlines()[0]
            )
            return

    patched = source.replace(LOOP_ANCHOR_DECL, LOOP_DECL + LOOP_ANCHOR_DECL)
    patched = patched.replace(LOOP_ANCHOR, LOOP_NEW)

    with open(TARGET, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(patched)

    print("patch_knx.py: routing loop guard applied to ip_data_link_layer.cpp")


patch_loopback()


# --------------------------------------------------------------------------
# 8. Ask the IP layer which addresses belong to a tunnel  -  NOT APPLIED
# --------------------------------------------------------------------------
#
# Kept as a record, deliberately switched off at the bottom of this block.
#
# Patch 4 stopped isTunnelingPA() from dereferencing a wild pointer by having
# it answer "no", which sends a reply for a tunnel client onto the TP line:
#
#     IP;RX;1.1.4;1.1.0;DeviceDescriptorRead
#     TP;TX;1.1.0;1.1.4;DeviceDescriptorResponse   <- onto the bus
#
# Answering truthfully instead makes sendTelegram() skip the TP line, on the
# assumption that the mirror into the tunnel further down would carry it. It
# does not - measured, the reply then appears nowhere at all:
#
#     IP;RX;1.1.4;1.1.0;DeviceDescriptorRead
#     (nothing)
#
# So the mirror is broken independently of this, and closing the TP path only
# removes the one route that happened to work: on this installation a second
# interface on the line picked the frame up and carried it back to ETS. Until
# the mirror is understood, the wrong path is better than no path.
#
# Do not re-enable without checking that a reply to a tunnel client actually
# produces a "Tunnel;TX" line in the bus monitor.

TUNPA_MARKER = "// sbip: ask the IP layer, which knows the open connections"

TUNPA_SRV_H_ANCHOR = "        uint16_t clientAddress() const;\n"

TUNPA_SRV_H_NEW = (
    "        uint16_t clientAddress() const;\n"
    "        // sbip: reaches IpDataLinkLayer::isTunnelAddress()\n"
    "        bool isTunnelAddress(uint16_t pa);\n"
)

TUNPA_SRV_C_ANCHOR = "void CemiServer::dataIndicationToTunnel(CemiFrame& frame)\n"

TUNPA_SRV_C_NEW = (
    "bool CemiServer::isTunnelAddress(uint16_t pa)\n"
    "{\n"
    "    // sbip: the primary layer is the IP one, and only it keeps the\n"
    "    // table of open tunnel connections.\n"
    "    return (_dataLinkLayerPrimary != nullptr) &&\n"
    "           _dataLinkLayerPrimary->isTunnelAddress(pa);\n"
    "}\n"
    "\n"
    "void CemiServer::dataIndicationToTunnel(CemiFrame& frame)\n"
)

TUNPA_DLL_ANCHOR = (
    "    if (_ipParameters == nullptr)\n"
    "        return false;\n"
)

TUNPA_DLL_NEW = (
    "    if (_ipParameters == nullptr)\n"
    "    {\n"
    "        " + TUNPA_MARKER + "\n"
    "        // Answering \"no\" here sends replies for a tunnel client onto\n"
    "        // the TP line instead, where ETS never sees them.\n"
    "        return (_cemiServer != nullptr) && _cemiServer->isTunnelAddress(pa);\n"
    "    }\n"
)


def patch_tunnel_pa():
    knx_dir  = os.path.dirname(DLL_C)
    server_h = os.path.join(knx_dir, "cemi_server.h")
    server_c = os.path.join(knx_dir, "cemi_server.cpp")

    for path in (server_h, server_c, DLL_C):
        if not os.path.isfile(path):
            return

    with open(DLL_C, "r", encoding="utf-8") as handle:
        if TUNPA_MARKER in handle.read():
            return

    edits = ((server_h, TUNPA_SRV_H_ANCHOR, TUNPA_SRV_H_NEW),
             (server_c, TUNPA_SRV_C_ANCHOR, TUNPA_SRV_C_NEW),
             (DLL_C,    TUNPA_DLL_ANCHOR,   TUNPA_DLL_NEW))

    for path, anchor, _ in edits:
        with open(path, "r", encoding="utf-8") as handle:
            if handle.read().count(anchor) != 1:
                sys.stderr.write(
                    "patch_knx.py: anchor no longer unique, replies to a "
                    "tunnel client go to TP instead of the tunnel:\n  %s\n"
                    % anchor.strip().splitlines()[0]
                )
                return

    for path, anchor, replacement in edits:
        with open(path, "r", encoding="utf-8") as handle:
            source = handle.read()
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(source.replace(anchor, replacement))

    print("patch_knx.py: tunnel address lookup restored")


# Switched off, see the block above.
# patch_tunnel_pa()


# --------------------------------------------------------------------------
# 9. Retry a UDP frame the WiFi stack refused
# --------------------------------------------------------------------------
#
# Found by an observation that makes no sense for a logic error: with the
# trace build the ETS can write the individual address and download the
# application, without it the same operations fail. Tracing slows the send
# path down, and that is the whole difference.
#
# Esp32Platform::sendBytesUniCast() reported a failed endPacket() and then
# returned true regardless:
#
#     if (_udp.endPacket() == 0)
#         println("sendBytesUniCast endPacket fail");
#     ...
#     return true;
#
# So a frame the WiFi driver refused - its transmit buffers run dry when
# telegrams follow each other closely - was silently gone, and no caller
# could react. The message does appear in the logs of this project, right
# after the radio comes up and, less visibly, in the middle of a download.
#
# Retrying is enough. The pause stays below the TP-UART's acknowledge window
# (1.4 ms) because this runs on the main task, next to knx.loop() - and it
# only ever happens on the failing path.

UDP_MARKER = "// sbip: the WiFi transmit buffers run dry under a burst"

UDP_ANCHOR = (
    "    if (_udp.beginPacket(ucastaddr, port) == 1)\n"
    "    {\n"
    "        _udp.write(buffer, len);\n"
    "\n"
    "        if (_udp.endPacket() == 0)\n"
    '            println("sendBytesUniCast endPacket fail");\n'
    "    }\n"
    "    else\n"
    '        println("sendBytesUniCast beginPacket fail");\n'
    "\n"
    "    return true;\n"
)

UDP_NEW = (
    "    " + UDP_MARKER + "\n"
    "    // and endPacket() then fails. Returning true regardless lost the\n"
    "    // frame without anyone noticing - a tunnel client just never got\n"
    "    // its answer.\n"
    "    for (uint8_t sbipTry = 0; sbipTry < 3; sbipTry++)\n"
    "    {\n"
    "        if (sbipTry > 0)\n"
    "            delayMicroseconds(400);\n"
    "\n"
    "        if (_udp.beginPacket(ucastaddr, port) != 1)\n"
    "            continue;\n"
    "\n"
    "        _udp.write(buffer, len);\n"
    "\n"
    "        if (_udp.endPacket() != 0)\n"
    "            return true;\n"
    "    }\n"
    "\n"
    '    println("sbip: a unicast frame was refused three times and is lost");\n'
    "    return false;\n"
)


def patch_udp_retry():
    platform_c = os.path.join(
        env["PROJECT_LIBDEPS_DIR"],  # noqa: F821
        env["PIOENV"],  # noqa: F821
        "knx",
        "src",
        "esp32_platform.cpp",
    )

    if not os.path.isfile(platform_c):
        return

    with open(platform_c, "r", encoding="utf-8") as handle:
        source = handle.read()

    if UDP_MARKER in source:
        return

    if source.count(UDP_ANCHOR) != 1:
        sys.stderr.write(
            "patch_knx.py: anchor no longer unique, a UDP frame the WiFi "
            "driver refuses stays lost - tunnel clients will miss answers "
            "under load:\n  %s\n" % UDP_ANCHOR.strip().splitlines()[0]
        )
        return

    with open(platform_c, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(source.replace(UDP_ANCHOR, UDP_NEW))

    print("patch_knx.py: UDP unicast retry applied to esp32_platform.cpp")


patch_udp_retry()


# --------------------------------------------------------------------------
# 9b. The same for the routing multicast
# --------------------------------------------------------------------------
#
# Patch 9 covered the tunnel. sendBytesMultiCast() had the same flaw in an
# even quieter form - it did not look at endPacket() at all:
#
#     _udp.beginMulticastPacket();
#     _udp.write(buffer, len);
#     _udp.endPacket();
#     return true;
#
# Found with ETS downloading over KNXnet/IP routing while the dashboard's bus
# monitor was open. The monitor streamed its window every one and a half
# seconds, that filled the transmit buffers, and the answers to ETS were
# refused by sendto(). The bus monitor still listed them as sent - its hook
# runs before sendFrame() - and the refusal went to log_e() on the serial
# port only, so nothing in the dashboard showed it. ETS waited for an answer
# that never left and gave up: "device not reachable".
#
# Same remedy, same pause, and the loss now reaches the log.

MCAST_MARKER = "// sbip: the multicast is refused under a burst as well"

MCAST_ANCHOR = (
    "bool Esp32Platform::sendBytesMultiCast(uint8_t* buffer, uint16_t len)\n"
    "{\n"
    "    //printHex(\"<- \",buffer, len);\n"
    "    _udp.beginMulticastPacket();\n"
    "    _udp.write(buffer, len);\n"
    "    _udp.endPacket();\n"
    "    return true;\n"
    "}\n"
)

MCAST_NEW = (
    "bool Esp32Platform::sendBytesMultiCast(uint8_t* buffer, uint16_t len)\n"
    "{\n"
    "    " + MCAST_MARKER + "\n"
    "    // - see patch 9b in scripts/patch_knx.py.\n"
    "    for (uint8_t sbipTry = 0; sbipTry < 3; sbipTry++)\n"
    "    {\n"
    "        if (sbipTry > 0)\n"
    "            delayMicroseconds(400);\n"
    "\n"
    "        if (_udp.beginMulticastPacket() != 1)\n"
    "            continue;\n"
    "\n"
    "        _udp.write(buffer, len);\n"
    "\n"
    "        if (_udp.endPacket() != 0)\n"
    "            return true;\n"
    "    }\n"
    "\n"
    '    println("sbip: a multicast frame was refused three times and is lost");\n'
    "    return false;\n"
    "}\n"
)


def patch_mcast_retry():
    platform_c = os.path.join(
        env["PROJECT_LIBDEPS_DIR"],  # noqa: F821
        env["PIOENV"],  # noqa: F821
        "knx",
        "src",
        "esp32_platform.cpp",
    )

    if not os.path.isfile(platform_c):
        return

    with open(platform_c, "r", encoding="utf-8") as handle:
        source = handle.read()

    if MCAST_MARKER in source:
        return

    if source.count(MCAST_ANCHOR) != 1:
        sys.stderr.write(
            "patch_knx.py: anchor no longer unique, a multicast frame the "
            "network driver refuses stays lost - ETS over routing will miss "
            "answers under load:\n  %s\n" % MCAST_ANCHOR.strip().splitlines()[0]
        )
        return

    with open(platform_c, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(source.replace(MCAST_ANCHOR, MCAST_NEW))

    print("patch_knx.py: UDP multicast retry applied to esp32_platform.cpp")


patch_mcast_retry()


# --------------------------------------------------------------------------
# 10. Reach devices outside our own line while unprogrammed
# --------------------------------------------------------------------------
#
# Without an ETS download the device carries the default address 15.15.0, and
# a coupler filters by line:
#
#     if (_couplerType == LineCoupler && srcIfIndex == kPrimaryIfIndex)
#         if (ZS != ownSNA)
#             return false;              // IGNORE_TOTALLY
#
# A request for 1.1.3 does not belong to 15.15.x, so it is thrown away and
# reading device information over TP fails. Correct for a coupler, useless
# for what this device is before it is programmed: a plain IP interface, which
# has to pass everything through.
#
# Same switch as for group addresses (patch 2), same reasoning - it is on
# whenever no filter table has been downloaded, and can be forced on.

PHYS_MARKER = "// sbip: an unprogrammed device is an interface, not a coupler"

PHYS_ANCHOR = (
    "bool NetworkLayerCoupler::isRoutedIndividualAddress(uint16_t"
    " individualAddress, uint8_t srcIfIndex)\n"
    "{\n"
)

PHYS_NEW = (
    "// sbip: set by RouterObject, see KnxLink::routeUnfiltered()\n"
    "extern bool sbipRouteUnfiltered;\n"
    "\n"
    "bool NetworkLayerCoupler::isRoutedIndividualAddress(uint16_t"
    " individualAddress, uint8_t srcIfIndex)\n"
    "{\n"
    "    " + PHYS_MARKER + "\n"
    "    // With the built-in address 15.15.0 the line filter below drops\n"
    "    // everything addressed to another line, so ETS cannot reach a single\n"
    "    // device on TP.\n"
    "    if (sbipRouteUnfiltered)\n"
    "        return true;\n"
    "\n"
)


def patch_physical_routing():
    coupler = os.path.join(
        env["PROJECT_LIBDEPS_DIR"],  # noqa: F821
        env["PIOENV"],  # noqa: F821
        "knx",
        "src",
        "knx",
        "network_layer_coupler.cpp",
    )

    if not os.path.isfile(coupler):
        return

    with open(coupler, "r", encoding="utf-8") as handle:
        source = handle.read()

    if PHYS_MARKER in source:
        return

    if source.count(PHYS_ANCHOR) != 1:
        sys.stderr.write(
            "patch_knx.py: anchor no longer unique, an unprogrammed device "
            "cannot reach anything outside line 15.15.x:\n  %s\n"
            % PHYS_ANCHOR.strip().splitlines()[0]
        )
        return

    with open(coupler, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(source.replace(PHYS_ANCHOR, PHYS_NEW))

    print("patch_knx.py: unfiltered physical routing applied to "
          "network_layer_coupler.cpp")


patch_physical_routing()


# --------------------------------------------------------------------------
# 11. Put a tunnel client's frame on the bus
# --------------------------------------------------------------------------
#
# This is the one that actually broke "read device information over TP".
# DataLinkLayer::dataRequestFromTunnel() ends in sendFrame(), but three
# early returns sit in front of it, and the middle one is:
#
#     if (isRoutedPA(frame.destinationAddress()))
#         return;
#
# "Routed" means: another line owns this address, so the coupler will carry
# it and I need not put it on TP myself. isRoutedPA() answers that by
# comparing the destination against our own subnetwork address - which is
# 0xFF00 while unprogrammed. Every real address then looks foreign, and
# every ETS request left through the routing multicast instead of the bus.
#
# The recording showed exactly that: Tunnel;RX 15.15.1 -> 1.1.1 followed by
# IP;TX and no TP send at all.

ROUTEDPA_MARKER = "// sbip: an interface has no second line to hand this to"

ROUTEDPA_ANCHOR = (
    "bool DataLinkLayer::isRoutedPA(uint16_t pa)\n"
    "{\n"
    "    uint16_t ownpa = _deviceObject.individualAddress();\n"
)

ROUTEDPA_NEW = (
    "// sbip: set by RouterObject, see KnxLink::routeUnfiltered()\n"
    "extern bool sbipRouteUnfiltered;\n"
    "\n"
    "bool DataLinkLayer::isRoutedPA(uint16_t pa)\n"
    "{\n"
    "    " + ROUTEDPA_MARKER + "\n"
    "    // Unprogrammed we are 15.15.0, so the comparison below calls every\n"
    "    // real address foreign and dataRequestFromTunnel() returns before\n"
    "    // sendFrame() - the bus never sees what ETS asked for.\n"
    "    if (sbipRouteUnfiltered)\n"
    "        return false;\n"
    "\n"
    "    uint16_t ownpa = _deviceObject.individualAddress();\n"
)

# The same function is the reason the recording looked innocent: the send
# that dataRequestFromTunnel() performs goes straight to sendFrame() and
# skips sendTelegram(), where the monitor hook of patch 6 sits.

TUNTP_MARKER = "// sbip: sendFrame() skips sendTelegram() and its hook"

TUNTP_ANCHOR = (
    "    // Send to KNX medium\n"
    "    sendFrame(frame);\n"
)

TUNTP_NEW = (
    "    " + TUNTP_MARKER + "\n"
    "    // Without this line a frame handed over by ETS disappears from the\n"
    "    // recording at the moment it goes onto the bus.\n"
    "    if (sbipMonitorHook)\n"
    "        sbipMonitorHook(_networkLayerEntity.getEntityIndex(), true,\n"
    "                        frame.data(), frame.totalLenght());\n"
    "\n"
    "    // Send to KNX medium\n"
    "    sendFrame(frame);\n"
)


def patch_tunnel_to_bus():
    with open(DLL_C, "r", encoding="utf-8") as handle:
        source = handle.read()

    if ROUTEDPA_MARKER in source:
        return

    if source.count(ROUTEDPA_ANCHOR) != 1:
        sys.stderr.write(
            "patch_knx.py: anchor no longer unique, a tunnel client "
            "cannot reach any device on the bus:\n  %s\n"
            % ROUTEDPA_ANCHOR.strip().splitlines()[0]
        )
        return

    source = source.replace(ROUTEDPA_ANCHOR, ROUTEDPA_NEW)

    # Only useful once patch 6 has declared the hook.
    if "sbipMonitorHook" in source and source.count(TUNTP_ANCHOR) == 1:
        source = source.replace(TUNTP_ANCHOR, TUNTP_NEW)

    with open(DLL_C, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(source)

    print("patch_knx.py: tunnel-to-bus path opened in data_link_layer.cpp")


patch_tunnel_to_bus()


# --------------------------------------------------------------------------
# 12. Notice a tunnel client that never answers
# --------------------------------------------------------------------------
#
# A tunnelling request has to be acknowledged. The library receives the ack
# and drops it on the floor, so "ETS shows nothing" and "the frames never
# arrive" look identical from here - and that question has cost several
# rounds of testing. Counting both sides settles it.

ACK_MARKER = "// sbip: a client that never acknowledges is not receiving"

ACK_ANCHOR = (
    "void IpDataLinkLayer::sendFrameToTunnel(KnxIpTunnelConnection* tunnel,"
    " CemiFrame& frame)\n"
    "{\n"
)

ACK_NEW = (
    ACK_MARKER + "\n"
    "static uint32_t sbipTunnelSent = 0;\n"
    "static uint32_t sbipTunnelAcked = 0;\n"
    "static bool sbipTunnelAckWarned = false;\n"
    "\n"
    "void IpDataLinkLayer::sendFrameToTunnel(KnxIpTunnelConnection* tunnel,"
    " CemiFrame& frame)\n"
    "{\n"
    "    if (!sbipTunnelAckWarned && ++sbipTunnelSent - sbipTunnelAcked >= 10)\n"
    "    {\n"
    "        sbipTunnelAckWarned = true;\n"
    "        println(\"KNX: a tunnel client is not acknowledging anything we \"\n"
    "                \"send. The frames leave this device, so they are lost on \"\n"
    "                \"the way (firewall, router between the subnets) or the \"\n"
    "                \"client rejects them.\");\n"
    "    }\n"
    "\n"
)

ACK_RX_ANCHOR = (
    "        case TunnelingAck:\n"
    "        {\n"
    "            //TOOD nothing to do now\n"
    "            //println(\"got Ack\");\n"
    "            break;\n"
    "        }\n"
)

ACK_RX_NEW = (
    "        case TunnelingAck:\n"
    "        {\n"
    "            sbipTunnelAcked++;\n"
    "            sbipTunnelAckWarned = false;\n"
    "            break;\n"
    "        }\n"
)


def patch_tunnel_ack():
    with open(TARGET, "r", encoding="utf-8") as handle:
        source = handle.read()

    if ACK_MARKER in source:
        return

    for anchor in (ACK_ANCHOR, ACK_RX_ANCHOR):
        if source.count(anchor) != 1:
            sys.stderr.write(
                "patch_knx.py: anchor no longer unique, an unresponsive "
                "tunnel client stays invisible:\n  %s\n"
                % anchor.strip().splitlines()[0]
            )
            return

    source = source.replace(ACK_ANCHOR, ACK_NEW)
    source = source.replace(ACK_RX_ANCHOR, ACK_RX_NEW)

    with open(TARGET, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(source)

    print("patch_knx.py: tunnel acknowledge watch applied to "
          "ip_data_link_layer.cpp")


patch_tunnel_ack()


# --------------------------------------------------------------------------
# Mandatory properties a mask 091A download writes
# --------------------------------------------------------------------------
#
# Product data for KNXnet/IP routers of mask 091A merge a fragment into the
# mask's load procedure that writes PID_COUPL_SERV_CONTROL in the router
# object and PID_ROUTING_BUSY_WAIT_TIME in the KNXnet/IP parameter object.
# The stack has neither for this mask, so the device answers the write with
# zero elements and the download stops.
#
# PID 57: RouterObject creates it only for coupler model 2.0, but 06 Profiles
# A.3.3 lists it for mask 091A as well (read level 3, write level 0). Default
# per 03_05_01 4.5.8: EN_SNA_READ (bit 3) set, everything else off.
#
# PID 78: mandatory for every KNXnet/IP router (03_08_03 2.5.28), default
# 100 ms, range 20..100 ms. Stored only; the routing busy handling of the stack
# keeps its own timing.
#
# Neither may take part in the NVM image. Memory::readMemory() restores the
# interface objects as one byte stream with no per-object length, so a new
# write-enabled DataProperty shifts everything behind it: a device programmed
# by an older firmware then reads element counts out of the neighbouring
# bytes, DataProperty::restore() allocates for them and the boot aborts before
# setup() finishes. Both properties are held in RAM only - nothing evaluates
# them, and the defaults come back after a restart.

VOLATILE_MARKER = "// sbip: DataProperty that stays out of the NVM image"

VOLATILE_CLASS = (
    "\n"
    + VOLATILE_MARKER + "\n"
    "class VolatileDataProperty : public DataProperty\n"
    "{\n"
    "    public:\n"
    "        using DataProperty::DataProperty;\n"
    "        uint8_t* save(uint8_t* buffer) override { return buffer; }\n"
    "        const uint8_t* restore(const uint8_t* buffer) override { return buffer; }\n"
    "        uint16_t saveSize() override { return 0; }\n"
    "};\n"
)

DATAPROP_H = os.path.join(
    env["PROJECT_LIBDEPS_DIR"],  # noqa: F821
    env["PIOENV"],  # noqa: F821
    "knx", "src", "knx", "data_property.h",
)

COUPL_MARKER = "// sbip: PID_COUPL_SERV_CONTROL for coupler model 1.x (mask 091A)"

COUPL_ANCHOR = (
    "        new DataProperty( PID_SUB_LCGRPCONFIG, true, PDT_BITSET8, 1, "
    "ReadLv3 | WriteLv0, (uint8_t) (LCGRPCONFIG::GROUP_6FFFROUTE | "
    "LCGRPCONFIG::GROUP_7000UNLOCK | LCGRPCONFIG::GROUP_REPEAT)), "
    "// Secondary: data group\n"
)

COUPL_EXTRA = (
    "        " + COUPL_MARKER + "\n"
    "        new VolatileDataProperty( PID_COUPLER_SERVICES_CONTROL, true, PDT_GENERIC_01, 1,"
    " ReadLv3 | WriteLv0, (uint8_t) 0x08),\n"
)

BUSY_MARKER = "// sbip: PID_ROUTING_BUSY_WAIT_TIME, mandatory for KNXnet/IP routers"

BUSY_ANCHOR = (
    "        new DataProperty(PID_TTL, true, PDT_UNSIGNED_CHAR, 1, "
    "ReadLv3 | WriteLv3, (uint8_t)16),\n"
)

BUSY_EXTRA = (
    "        " + BUSY_MARKER + "\n"
    "        new VolatileDataProperty(PID_ROUTING_BUSY_WAIT_TIME, true, PDT_UNSIGNED_INT, 1,"
    " ReadLv3 | WriteLv3, (uint16_t)100),\n"
)


def insert_after(path, marker, anchor, extra, what):
    if not os.path.isfile(path):
        return

    with open(path, "r", encoding="utf-8") as handle:
        source = handle.read()

    if marker in source:
        return

    if source.count(anchor) != 1:
        sys.stderr.write(
            "patch_knx.py: anchor for %s not found, a mask 091A download "
            "will stop at this property\n" % what
        )
        return

    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(source.replace(anchor, anchor + extra))

    print("patch_knx.py: %s added to %s" % (what, os.path.basename(path)))


def append_volatile_class():
    if not os.path.isfile(DATAPROP_H):
        return

    with open(DATAPROP_H, "r", encoding="utf-8") as handle:
        source = handle.read()

    if VOLATILE_MARKER in source:
        return

    with open(DATAPROP_H, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(source + VOLATILE_CLASS)

    print("patch_knx.py: VolatileDataProperty added to data_property.h")


append_volatile_class()
insert_after(ROUTER, COUPL_MARKER, COUPL_ANCHOR, COUPL_EXTRA, "PID_COUPL_SERV_CONTROL")
insert_after(IPPARAM, BUSY_MARKER, BUSY_ANCHOR, BUSY_EXTRA, "PID_ROUTING_BUSY_WAIT_TIME")


# --------------------------------------------------------------------------
# 13. Show the bus monitor what the coupler does not acknowledge
# --------------------------------------------------------------------------
#
# Symptom: once ETS has programmed the coupler, the bus monitor misses most
# of the line - a line scan shows our requests but hardly any of the traffic
# between the devices, while the same scan unprogrammed shows everything.
#
# Cause: TpUartDataLinkLayer asks isAckRequired() after the seventh byte and
# passes a frame on only when the answer was yes. Bau091A answers yes for a
# group address in the filter table and for an individual address on the
# other side; unprogrammed, sbipRouteUnfiltered makes both say yes to
# everything. The rest is counted as ignored and dropped before it reaches
# frameReceived(), where the hook of patch 6 sits.
#
# Fix: hand those frames to the hook as well, and nothing else - they are
# still neither acknowledged nor routed. The echo of our own frame is left
# out, sendTelegram() has already recorded it as TX. The bus load figure,
# fed from the same hook, now counts the whole line too.

TPRX_C = os.path.join(
    env["PROJECT_LIBDEPS_DIR"],  # noqa: F821
    env["PIOENV"],  # noqa: F821
    "knx",
    "src",
    "knx",
    "tpuart_data_link_layer.cpp",
)

TPRX_MARKER = "// sbip: bus monitor, frames the coupler does not acknowledge"

TPRX_ANCHOR_DECL = "void TpUartDataLinkLayer::processRxFrame(TpFrame* tpFrame)\n"

TPRX_DECL = (
    TPRX_MARKER + "\n"
    "extern void (*sbipMonitorHook)(uint8_t side, bool outgoing,\n"
    "                               const uint8_t* cemi, uint16_t length);\n"
    "\n"
)

TPRX_ANCHOR = (
    "        if (!(tpFrame->flags() & TP_FRAME_FLAG_ECHO))\n"
    "            rxFrameReceived(tpFrame);\n"
    "    }\n"
)

TPRX_NEW = TPRX_ANCHOR + (
    "    else if (sbipMonitorHook && !(tpFrame->flags() & TP_FRAME_FLAG_ECHO))\n"
    "    {\n"
    "        // sbip: seen on the line, not for us - recorded, not routed.\n"
    "        uint8_t* cemi = tpFrame->cemiData();\n"
    "        sbipMonitorHook(_networkLayerEntity.getEntityIndex(), false,\n"
    "                        cemi, tpFrame->cemiSize());\n"
    "        free(cemi);\n"
    "    }\n"
)


def patch_monitor_unaddressed():
    if not os.path.isfile(TPRX_C):
        return

    with open(TPRX_C, "r", encoding="utf-8") as handle:
        source = handle.read()

    if TPRX_MARKER in source:
        return

    if source.count(TPRX_ANCHOR_DECL) != 1 or source.count(TPRX_ANCHOR) != 1:
        sys.stderr.write(
            "patch_knx.py: anchor no longer unique, the bus monitor shows only "
            "what the coupler acknowledges once it is programmed\n"
        )
        return

    source = source.replace(TPRX_ANCHOR_DECL, TPRX_DECL + TPRX_ANCHOR_DECL)
    source = source.replace(TPRX_ANCHOR, TPRX_NEW)

    with open(TPRX_C, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(source)

    print("patch_knx.py: unacknowledged TP frames passed to the bus monitor")


if MONITOR_OK:
    patch_monitor_unaddressed()


# --------------------------------------------------------------------------
# 14. One line for the restored configuration instead of a column of numbers
# --------------------------------------------------------------------------
#
# Memory::readMemory() prints a hex dump of the header, then for every saved
# object its offset in the image - with the sign the wrong way round, as
# "flashStart - buffer" - on a line of its own, followed by a line holding a
# single dot. Twenty lines of the boot log that say nothing a reader can use.
#
# Replaced by one line with the size of each part, in the order the stack
# registered them: for mask 091A the device object, the cEMI server object,
# the IP parameters and the facade, then the application program and the
# filter table. The warnings for an image that does not match stay as they
# are. writeMemory() loses its two counting lines for the same reason.

MEMLOG_C = os.path.join(
    env["PROJECT_LIBDEPS_DIR"],  # noqa: F821
    env["PIOENV"],  # noqa: F821
    "knx",
    "src",
    "knx",
    "memory.cpp",
)

MEMLOG_MARKER = "// sbip: restored sizes on one line, see scripts/patch_knx.py"

MEMLOG_EDITS = (
    (
        '    println("readMemory");\n\n',
        "",
    ),
    (
        '    printHex("RESTORED ", flashStart, _metadataSize);\n\n',
        "",
    ),
    (
        '    println("restoring data from flash...");\n'
        '    print("saverestores ");\n'
        "    println(_saveCount);\n"
        "\n"
        "    for (int i = 0; i < _saveCount; i++)\n"
        "    {\n"
        "        println(flashStart - buffer);\n"
        '        println(".");\n'
        "        buffer = _saveRestores[i]->restore(buffer);\n"
        "    }\n"
        "\n"
        '    println("restored saveRestores");\n',
        "    " + MEMLOG_MARKER + "\n"
        '    print("KNX: configuration restored - objects");\n'
        "\n"
        "    for (int i = 0; i < _saveCount; i++)\n"
        "    {\n"
        "        const uint8_t* start = buffer;\n"
        "        buffer = _saveRestores[i]->restore(buffer);\n"
        '        print(i ? "+" : " ");\n'
        "        print((unsigned int)(buffer - start));\n"
        "    }\n"
        "\n"
        '    print(" B");\n',
    ),
    (
        '        println("TableObjects are referring to an older firmware version'
        ' and are not loaded");\n',
        '        println(", tables not loaded:");\n'
        '        println("TableObjects are referring to an older firmware version'
        ' and are not loaded");\n',
    ),
    (
        '    print("tableObjs ");\n'
        "    println(_tableObjCount);\n"
        "\n"
        "    for (int i = 0; i < _tableObjCount; i++)\n"
        "    {\n"
        "        println(flashStart - buffer);\n"
        '        println(".");\n'
        "        buffer = _tableObjects[i]->restore(buffer);\n"
        "        uint16_t memorySize = 0;\n"
        "        buffer = popWord(memorySize, buffer);\n"
        "        println(memorySize);\n",
        '    print(", tables");\n'
        "\n"
        "    for (int i = 0; i < _tableObjCount; i++)\n"
        "    {\n"
        "        buffer = _tableObjects[i]->restore(buffer);\n"
        "        uint16_t memorySize = 0;\n"
        "        buffer = popWord(memorySize, buffer);\n"
        '        print(i ? "+" : " ");\n'
        "        print((unsigned int)memorySize);\n",
    ),
    (
        '    println("restored Tableobjects");\n',
        '    println(" B");\n',
    ),
    (
        '    print("save saveRestores ");\n'
        "    println(_saveCount);\n"
        "\n",
        "",
    ),
    (
        '    print("save tableobjs ");\n'
        "    println(_tableObjCount);\n"
        "\n",
        "",
    ),
)


def patch_memory_log():
    if not os.path.isfile(MEMLOG_C):
        return

    with open(MEMLOG_C, "r", encoding="utf-8") as handle:
        source = handle.read()

    if MEMLOG_MARKER in source:
        return

    for anchor, _ in MEMLOG_EDITS:
        if source.count(anchor) != 1:
            sys.stderr.write(
                "patch_knx.py: anchor no longer unique, the boot log keeps "
                "the stack's column of offsets:\n  %s\n"
                % anchor.strip().splitlines()[0]
            )
            return

    for anchor, replacement in MEMLOG_EDITS:
        source = source.replace(anchor, replacement)

    with open(MEMLOG_C, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(source)

    print("patch_knx.py: restore log condensed in memory.cpp")


patch_memory_log()


# --------------------------------------------------------------------------
# 15. Answer an extended search in programming mode
# --------------------------------------------------------------------------
#
# ETS locates a KNX IP device in programming mode with a
# SEARCH_REQUEST_EXTENDED carrying the "programming mode" search parameter,
# and only then writes the individual address. platformio.ini switches Core
# v2 on for that. The handler asks the global facade whether programming mode
# is on - and there is no global facade here, KNX_NO_AUTOMATIC_GLOBAL_INSTANCE
# is set because knx_link.cpp builds its own. The device object the layer
# already holds knows the same thing.

SRPPROG_MARKER = "// sbip: no global knx instance, ask the device object"

SRPPROG_ANCHOR = (
    '        println("srpByProgMode");\n'
    "\n"
    "        if (!knx.progMode())\n"
    "            return;\n"
)

SRPPROG_NEW = (
    "        " + SRPPROG_MARKER + "\n"
    "        if (!_deviceObject.progMode())\n"
    "            return;\n"
)


def patch_search_prog_mode():
    with open(TARGET, "r", encoding="utf-8") as handle:
        source = handle.read()

    if SRPPROG_MARKER in source:
        return

    if source.count(SRPPROG_ANCHOR) != 1:
        sys.stderr.write(
            "patch_knx.py: anchor no longer unique, an extended search in "
            "programming mode will not build:\n  %s\n"
            % SRPPROG_ANCHOR.strip().splitlines()[0]
        )
        return

    with open(TARGET, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(source.replace(SRPPROG_ANCHOR, SRPPROG_NEW))

    print("patch_knx.py: programming mode search parameter fixed in "
          "ip_data_link_layer.cpp")


patch_search_prog_mode()


# --------------------------------------------------------------------------
# 16. Confirm a tunnel client's frame once TP1 has sent it
# --------------------------------------------------------------------------
#
# Symptom: an ETS line scan filled the TP-UART transmit queue within seconds
# - 735 frames in five seconds in the bus monitor, "Ignore frame because
# transmit queue is full!" 138 times - and whatever was dropped there, ETS
# still counted as sent.
#
# Cause: dataRequestFromTunnel() sends the L_Data.con first thing, before
# the frame has even been queued. A tunnel client paces itself by that
# confirmation, so ETS got the go-ahead for the next frame at IP speed while
# TP1 manages some fifty a second. The TP-UART's real confirmation arrived
# later in dataConReceived() and was thrown away, since the frame came from
# a tunnel and not from the local stack.
#
# Fix: a frame that goes onto TP1 is confirmed from dataConReceived(), with
# the outcome the TP-UART reported - negatively when the queue was full,
# which makes ETS repeat it instead of losing it. A frame that stays off the
# bus is confirmed at once as before.
#
# Only the IP layer knows the tunnel addresses (DataLinkLayer::_ipParameters
# stays null, see patch 4), so the TP layer asks the firmware through
# sbipTunnelSourceHook, defined in src/knx_link.cpp. Unset, both places fall
# back to the upstream behaviour.

TUNCON_MARKER = "// sbip: confirmation hook, defined in src/knx_link.cpp"

TUNCON_ANCHOR_DECL = "void DataLinkLayer::dataRequestFromTunnel(CemiFrame& frame)\n"

TUNCON_DECL = (
    TUNCON_MARKER + "\n"
    "extern bool (*sbipTunnelSourceHook)(uint16_t address);\n"
    "\n"
)

TUNCON_ANCHOR_REQ = (
    "void DataLinkLayer::dataRequestFromTunnel(CemiFrame& frame)\n"
    "{\n"
    "    _cemiServer->dataConfirmationToTunnel(frame);\n"
)

TUNCON_NEW_REQ = (
    "void DataLinkLayer::dataRequestFromTunnel(CemiFrame& frame)\n"
    "{\n"
    "    // sbip: decided before local delivery, which may change our address\n"
    "    bool sbipToBus = true;\n"
    "\n"
    "#ifdef KNX_TUNNELING\n"
    "    if (frame.addressType() == AddressType::IndividualAddress &&\n"
    "            (frame.destinationAddress() == _deviceObject.individualAddress() ||\n"
    "             isRoutedPA(frame.destinationAddress()) ||\n"
    "             isTunnelingPA(frame.destinationAddress())))\n"
    "        sbipToBus = false;\n"
    "#endif\n"
    "\n"
    "    // sbip: a frame for TP1 is confirmed by dataConReceived() once the\n"
    "    // TP-UART has sent it - the client paces itself by that.\n"
    "    if (!sbipToBus || sbipTunnelSourceHook == nullptr ||\n"
    "            mediumType() != DptMedium::KNX_TP1 ||\n"
    "            !sbipTunnelSourceHook(frame.sourceAddress()))\n"
    "        _cemiServer->dataConfirmationToTunnel(frame);\n"
)

TUNCON_ANCHOR_OPTI = (
    "    if (frame.addressType() == AddressType::IndividualAddress)\n"
    "    {\n"
    "        if (frame.destinationAddress() == _deviceObject.individualAddress())\n"
    "            return;\n"
    "\n"
    "        if (isRoutedPA(frame.destinationAddress()))\n"
    "            return;\n"
    "\n"
    "        if (isTunnelingPA(frame.destinationAddress()))\n"
    "            return;\n"
    "    }\n"
)

TUNCON_NEW_OPTI = (
    "    // sbip: the same decision as above, taken once\n"
    "    if (!sbipToBus)\n"
    "        return;\n"
)

TUNCON_ANCHOR_CON = (
    "    // if the confirmation was caused by a tunnel request then\n"
    "    // do not send it to the local stack\n"
)

TUNCON_NEW_CON = (
    "    // sbip: the confirmation dataRequestFromTunnel() held back\n"
    "    if (sbipTunnelSourceHook != nullptr &&\n"
    "            mediumType() == DptMedium::KNX_TP1 &&\n"
    "            sbipTunnelSourceHook(source))\n"
    "    {\n"
    "        _cemiServer->dataConfirmationToTunnel(frame);\n"
    "        frame.messageCode(backupMsgCode);\n"
    "        return;\n"
    "    }\n"
    "\n"
) + TUNCON_ANCHOR_CON

TUNCON_EDITS = (
    (TUNCON_ANCHOR_REQ, TUNCON_NEW_REQ),
    (TUNCON_ANCHOR_OPTI, TUNCON_NEW_OPTI),
    (TUNCON_ANCHOR_CON, TUNCON_NEW_CON),
    (TUNCON_ANCHOR_DECL, TUNCON_DECL + TUNCON_ANCHOR_DECL),
)


def patch_tunnel_confirm():
    with open(DLL_C, "r", encoding="utf-8") as handle:
        source = handle.read()

    if TUNCON_MARKER in source:
        return

    for anchor, _ in TUNCON_EDITS:
        if source.count(anchor) != 1:
            sys.stderr.write(
                "patch_knx.py: anchor no longer unique, a tunnel client is "
                "confirmed before its frame reaches TP1:\n  %s\n"
                % anchor.strip().splitlines()[0]
            )
            return

    for anchor, replacement in TUNCON_EDITS:
        source = source.replace(anchor, replacement)

    with open(DLL_C, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(source)

    print("patch_knx.py: tunnel confirmation deferred to TP1 in "
          "data_link_layer.cpp")


patch_tunnel_confirm()


# --------------------------------------------------------------------------
# 17. Let the firmware decide which path may manage the device
# --------------------------------------------------------------------------
#
# Used as a line coupler into an unprotected line - a garden, a garage - the
# device can be reprogrammed from that line: anyone who knows its address
# opens a connection, rewrites the filter table or LCCONFIG and the inner line
# is open. The programming button does not help, it only guards the address
# assignment.
#
# The device is reached along three paths, told apart here:
#
#     0  KNXnet/IP routing   frames from the multicast, primary interface
#     1  TP1                 frames from the line, secondary interface
#     2  KNXnet/IP tunnel    frames a tunnel client handed over; they enter
#                            through the TP layer, which is where bau091A
#                            put the cEMI server, so srcIfIdx says 1
#
# sbipManagementHook, defined in src/ets_access.cpp, answers per frame.
# Refused are everything addressed to our own individual address, the local
# delivery of broadcasts - address programming, serial number writes - and
# property writes a tunnel client sends as cEMI M_PropWrite. Forwarding to
# the other side is untouched: the lock guards the device, not the line.
#
# A refused T_Connect gets a T_Disconnect back, so the tool reports the
# device unreachable at once instead of running into its timeout.

MGMT_MARKER = "// sbip: management lock, see src/ets_access.cpp"

MGMT_DECL = (
    MGMT_MARKER + "\n"
    "extern bool (*sbipManagementHook)(uint8_t path, uint16_t source, bool individual);\n"
)

# data_link_layer.cpp: mark the frames a tunnel client hands over.
MGMT_TUN_ANCHOR = (
    "    sbipMonitorSuppress = true;\n"
    "    frameReceived(frame);\n"
    "    sbipMonitorSuppress = false;\n"
)

MGMT_TUN_NEW = (
    "    sbipMonitorSuppress = true;\n"
    "    sbipFromTunnel = true;\n"
    "    frameReceived(frame);\n"
    "    sbipFromTunnel = false;\n"
    "    sbipMonitorSuppress = false;\n"
)

MGMT_TUN_ANCHOR_DECL = "void DataLinkLayer::dataRequestFromTunnel(CemiFrame& frame)\n"

MGMT_TUN_DECL = (
    MGMT_MARKER + "\n"
    "// Set while a tunnel client's frame is delivered locally.\n"
    "bool sbipFromTunnel = false;\n"
    "\n"
)

# network_layer_coupler.cpp: the three places that deliver to the local stack.
MGMT_NL_ANCHOR_DECL = (
    "void NetworkLayerCoupler::routeDataIndividual(AckType ack, uint16_t"
    " destination, NPDU& npdu, Priority priority, uint16_t source, uint8_t"
    " srcIfIndex)\n"
)

MGMT_NL_DECL = (
    MGMT_DECL +
    "extern bool sbipFromTunnel;\n"
    "\n"
    "static bool sbipManagementAllowed(uint8_t srcIfIdx, uint16_t source, bool individual)\n"
    "{\n"
    "    // Our own frames, and a build without the firmware side\n"
    "    if (srcIfIdx > 1 || sbipManagementHook == nullptr)\n"
    "        return true;\n"
    "\n"
    "    return sbipManagementHook(sbipFromTunnel ? 2 : srcIfIdx, source, individual);\n"
    "}\n"
    "\n"
)

MGMT_NL_ANCHOR_IND = (
    "        // FORWARD_LOCALLY\n"
    "        //println(\"NetworkLayerCoupler::routeDataIndividual locally\");\n"
)

MGMT_NL_NEW_IND = (
    MGMT_NL_ANCHOR_IND +
    "        if (!sbipManagementAllowed(srcIfIndex, source, true))\n"
    "        {\n"
    "            // sbip: hang up at once rather than let the tool time out\n"
    "            if (npdu.tpdu().type() == Connect)\n"
    "            {\n"
    "                CemiFrame frame(0);\n"
    "                TPDU& tpdu = frame.tpdu();\n"
    "                tpdu.type(Disconnect);\n"
    "                tpdu.sequenceNumber(0);\n"
    "                dataIndividualRequest(AckRequested, source, NetworkLayerParameter,\n"
    "                                      SystemPriority, tpdu);\n"
    "            }\n"
    "            return;\n"
    "        }\n"
    "\n"
)

MGMT_NL_ANCHOR_SYS = (
    "            npdu.frame().systemBroadcast(SysBroadcast);\n"
    "            _transportLayer.dataSystemBroadcastIndication(hopType, priority, source, npdu.tpdu());\n"
    "            return;\n"
)

MGMT_NL_NEW_SYS = (
    "            npdu.frame().systemBroadcast(SysBroadcast);\n"
    "            if (sbipManagementAllowed(srcIfIdx, source, false))\n"
    "                _transportLayer.dataSystemBroadcastIndication(hopType, priority, source, npdu.tpdu());\n"
    "            return;\n"
)

MGMT_NL_ANCHOR_BC = (
    "\n"
    "        _transportLayer.dataBroadcastIndication(hopType, priority, source, npdu.tpdu());\n"
)

MGMT_NL_NEW_BC = (
    "\n"
    "        if (sbipManagementAllowed(srcIfIdx, source, false))\n"
    "            _transportLayer.dataBroadcastIndication(hopType, priority, source, npdu.tpdu());\n"
)

MGMT_NL_ANCHOR_SYS2 = (
    "        HopCountType hopType = npdu.hopCount() == 7 ? UnlimitedRouting : NetworkLayerParameter;\n"
    "        _transportLayer.dataSystemBroadcastIndication(hopType, priority, source, npdu.tpdu());\n"
    "    }\n"
)

MGMT_NL_NEW_SYS2 = (
    "        HopCountType hopType = npdu.hopCount() == 7 ? UnlimitedRouting : NetworkLayerParameter;\n"
    "        if (sbipManagementAllowed(srcIfIdx, source, false))\n"
    "            _transportLayer.dataSystemBroadcastIndication(hopType, priority, source, npdu.tpdu());\n"
    "    }\n"
)

# cemi_server.cpp: local device management over a tunnel or a configuration
# connection. Reading stays open - ETS reads a few properties before it uses
# the device as a plain interface. The client address writes only change
# what this connection is told, and stay open for the same reason.
MGMT_CEMI_ANCHOR_DECL = "void CemiServer::frameReceived(CemiFrame& frame)\n"

MGMT_CEMI_ANCHOR_WRITE = (
    "    else\n"
    "    {\n"
    "        _bau.propertyValueWrite((ObjectType)objectType, objectInstance,"
    " propertyId, numberOfElements, startIndex, requestData, requestDataSize);\n"
    "    }\n"
)

MGMT_CEMI_NEW_WRITE = (
    "    else if (sbipManagementHook != nullptr && !sbipManagementHook(2, 0, true))\n"
    "    {\n"
    "        // sbip: refused, answered with a negative confirmation below\n"
    "        numberOfElements = 0;\n"
    "    }\n"
) + MGMT_CEMI_ANCHOR_WRITE

MGMT_CEMI_ANCHOR_RESET = (
    "        case M_Reset_req:\n"
    "        {\n"
    "            handleMReset(frame);\n"
)

MGMT_CEMI_NEW_RESET = (
    "        case M_Reset_req:\n"
    "        {\n"
    "            if (sbipManagementHook != nullptr && !sbipManagementHook(2, 0, true))\n"
    "                break;\n"
    "\n"
    "            handleMReset(frame);\n"
)


def knx_source(name):
    return os.path.join(
        env["PROJECT_LIBDEPS_DIR"],  # noqa: F821
        env["PIOENV"],  # noqa: F821
        "knx", "src", "knx", name,
    )


def apply_edits(path, edits, what):
    """All edits or none - a lock that covers only some paths is worse than
    no lock, because the dashboard would claim otherwise."""
    with open(path, "r", encoding="utf-8") as handle:
        source = handle.read()

    if MGMT_MARKER in source:
        return None

    for anchor, _ in edits:
        if source.count(anchor) != 1:
            sys.stderr.write(
                "patch_knx.py: anchor no longer unique, %s:\n  %s\n"
                % (what, anchor.strip().splitlines()[0])
            )
            return False

    for anchor, replacement in edits:
        source = source.replace(anchor, replacement)

    return source


def patch_management_lock():
    files = (
        (DLL_C, (
            (MGMT_TUN_ANCHOR, MGMT_TUN_NEW),
            (MGMT_TUN_ANCHOR_DECL, MGMT_TUN_DECL + MGMT_TUN_ANCHOR_DECL),
        )),
        (knx_source("network_layer_coupler.cpp"), (
            (MGMT_NL_ANCHOR_IND, MGMT_NL_NEW_IND),
            (MGMT_NL_ANCHOR_SYS, MGMT_NL_NEW_SYS),
            (MGMT_NL_ANCHOR_BC, MGMT_NL_NEW_BC),
            (MGMT_NL_ANCHOR_SYS2, MGMT_NL_NEW_SYS2),
            (MGMT_NL_ANCHOR_DECL, MGMT_NL_DECL + MGMT_NL_ANCHOR_DECL),
        )),
        (knx_source("cemi_server.cpp"), (
            (MGMT_CEMI_ANCHOR_WRITE, MGMT_CEMI_NEW_WRITE),
            (MGMT_CEMI_ANCHOR_RESET, MGMT_CEMI_NEW_RESET),
            (MGMT_CEMI_ANCHOR_DECL, MGMT_DECL + "\n" + MGMT_CEMI_ANCHOR_DECL),
        )),
    )

    if not all(os.path.isfile(path) for path, _ in files):
        return

    what = "the ETS access setting has NO effect"
    patched = []

    for path, edits in files:
        source = apply_edits(path, edits, what)
        if source is False:
            return
        if source is not None:
            patched.append((path, source))

    for path, source in patched:
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(source)

    if patched:
        print("patch_knx.py: management lock applied to %s"
              % ", ".join(os.path.basename(p) for p, _ in patched))


patch_management_lock()


# --------------------------------------------------------------------------
# 18. Announce the core version the device really implements
# --------------------------------------------------------------------------
#
# ETS 6 could program the device over routing but not through its own
# tunnel: "the target computer refused the connection 192.168.179.113:3671",
# and not a single frame in the bus monitor. That is a refused TCP connect.
#
# TCP transport belongs to KNXnet/IP Core version 2, and both search
# responses announce exactly that - they put KNX_SERVICE_FAMILY_CORE into the
# supported service families, which platformio.ini sets to 2 so that the
# stack answers SEARCH_REQUEST_EXTENDED. ETS reads the 2 and opens its tunnel
# over TCP. The stack has no TCP at all: every endpoint it hands out is
# IPV4_UDP, so lwIP answers the connect with a reset.
#
# The description response already says core 1, hard coded. Both search
# responses now say the same. The extended search stays compiled in and
# answered - ETS needs it to find a device in programming mode - it just no
# longer promises a transport the device cannot provide.

CORE_MARKER = "// sbip: core 1 - no TCP here, see patch 18 in scripts/patch_knx.py"

CORE_ANCHOR = "    _supportedServices.serviceVersion(Core, KNX_SERVICE_FAMILY_CORE);\n"

CORE_NEW = (
    "    " + CORE_MARKER + "\n"
    "    _supportedServices.serviceVersion(Core, 1);\n"
)


def patch_core_version():
    base = os.path.join(
        env["PROJECT_LIBDEPS_DIR"],  # noqa: F821
        env["PIOENV"],  # noqa: F821
        "knx", "src", "knx",
    )

    patched = []
    for name in ("knx_ip_search_response.cpp", "knx_ip_search_response_extended.cpp"):
        path = os.path.join(base, name)
        if not os.path.isfile(path):
            continue

        with open(path, "r", encoding="utf-8") as handle:
            source = handle.read()

        if CORE_MARKER in source:
            continue

        if source.count(CORE_ANCHOR) != 1:
            sys.stderr.write(
                "patch_knx.py: anchor no longer unique in %s, the search "
                "response keeps announcing core %s - ETS 6 will try its tunnel "
                "over TCP and be refused\n" % (name, "2")
            )
            continue

        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(source.replace(CORE_ANCHOR, CORE_NEW))
        patched.append(name)

    if patched:
        print("patch_knx.py: core version 1 announced in %s" % ", ".join(patched))


patch_core_version()


# ==========================================================================
# KNX Secure, patches 19 - 23. See SECURE.md for the whole picture.
# ==========================================================================
#
# All of them apply only with -DUSE_DATASECURE, which platformio.ini sets.
# Each one is all-or-nothing per file, like patch 17: a security check that
# covers some paths and not others is worse than none, because the dashboard
# would claim otherwise.

def apply_secure_edits(name, marker, edits, what):
    path = knx_source(name)
    if not os.path.isfile(path):
        return False

    with open(path, "r", encoding="utf-8") as handle:
        source = handle.read()

    if marker in source:
        return False

    for anchor, _ in edits:
        if source.count(anchor) != 1:
            sys.stderr.write(
                "patch_knx.py: anchor no longer unique in %s, %s:\n  %s\n"
                % (name, what, anchor.strip().splitlines()[0])
            )
            return False

    for anchor, replacement in edits:
        source = source.replace(anchor, replacement)

    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(source)

    return True


# --------------------------------------------------------------------------
# 19. The KNXnet/IP Secure properties of the IP parameter object
# --------------------------------------------------------------------------
#
# PID 79 tunnelling addresses, 91 backbone key, 92 device authentication
# code, 93 password hashes, 94 secured service families, 95 multicast
# latency tolerance, 96 sync latency fraction, 97 tunnelling users.
#
# They live in the firmware (src/knxip_secure.cpp), not in the property:
# a DataProperty is part of the stack's memory image, and adding one shifts
# every object after it - a device updated to this firmware would read its
# filter table from the wrong offset. Callback properties save nothing, so
# the image of an already programmed device stays valid.
#
# 94 is a function property (PDT_FUNCTION in the KNX property list): command
# [0, service 0, family, on/off], state [0, service 0, family].

SEC19_MARKER = "// sbip: KNXnet/IP Secure properties, see src/knxip_secure.cpp"

SEC19_ANCHOR_INC = '#include "callback_property.h"\n'

SEC19_INC = (
    '#include "callback_property.h"\n'
    '#include "function_property.h"\n'
    "\n"
    + SEC19_MARKER + "\n"
    "extern uint8_t (*sbipIpSecureRead)(uint8_t pid, uint16_t start, uint8_t count, uint8_t* data);\n"
    "extern uint8_t (*sbipIpSecureWrite)(uint8_t pid, uint16_t start, uint8_t count, const uint8_t* data);\n"
    "extern void (*sbipIpSecureFunction)(bool command, uint8_t pid, uint8_t* data, uint8_t length,\n"
    "                                    uint8_t* result, uint8_t& resultLength);\n"
)

SEC19_ANCHOR_PROPS = (
    "        new DataProperty(PID_FRIENDLY_NAME, true, PDT_UNSIGNED_CHAR, 30, ReadLv3 | WriteLv3)\n"
    "    };\n"
)


def sec19_property(pid, pdt, count):
    return (
        "        new CallbackProperty<IpParameterObject>(this, (PropertyID)%d, true, %s, %s, ReadLv3 | WriteLv3,\n"
        "            [](IpParameterObject*, uint16_t start, uint8_t count, uint8_t* data) -> uint8_t\n"
        "            { return sbipIpSecureRead ? sbipIpSecureRead(%d, start, count, data) : 0; },\n"
        "            [](IpParameterObject*, uint16_t start, uint8_t count, const uint8_t* data) -> uint8_t\n"
        "            { return sbipIpSecureWrite ? sbipIpSecureWrite(%d, start, count, data) : 0; }),\n"
        % (pid, pdt, count, pid, pid)
    )


SEC19_PROPS = (
    "        new DataProperty(PID_FRIENDLY_NAME, true, PDT_UNSIGNED_CHAR, 30, ReadLv3 | WriteLv3),\n"
    "        " + SEC19_MARKER + "\n"
    + sec19_property(79, "PDT_UNSIGNED_CHAR", "KNX_TUNNELING")
    + sec19_property(91, "PDT_GENERIC_16", "1")
    + sec19_property(92, "PDT_GENERIC_16", "1")
    + sec19_property(93, "PDT_GENERIC_16", "(KNX_TUNNELING + 1)")
    + "        new FunctionProperty<IpParameterObject>(this, (PropertyID)94,\n"
    "            [](IpParameterObject*, uint8_t* data, uint8_t length, uint8_t* result, uint8_t& resultLength) -> void\n"
    "            { if (sbipIpSecureFunction) sbipIpSecureFunction(true, 94, data, length, result, resultLength); else resultLength = 0; },\n"
    "            [](IpParameterObject*, uint8_t* data, uint8_t length, uint8_t* result, uint8_t& resultLength) -> void\n"
    "            { if (sbipIpSecureFunction) sbipIpSecureFunction(false, 94, data, length, result, resultLength); else resultLength = 0; }),\n"
    + sec19_property(95, "PDT_UNSIGNED_INT", "1")
    + sec19_property(96, "PDT_SCALING", "1")
    + sec19_property(97, "PDT_GENERIC_02", "(2 * KNX_TUNNELING)").rstrip(",\n") + "\n"
    "    };\n"
)


def patch_ipsecure_properties():
    if apply_secure_edits(
        "ip_parameter_object.cpp", SEC19_MARKER,
        ((SEC19_ANCHOR_INC, SEC19_INC), (SEC19_ANCHOR_PROPS, SEC19_PROPS)),
        "the KNXnet/IP Secure properties are MISSING - ETS cannot write the keys",
    ):
        print("patch_knx.py: KNXnet/IP Secure properties added to ip_parameter_object.cpp")


patch_ipsecure_properties()


# --------------------------------------------------------------------------
# 20. Offer a connecting secure user only the tunnels assigned to it
# --------------------------------------------------------------------------
#
# The stack hands the first free tunnel to whoever connects. With secure
# tunnelling a user may only use the addresses PID 97 assigns to it; the
# firmware knows which user sent the CONNECT_REQUEST and answers per slot.

SEC20_MARKER = "// sbip: tunnel slots per secure user, see src/knxip_shim.cpp"

SEC20_ANCHOR_DECL = (
    "void IpDataLinkLayer::loopHandleConnectRequest(uint8_t* buffer, uint16_t length,"
    " uint32_t& src_addr, uint16_t& src_port)\n"
)

SEC20_DECL = (
    SEC20_MARKER + "\n"
    "extern bool (*sbipTunnelSlotHook)(uint8_t slot);\n"
    "\n"
    + SEC20_ANCHOR_DECL
)

SEC20_ANCHOR_FREE = (
    "            if (tunnels[i].ChannelId == 0 && firstFreeTunnel < 0)\n"
    "                firstFreeTunnel = i;\n"
)

SEC20_FREE = (
    "            if (tunnels[i].ChannelId == 0 && firstFreeTunnel < 0 &&\n"
    "                    (sbipTunnelSlotHook == nullptr || sbipTunnelSlotHook(i)))\n"
    "                firstFreeTunnel = i;\n"
)


def patch_tunnel_slots():
    if apply_secure_edits(
        "ip_data_link_layer.cpp", SEC20_MARKER,
        ((SEC20_ANCHOR_DECL, SEC20_DECL), (SEC20_ANCHOR_FREE, SEC20_FREE)),
        "secure users get ANY free tunnel",
    ):
        print("patch_knx.py: tunnel slot hook applied to ip_data_link_layer.cpp")


patch_tunnel_slots()


# --------------------------------------------------------------------------
# 21. Keep the security object out of the memory image, give it a real FDSK
# --------------------------------------------------------------------------
#
# The coupler BAU registers its security object with the memory image right
# after the device object. Switching USE_DATASECURE on would therefore move
# every object behind it, and a device updated from a firmware without it
# would restore its filter table from the wrong place. The firmware keeps the
# object in NVS instead (src/knx_link.cpp).
#
# configured() demanded a loaded security object as well. A device ETS
# programs without security never loads it, so it would count as
# unprogrammed forever - and the firmware then forwards every group telegram
# unfiltered. Only a device in secure mode needs it.
#
# The FDSK was a constant, 00 01 02 .. 0F, on every device built with this
# stack. The factory reset now restores the device's own one, and for the
# erase codes that keep the address too.

SEC21_MARKER = "// sbip: security object kept by the firmware, see scripts/patch_knx.py 21"

SEC21_COUPLER_ANCHOR = (
    "    _memory.addSaveRestore(&_deviceObj);\n"
    "#ifdef USE_DATASECURE\n"
    "    _memory.addSaveRestore(&_secIfObj);\n"
    "#endif\n"
)

SEC21_COUPLER_NEW = (
    "    _memory.addSaveRestore(&_deviceObj);\n"
    "    " + SEC21_MARKER + "\n"
)

SEC21_CONF_ANCHOR = (
    "    _configured = _routerObj.loadState() == LS_LOADED;\n"
    "#ifdef USE_DATASECURE\n"
    "    _configured &= _secIfObj.loadState() == LS_LOADED;\n"
    "#endif\n"
)

SEC21_CONF_NEW = (
    "    _configured = _routerObj.loadState() == LS_LOADED;\n"
    "#ifdef USE_DATASECURE\n"
    "    " + SEC21_MARKER + "\n"
    "    // Only a device in secure mode was given a security object to load.\n"
    "    _configured &= _secIfObj.loadState() == LS_LOADED || !_secIfObj.isSecurityModeEnabled();\n"
    "#endif\n"
)

SEC21_SIO_ANCHOR_DECL = "SecurityInterfaceObject::SecurityInterfaceObject()\n"

SEC21_SIO_DECL = (
    SEC21_MARKER + "\n"
    "extern const uint8_t* (*sbipFdskHook)();\n"
    "\n"
    + SEC21_SIO_ANCHOR_DECL
)

SEC21_SIO_ANCHOR_RESET = (
    "    if (eraseCode == FactoryReset)\n"
    "    {\n"
    "        // TODO handle different erase codes\n"
    "        println(\"Factory reset of security interface object requested.\");\n"
    "        setSecurityMode(false);\n"
    "        property(PID_TOOL_KEY)->write(1, 1, _fdsk);\n"
    "    }\n"
)

SEC21_SIO_RESET = (
    "    if (eraseCode == FactoryReset || eraseCode == FactoryResetWithoutIA)\n"
    "    {\n"
    "        println(\"Factory reset of security interface object requested.\");\n"
    "        setSecurityMode(false);\n"
    "        loadState(LS_UNLOADED);\n"
    "        property(PID_TOOL_KEY)->write(1, 1, sbipFdskHook ? sbipFdskHook() : _fdsk);\n"
    "    }\n"
)


def patch_security_object():
    done = []

    if apply_secure_edits(
        "bau_systemB_coupler.cpp", SEC21_MARKER,
        ((SEC21_COUPLER_ANCHOR, SEC21_COUPLER_NEW),),
        "the memory image of programmed devices would be SHIFTED",
    ):
        done.append("bau_systemB_coupler.cpp")

    if apply_secure_edits(
        "bau091A.cpp", SEC21_MARKER,
        ((SEC21_CONF_ANCHOR, SEC21_CONF_NEW),),
        "a device without secure mode never counts as programmed",
    ):
        done.append("bau091A.cpp")

    if apply_secure_edits(
        "security_interface_object.cpp", SEC21_MARKER,
        ((SEC21_SIO_ANCHOR_DECL, SEC21_SIO_DECL),
         (SEC21_SIO_ANCHOR_RESET, SEC21_SIO_RESET)),
        "the factory reset restores the stack's constant FDSK",
    ):
        done.append("security_interface_object.cpp")

    if done:
        print("patch_knx.py: security object patched in %s" % ", ".join(done))


patch_security_object()


# --------------------------------------------------------------------------
# 22. Secure application layer: randomness, sequence numbers, auth-only MAC,
#     no plain text in the log
# --------------------------------------------------------------------------
#
# getRandomNumber() returned 0x000102030405. It is the challenge of every
# S-A_Sync_Req and the mask of every S-A_Sync_Res.
#
# The sending sequence numbers lived in RAM only and started over after a
# restart: 50 under the tool key. The ETS had long accepted higher ones and
# discards everything the device sends. They now come from and go to the
# firmware's high-water marks, and a value ETS writes into
# PID_SEQUENCE_NUMBER_SENDING (secure commissioning does exactly that) is
# honoured instead of ignored. The last number accepted from the tool is
# kept as well - without it a recorded frame could be replayed after a
# restart.
#
# The authentication-only MAC had three faults against AN158: the SCF was
# missing from the additional data, B0 announced the APDU as payload, and
# the first cipher block was taken instead of the last. The decrypt side
# also overwrote the received APDU with the SCF and sequence number. ETS uses
# authentication + confidentiality for tool access, so management was never
# affected, but group telegrams with authentication only were.
#
# The file prints every frame it handles, and the plain text of every frame
# it decrypts. With the log in the dashboard that included the tool key ETS
# writes during commissioning. Its prints are compiled out.

SEC22_MARKER = "// sbip: secure application layer, see scripts/patch_knx.py 22"

SEC22_ANCHOR_INC = '#include "aes.hpp"\n'

SEC22_INC = (
    '#include "aes.hpp"\n'
    "\n"
    + SEC22_MARKER + "\n"
    "extern void (*sbipRandomHook)(uint8_t* out, size_t length);\n"
    "extern uint64_t (*sbipSequenceHook)(uint8_t counter, uint64_t value, bool store);\n"
    "\n"
    "// Every print of this file is a trace of frames and of their plain text.\n"
    "#define print(...) do {} while (0)\n"
    "#define println(...) do {} while (0)\n"
    "#define printHex(...) do {} while (0)\n"
    "#define printPDU() length()\n"
)

SEC22_ANCHOR_RANDOM = (
    "    return 0x000102030405; // TODO: generate random number\n"
)

SEC22_RANDOM = (
    "    uint8_t random[6] = {0};\n"
    "\n"
    "    if (sbipRandomHook)\n"
    "        sbipRandomHook(random, sizeof(random));\n"
    "\n"
    "    return sixBytesToUInt64(random);\n"
)

SEC22_ANCHOR_NEXT = (
    "uint64_t SecureApplicationLayer::nextSequenceNumber(bool toolAccess)\n"
    "{\n"
    "    return toolAccess ? _sequenceNumberToolAccess : _sequenceNumber;\n"
    "}\n"
)

SEC22_NEXT = (
    "uint64_t SecureApplicationLayer::nextSequenceNumber(bool toolAccess)\n"
    "{\n"
    "    // One counter, as AN158 has it: PID_SEQUENCE_NUMBER_SENDING. The stack\n"
    "    // keeps a second one for tool access (PID 250, not in the standard);\n"
    "    // both continue from the highest of everything known - the two members,\n"
    "    // what ETS wrote into either property, and the firmware's high-water\n"
    "    // mark from before the restart.\n"
    "    uint64_t next = _sequenceNumber > _sequenceNumberToolAccess ? _sequenceNumber : _sequenceNumberToolAccess;\n"
    "    const PropertyID pids[2] = {PID_SEQUENCE_NUMBER_SENDING, PID_TOOL_SEQUENCE_NUMBER_SENDING};\n"
    "\n"
    "    for (PropertyID pid : pids)\n"
    "    {\n"
    "        uint8_t written[6] = {0};\n"
    "\n"
    "        if (_secIfObj.property(pid)->read(1, 1, written) == 1 && sixBytesToUInt64(written) > next)\n"
    "            next = sixBytesToUInt64(written);\n"
    "    }\n"
    "\n"
    "    if (sbipSequenceHook)\n"
    "    {\n"
    "        uint64_t floor = sbipSequenceHook(0, next, false);\n"
    "\n"
    "        if (floor > next)\n"
    "            next = floor;\n"
    "    }\n"
    "\n"
    "    if (next == 0)\n"
    "        next = 1;\n"
    "\n"
    "    (toolAccess ? _sequenceNumberToolAccess : _sequenceNumber) = next;\n"
    "    return next;\n"
    "}\n"
)

SEC22_ANCHOR_UPDATE = (
    "    // Also update the properties accordingly\n"
    "    _secIfObj.setSequenceNumber(toolAccess, seqNum);\n"
)

SEC22_UPDATE = (
    "    // Also update the properties accordingly\n"
    "    _secIfObj.setSequenceNumber(toolAccess, seqNum);\n"
    "\n"
    "    if (sbipSequenceHook)\n"
    "        sbipSequenceHook(0, seqNum, true);\n"
)

SEC22_ANCHOR_LASTVALID = (
    "        if (srcAddr == _deviceObj.individualAddress())\n"
    "            return _sequenceNumberToolAccess;\n"
    "\n"
    "        return _lastValidSequenceNumberTool;\n"
)

SEC22_LASTVALID = (
    "        if (srcAddr == _deviceObj.individualAddress())\n"
    "            return _sequenceNumberToolAccess;\n"
    "\n"
    "        if (sbipSequenceHook)\n"
    "        {\n"
    "            uint64_t stored = sbipSequenceHook(1, _lastValidSequenceNumberTool, false);\n"
    "\n"
    "            if (stored > _lastValidSequenceNumberTool)\n"
    "                _lastValidSequenceNumberTool = stored;\n"
    "        }\n"
    "\n"
    "        return _lastValidSequenceNumberTool;\n"
)

SEC22_ANCHOR_UPDVALID = (
    "        // TODO: check if we really have to support multiple tools at the same time\n"
    "        _lastValidSequenceNumberTool = seqNo;\n"
)

SEC22_UPDVALID = (
    "    {\n"
    "        // TODO: check if we really have to support multiple tools at the same time\n"
    "        _lastValidSequenceNumberTool = seqNo;\n"
    "\n"
    "        if (sbipSequenceHook)\n"
    "            sbipSequenceHook(1, seqNo, true);\n"
    "    }\n"
)

SEC22_ANCHOR_AUTHDEF = (
    "uint32_t SecureApplicationLayer::calcAuthOnlyMac(uint8_t* apdu, uint8_t apduLength,"
    " const uint8_t* key, uint8_t* iv, uint8_t* ctr0)\n"
    "{\n"
    "    uint16_t bufLen = 2 + apduLength; // 2 bytes for the length field (uint16_t)\n"
    "    // AES-128 operates on blocks of 16 bytes, add padding\n"
    "    uint16_t bufLenPadded = (bufLen + 15) / 16 * 16;\n"
    "    uint8_t buffer[bufLenPadded];\n"
    "    // Make sure to have zeroes everywhere, because of the padding\n"
    "    memset(buffer, 0x00, bufLenPadded);\n"
    "\n"
    "    uint8_t* pBuf = buffer;\n"
    "\n"
    "    pBuf = pushWord(apduLength, pBuf);\n"
    "    pBuf = pushByteArray(apdu, apduLength, pBuf);\n"
    "\n"
    "    encryptAesCbc(buffer, bufLenPadded, iv, key);\n"
    "    xcryptAesCtr(buffer, 4, ctr0, key); // 4 bytes only for the MAC\n"
    "\n"
    "    uint32_t mac;\n"
    "    popInt(mac, &buffer[0]);\n"
    "\n"
    "    return mac;\n"
    "}\n"
)

SEC22_AUTHDEF = (
    "uint32_t SecureApplicationLayer::calcAuthOnlyMac(uint8_t* apdu, uint8_t apduLength,"
    " const uint8_t* key, uint8_t* iv, uint8_t* ctr0, uint8_t scf)\n"
    "{\n"
    "    // AN158: the additional data are SCF and APDU, B0 announces no payload,\n"
    "    // and the MAC is the last cipher block.\n"
    "    iv[15] = 0x00;\n"
    "\n"
    "    uint16_t adLength = 1 + apduLength;\n"
    "    uint16_t bufLen = 2 + adLength;\n"
    "    uint16_t bufLenPadded = (bufLen + 15) / 16 * 16;\n"
    "    uint8_t buffer[bufLenPadded];\n"
    "    memset(buffer, 0x00, bufLenPadded);\n"
    "\n"
    "    uint8_t* pBuf = buffer;\n"
    "    pBuf = pushWord(adLength, pBuf);\n"
    "    pBuf = pushByte(scf, pBuf);\n"
    "    pBuf = pushByteArray(apdu, apduLength, pBuf);\n"
    "\n"
    "    encryptAesCbc(buffer, bufLenPadded, iv, key);\n"
    "\n"
    "    uint8_t macBytes[4];\n"
    "    memcpy(macBytes, &buffer[bufLenPadded - 16], 4);\n"
    "    xcryptAesCtr(macBytes, 4, ctr0, key);\n"
    "\n"
    "    uint32_t mac;\n"
    "    popInt(mac, macBytes);\n"
    "\n"
    "    return mac;\n"
    "}\n"
)

SEC22_ANCHOR_AUTHDEC = (
    "        uint32_t calculatedMac = calcAuthOnlyMac(plainApdu, remainingPlainApduLength, key, iv, ctr0);\n"
)

SEC22_AUTHDEC = (
    "        uint32_t calculatedMac = calcAuthOnlyMac(plainApdu, remainingPlainApduLength, key, iv, ctr0, scf);\n"
)

SEC22_ANCHOR_AUTHCOPY = (
    "        memcpy(plainApdu, secureAsdu, remainingPlainApduLength);\n"
)

SEC22_AUTHCOPY = (
    "        // sbip: plainApdu already holds the APDU; secureAsdu points at the SCF.\n"
)

SEC22_ANCHOR_AUTHENC = (
    "        uint32_t tmpMac = calcAuthOnlyMac(apdu, apduLength, key, iv, ctr0);\n"
)

SEC22_AUTHENC = (
    "        uint32_t tmpMac = calcAuthOnlyMac(apdu, apduLength, key, iv, ctr0, scf);\n"
)

SEC22_H_ANCHOR = (
    "        uint32_t calcAuthOnlyMac(uint8_t* apdu, uint8_t apduLength, const uint8_t* key,"
    " uint8_t* iv, uint8_t* ctr0);\n"
)

SEC22_H_NEW = (
    "        " + SEC22_MARKER + "\n"
    "        uint32_t calcAuthOnlyMac(uint8_t* apdu, uint8_t apduLength, const uint8_t* key,"
    " uint8_t* iv, uint8_t* ctr0, uint8_t scf);\n"
)


def patch_secure_application_layer():
    done = []

    if apply_secure_edits(
        "secure_application_layer.h", SEC22_MARKER,
        ((SEC22_H_ANCHOR, SEC22_H_NEW),),
        "the authentication-only MAC stays wrong",
    ):
        done.append("secure_application_layer.h")

    if apply_secure_edits(
        "secure_application_layer.cpp", SEC22_MARKER,
        ((SEC22_ANCHOR_INC, SEC22_INC),
         (SEC22_ANCHOR_RANDOM, SEC22_RANDOM),
         (SEC22_ANCHOR_NEXT, SEC22_NEXT),
         (SEC22_ANCHOR_UPDATE, SEC22_UPDATE),
         (SEC22_ANCHOR_LASTVALID, SEC22_LASTVALID),
         (SEC22_ANCHOR_UPDVALID, SEC22_UPDVALID),
         (SEC22_ANCHOR_AUTHDEF, SEC22_AUTHDEF),
         (SEC22_ANCHOR_AUTHDEC, SEC22_AUTHDEC),
         (SEC22_ANCHOR_AUTHCOPY, SEC22_AUTHCOPY),
         (SEC22_ANCHOR_AUTHENC, SEC22_AUTHENC)),
        "Data Secure uses a CONSTANT challenge and forgets its sequence numbers",
    ):
        done.append("secure_application_layer.cpp")

    if done:
        print("patch_knx.py: secure application layer patched in %s" % ", ".join(done))


patch_secure_application_layer()


# --------------------------------------------------------------------------
# 23. Access policies for management services
# --------------------------------------------------------------------------
#
# The stack decrypts S-A_Data and passes the security it found on, but
# nothing ever looks at it: in secure mode a plain A_PropertyValue_Write is
# executed like a secured one, and the key properties can be read by anyone.
# 3/5/1 assigns every service and property an access policy by role (tool,
# runtime key, none) and security (none, auth, auth+conf), separately for
# secure mode on and off. src/knx_access_policy.cpp holds the table and
# decides; here it is asked at the three places the application layer hands
# incoming services to the BAU, and by the cEMI server for local management.
#
# A refused property service is answered the way the specification asks for
# an inaccessible property - no elements, or AccessDenied - so ETS reports
# the refusal instead of running into a timeout. Everything else is dropped.

SEC23_MARKER = "// sbip: access policies, see src/knx_access_policy.cpp"

SEC23_AL_ANCHOR_DECL = "void ApplicationLayer::transportLayer(TransportLayer& layer)\n"

SEC23_AL_DECL = (
    SEC23_MARKER + "\n"
    "// 0 allowed, 1 denied, 2 A_DeviceDescriptor_Read to be answered with FFFFh\n"
    "extern uint8_t (*sbipAccessHook)(uint16_t apduType, const uint8_t* data, uint16_t length,\n"
    "                                 bool toolAccess, uint8_t dataSecurity);\n"
    "\n"
    "static uint8_t sbipAccessCheck(APDU& apdu, const SecurityControl& secCtrl)\n"
    "{\n"
    "    if (sbipAccessHook == nullptr)\n"
    "        return 0;\n"
    "\n"
    "    return sbipAccessHook(apdu.type(), apdu.data(), apdu.length(), secCtrl.toolAccess,\n"
    "                          (uint8_t)secCtrl.dataSecurity);\n"
    "}\n"
    "\n"
    "// A refused service answered the way AN193 and 3/3/7 ask for an\n"
    "// inaccessible resource. Anything not listed is dropped.\n"
    "static void sbipAccessDenied(ApplicationLayer& al, HopCountType hopType, Priority priority,\n"
    "                             uint16_t tsap, APDU& apdu, const SecurityControl& secCtrl, uint8_t verdict)\n"
    "{\n"
    "    const uint8_t* data = apdu.data();\n"
    "    uint8_t denied = ReturnCodes::AccessDenied;\n"
    "\n"
    "    switch (apdu.type())\n"
    "    {\n"
    "        case DeviceDescriptorRead:\n"
    "        {\n"
    "            // AN193 2.2.4.5: type 0 as FFFFh, any other type as 3Fh.\n"
    "            uint8_t descriptorType = data[0] & 0x3f;\n"
    "            uint8_t voidDescriptor[2] = {0xff, 0xff};\n"
    "\n"
    "            if (verdict == 2)\n"
    "                al.deviceDescriptorReadResponse(AckRequested, priority, hopType, tsap, secCtrl,\n"
    "                                                descriptorType == 0 ? 0 : 0x3f, voidDescriptor);\n"
    "            break;\n"
    "        }\n"
    "\n"
    "        case PropertyValueRead:\n"
    "        case PropertyValueWrite:\n"
    "        {\n"
    "            uint16_t startIndex = ((data[3] & 0x0f) << 8) | data[4];\n"
    "            al.propertyValueReadResponse(AckRequested, priority, hopType, tsap, secCtrl,\n"
    "                                         data[1], data[2], 0, startIndex, nullptr, 0);\n"
    "            break;\n"
    "        }\n"
    "\n"
    "        case PropertyDescriptionRead:\n"
    "            // AN193 2.2.1: write_enable, type, max_nr_of_elem and access zero.\n"
    "            al.propertyDescriptionReadResponse(AckRequested, priority, hopType, tsap, secCtrl,\n"
    "                                               data[1], data[2], data[3], false, 0, 0, 0);\n"
    "            break;\n"
    "\n"
    "        case PropertyExtDescriptionRead:\n"
    "        {\n"
    "            uint16_t objectType = (data[1] << 8) | data[2];\n"
    "            uint16_t objectInstance = (data[3] << 4) | (data[4] >> 4);\n"
    "            uint16_t propertyId = ((data[4] & 0x0f) << 8) | data[5];\n"
    "            uint8_t descriptionType = (data[6] & 0xf0) >> 4;\n"
    "            uint16_t propertyIndex = ((data[7] & 0x0f) << 8) | data[8];\n"
    "            al.propertyExtDescriptionReadResponse(AckRequested, priority, hopType, tsap, secCtrl, objectType,\n"
    "                                                  objectInstance, propertyId, propertyIndex, descriptionType,\n"
    "                                                  false, 0, 0, 0);\n"
    "            break;\n"
    "        }\n"
    "\n"
    "        case PropertyValueExtRead:\n"
    "        case PropertyValueExtWriteCon:\n"
    "        {\n"
    "            uint16_t objectType = (data[1] << 8) | data[2];\n"
    "            uint8_t objectInstance = (data[3] << 4) | (data[4] >> 4);\n"
    "            uint16_t propertyId = ((data[4] & 0x0f) << 8) | data[5];\n"
    "            uint16_t startIndex = ((data[7] & 0x0f) << 8) | data[8];\n"
    "\n"
    "            if (apdu.type() == PropertyValueExtRead)\n"
    "                al.propertyValueExtReadResponse(AckRequested, priority, hopType, tsap, secCtrl, objectType,\n"
    "                                                objectInstance, propertyId, 0, startIndex, nullptr, 0);\n"
    "            else\n"
    "                al.propertyValueExtWriteConResponse(AckRequested, priority, hopType, tsap, secCtrl, objectType,\n"
    "                                                    objectInstance, propertyId, data[6], startIndex, denied);\n"
    "            break;\n"
    "        }\n"
    "\n"
    "        case FunctionPropertyCommand:\n"
    "        case FunctionPropertyState:\n"
    "            al.functionPropertyStateResponse(AckRequested, priority, hopType, tsap, secCtrl,\n"
    "                                             data[1], data[2], &denied, 1);\n"
    "            break;\n"
    "\n"
    "        case FunctionPropertyExtCommand:\n"
    "        case FunctionPropertyExtState:\n"
    "        {\n"
    "            uint16_t objectType = (data[1] << 8) | data[2];\n"
    "            uint8_t objectInstance = (data[3] << 4) | (data[4] >> 4);\n"
    "            uint16_t propertyId = ((data[4] & 0x0f) << 8) | data[5];\n"
    "            al.functionPropertyExtStateResponse(AckRequested, priority, hopType, tsap, secCtrl, objectType,\n"
    "                                                objectInstance, propertyId, &denied, 1);\n"
    "            break;\n"
    "        }\n"
    "\n"
    "        default:\n"
    "            break;\n"
    "    }\n"
    "}\n"
    "\n"
    + SEC23_AL_ANCHOR_DECL
)

SEC23_AL_ANCHOR_IND = (
    "void ApplicationLayer::individualIndication(HopCountType hopType, Priority priority,"
    " uint16_t tsap, APDU& apdu, const SecurityControl& secCtrl)\n"
    "{\n"
    "    uint8_t* data = apdu.data();\n"
)

SEC23_AL_IND = (
    SEC23_AL_ANCHOR_IND
    + "\n"
    "    uint8_t sbipVerdict = sbipAccessCheck(apdu, secCtrl);\n"
    "\n"
    "    if (sbipVerdict != 0)\n"
    "    {\n"
    "        sbipAccessDenied(*this, hopType, priority, tsap, apdu, secCtrl, sbipVerdict);\n"
    "        return;\n"
    "    }\n"
)

SEC23_AL_ANCHOR_BC = (
    "void ApplicationLayer::dataBroadcastIndication(HopCountType hopType, Priority priority,"
    " uint16_t source, APDU& apdu, const SecurityControl& secCtrl)\n"
    "{\n"
    "    uint8_t* data = apdu.data();\n"
)

SEC23_AL_BC = (
    SEC23_AL_ANCHOR_BC
    + "\n"
    "    if (sbipAccessCheck(apdu, secCtrl) != 0)\n"
    "        return;\n"
)

SEC23_AL_ANCHOR_SBC = (
    "void ApplicationLayer::dataSystemBroadcastIndication(HopCountType hopType, Priority priority,"
    " uint16_t source, APDU& apdu, const SecurityControl& secCtrl)\n"
    "{\n"
    "    const uint8_t* data = apdu.data();\n"
)

SEC23_AL_SBC = (
    SEC23_AL_ANCHOR_SBC
    + "\n"
    "    if (sbipAccessCheck(apdu, secCtrl) != 0)\n"
    "        return;\n"
)

SEC23_CEMI_ANCHOR_DECL = "void CemiServer::frameReceived(CemiFrame& frame)\n"

SEC23_CEMI_DECL = (
    SEC23_MARKER + "\n"
    "extern bool (*sbipCemiAccessHook)(uint16_t objectType, uint8_t propertyId, bool write);\n"
    "\n"
    + SEC23_CEMI_ANCHOR_DECL
)

SEC23_CEMI_ANCHOR_SWITCH = (
    "{\n"
    "    switch (frame.messageCode())\n"
    "    {\n"
    "        case L_data_req:\n"
    "        {\n"
    "            handleLData(frame);\n"
)

SEC23_CEMI_SWITCH = (
    "{\n"
    "    // A refused property access gets no elements: the handlers answer\n"
    "    // that with a negative confirmation.\n"
    "    if ((frame.messageCode() == M_PropRead_req || frame.messageCode() == M_PropWrite_req) &&\n"
    "            sbipCemiAccessHook != nullptr)\n"
    "    {\n"
    "        uint16_t objectType = (frame.data()[1] << 8) | frame.data()[2];\n"
    "\n"
    "        if (!sbipCemiAccessHook(objectType, frame.data()[4], frame.messageCode() == M_PropWrite_req))\n"
    "            frame.data()[5] &= 0x0F;\n"
    "    }\n"
    "\n"
    "    switch (frame.messageCode())\n"
    "    {\n"
    "        case L_data_req:\n"
    "        {\n"
    "            handleLData(frame);\n"
)


def patch_access_policies():
    done = []

    if apply_secure_edits(
        "application_layer.cpp", SEC23_MARKER,
        ((SEC23_AL_ANCHOR_DECL, SEC23_AL_DECL),
         (SEC23_AL_ANCHOR_IND, SEC23_AL_IND),
         (SEC23_AL_ANCHOR_BC, SEC23_AL_BC),
         (SEC23_AL_ANCHOR_SBC, SEC23_AL_SBC)),
        "management is NOT checked against the access policies",
    ):
        done.append("application_layer.cpp")

    if apply_secure_edits(
        "cemi_server.cpp", SEC23_MARKER,
        ((SEC23_CEMI_ANCHOR_DECL, SEC23_CEMI_DECL),
         (SEC23_CEMI_ANCHOR_SWITCH, SEC23_CEMI_SWITCH)),
        "local cEMI management is NOT checked against the access policies",
    ):
        done.append("cemi_server.cpp")

    if done:
        print("patch_knx.py: access policies applied to %s" % ", ".join(done))


patch_access_policies()


# --------------------------------------------------------------------------
# 24. Announce the KNXnet/IP capabilities the device has
# --------------------------------------------------------------------------
#
# PID_KNXNETIP_DEVICE_CAPABILITIES (3/8/3, 2.5.19) is a bit set of the
# supported service families: 0 device management, 1 tunnelling, 2 routing,
# 6 security. The stack answers 0001h, device management alone, although it
# tunnels and routes. 03_08_09 2.6.2.1 asks for the security bit as well.

SEC24_MARKER = "// sbip: device management, tunnelling, routing, security (3/8/3 2.5.19)"

SEC24_ANCHOR = "            pushWord(0x1, data);\n"

SEC24_NEW = (
    "            " + SEC24_MARKER + "\n"
    "            pushWord(0x0047, data);\n"
)


def patch_device_capabilities():
    if apply_secure_edits(
        "ip_parameter_object.cpp", SEC24_MARKER,
        ((SEC24_ANCHOR, SEC24_NEW),),
        "PID_KNXNETIP_DEVICE_CAPABILITIES keeps claiming device management only",
    ):
        print("patch_knx.py: KNXnet/IP device capabilities corrected in ip_parameter_object.cpp")


patch_device_capabilities()


# --------------------------------------------------------------------------
# 25. No extended frames to a TP-UART that cannot send them
# --------------------------------------------------------------------------
#
# The TP-UART 2 emulator on the SB-Interface does not handle extended frames
# yet - the SBLib underneath has no support for them. A KNX Data Secure frame
# carries 13 octets of overhead, so almost every secured management frame is
# an extended one. Handed to the emulator it is lost or garbled on the line;
# refused here, the sender gets a negative L_Data.con and knows. The router
# object announces the same limit in PID_MAX_APDULENGTH_ROUTING (see
# src/knx_link.cpp), so ETS does not try in the first place.

SEC25_MARKER = "// sbip: extended frames only if the TP-UART can send them"

SEC25_ANCHOR_DECL = "bool TpUartDataLinkLayer::sendFrame(CemiFrame& cemiFrame)\n"

SEC25_DECL = (
    SEC25_MARKER + "\n"
    "extern bool sbipTpExtendedFrames;\n"
    "\n"
    + SEC25_ANCHOR_DECL
)

SEC25_ANCHOR = "    TpFrame* tpFrame = new TpFrame(cemiFrame);\n"

SEC25_NEW = (
    "    if (!sbipTpExtendedFrames && cemiFrame.frameType() == ExtendedFrame)\n"
    "    {\n"
    "        println(\"sbip: extended frame not sent - the TP-UART cannot send it\");\n"
    "        dataConReceived(cemiFrame, false);\n"
    "        return false;\n"
    "    }\n"
    "\n"
    + SEC25_ANCHOR
)


def patch_tp_extended_frames():
    if apply_secure_edits(
        "tpuart_data_link_layer.cpp", SEC25_MARKER,
        ((SEC25_ANCHOR_DECL, SEC25_DECL), (SEC25_ANCHOR, SEC25_NEW)),
        "extended frames go to a TP-UART that cannot send them",
    ):
        print("patch_knx.py: extended frame guard applied to tpuart_data_link_layer.cpp")


patch_tp_extended_frames()
