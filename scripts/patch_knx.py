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
# 3. Manufacturer specific properties of the emulated product
# --------------------------------------------------------------------------

IPPARAM = os.path.join(
    env["PROJECT_LIBDEPS_DIR"],  # noqa: F821
    env["PIOENV"],  # noqa: F821
    "knx",
    "src",
    "knx",
    "ip_parameter_object.cpp",
)

IPPARAM_MARKER = "// sbip: manufacturer specific, written by the ABB load procedure"

IPPARAM_ANCHOR = (
    "        new DataProperty(PID_IP_ASSIGNMENT_METHOD, true, PDT_UNSIGNED_CHAR, 1,"
    " ReadLv3 | WriteLv3),\n"
)

# The ABB IPR/S 3.1.1 load procedure writes these two:
#     <LdCtrlWriteProp ObjType="11" PropId="204" Verify="false" />
#     <LdCtrlWriteProp ObjType="11" PropId="209" Verify="false" />
# The knxprod does not say what goes in them, and Verify="false" means ETS
# never reads them back - they only have to exist, otherwise the write fails
# and the download stops. Declared as byte arrays so any length ETS sends
# fits; adjust once the monitor shows the real size.
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

    if IPPARAM_MARKER in source:
        return

    if source.count(IPPARAM_ANCHOR) != 1:
        sys.stderr.write(
            "patch_knx.py: anchor for the manufacturer properties not found, "
            "an ABB download will fail on ObjType 11 PropId 204/209\n"
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


if patch_monitor():
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
