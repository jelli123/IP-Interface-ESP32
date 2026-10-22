/*
 *  security_proxy_test.cpp - Host test of the Security Proxy (AN192 v06)
 *  in src/knx_proxy_core.cpp.
 *
 *  The frames are checked against bussard's Data Secure group vector, the
 *  same one test/secure_crypto_test.cpp uses for the bare primitives: key
 *  00..0F, sequence 42, 1.1.1 -> 1/2/3, APDU 00 81. The white lists follow
 *  Example 3 of AN192 2.3.1.3.6.1.
 */

#include <stdio.h>
#include <string.h>

#include "knx_proxy_core.h"

using namespace knxproxy;

static int failures = 0;

static size_t hex(const char* text, uint8_t* out)
{
    size_t n = 0;
    while (*text)
    {
        if (*text == ' ')
        {
            text++;
            continue;
        }
        unsigned value;
        sscanf(text, "%2x", &value);
        out[n++] = (uint8_t)value;
        text += 2;
    }
    return n;
}

static void expectTrue(const char* name, bool ok)
{
    printf("%s %s\n", ok ? "ok   " : "FAIL ", name);
    if (!ok) failures++;
}

static void expectBytes(const char* name, const uint8_t* actual, size_t length, const char* expectedHex)
{
    uint8_t expected[300];
    size_t  n = hex(expectedHex, expected);
    expectTrue(name, n == length && memcmp(actual, expected, n) == 0);
}

static GroupKey entry(uint16_t ga, uint8_t settings)
{
    GroupKey e = {};
    e.ga = ga;
    hex("000102030405060708090a0b0c0d0e0f", e.key);
    e.settings = settings;
    return e;
}

static void frames()
{
    uint8_t key[16], plain[2] = {0x00, 0x81}, out[64], back[64];
    hex("000102030405060708090a0b0c0d0e0f", key);

    size_t n = seal(key, 0x1101, 0x0A03, 42, true, plain, 2, out);
    expectBytes("seal auth+conf", out, n, "03f1 10 00000000002a df59 49899fd3");

    uint64_t seq = 0;
    size_t   m   = open(key, 0x1101, 0x0A03, out, n, back, seq);
    expectTrue("open auth+conf", m == 2 && memcmp(back, plain, 2) == 0 && seq == 42);

    out[9] ^= 0x01;
    expectTrue("open refuses a changed APDU", open(key, 0x1101, 0x0A03, out, n, back, seq) == 0);
    out[9] ^= 0x01;
    expectTrue("open refuses another source", open(key, 0x1102, 0x0A03, out, n, back, seq) == 0);

    n = seal(key, 0x1101, 0x0A03, 42, false, plain, 2, out);
    expectBytes("seal auth only", out, n, "03f1 00 00000000002a 0081 2e51ca4a");
    m = open(key, 0x1101, 0x0A03, out, n, back, seq);
    expectTrue("open auth only", m == 2 && memcmp(back, plain, 2) == 0 && seq == 42);

    out[10] ^= 0x80;
    expectTrue("open auth only refuses a changed APDU", open(key, 0x1101, 0x0A03, out, n, back, seq) == 0);

    // A DPT 9 value: four octets of APDU, the first that needs a long frame on TP.
    uint8_t value[4] = {0x00, 0x80, 0x0C, 0x1A};
    n = seal(key, 0x1100, 0x0A03, 7, true, value, 4, out);
    expectTrue("seal adds 13 octets", n == 4 + 13 && SECURE_OVERHEAD == 13);
    m = open(key, 0x1100, 0x0A03, out, n, back, seq);
    expectTrue("round trip of a longer APDU", m == 4 && memcmp(back, value, 4) == 0 && seq == 7);

    uint8_t t[16];
    expectTrue("classify plain", classify(plain, 2) == PLAIN);
    size_t k = hex("03f1 10 00000000002a df59 49899fd3", t);
    expectTrue("classify S-A_Data", classify(t, k) == SECURE_DATA);
    t[2] = 0x12;
    expectTrue("classify S-A_Sync_Req", classify(t, k) == SECURE_SYNC);
    t[2] = 0x13;
    expectTrue("classify S-A_Sync_Res", classify(t, k) == SECURE_SYNC);
    t[2] = 0x10;
    expectTrue("classify a truncated S-A_Data", classify(t, 10) == SECURE_OTHER);
}

static void table8()
{
    // Secure on TP (secondary), plain on IP (primary): the usual case.
    GroupKey tpSecure = entry(0x0A03, SEC_AT_SECONDARY | FLAG_CONF | FLAG_AUTH);
    GroupKey both     = entry(0x0A03, SEC_AT_PRIMARY | SEC_AT_SECONDARY | FLAG_CONF | FLAG_AUTH);
    GroupKey neither  = entry(0x0A03, FLAG_CONF | FLAG_AUTH);
    GroupKey authOnly = entry(0x0A03, SEC_AT_SECONDARY | FLAG_AUTH);
    GroupKey noAuth   = entry(0x0A03, SEC_AT_SECONDARY | FLAG_CONF);

    expectTrue("case 2: plain, not in the table", groupAction(nullptr, PRIMARY, PLAIN, 0) == FORWARD);
    expectTrue("case 5: secure, not in the table", groupAction(nullptr, PRIMARY, SECURE_DATA, 0x10) == FORWARD);
    expectTrue("case 3.1: plain from the plain side", groupAction(&tpSecure, PRIMARY, PLAIN, 0) == ENCRYPT);
    expectTrue("case 3.2: plain from the secure side", groupAction(&tpSecure, SECONDARY, PLAIN, 0) == BLOCK);
    expectTrue("case 4.1: secure from the plain side", groupAction(&tpSecure, PRIMARY, SECURE_DATA, 0x10) == BLOCK);
    expectTrue("case 4.2: secure from the secure side", groupAction(&tpSecure, SECONDARY, SECURE_DATA, 0x10) == DECRYPT);
    expectTrue("rule: secure on both sides", groupAction(&both, SECONDARY, SECURE_DATA, 0x10) == BLOCK);
    expectTrue("rule: plain on both sides", groupAction(&neither, PRIMARY, PLAIN, 0) == BLOCK);
    expectTrue("SAI auth only where conf is due", groupAction(&tpSecure, SECONDARY, SECURE_DATA, 0x00) == BLOCK);
    expectTrue("SAI conf where auth only is due", groupAction(&authOnly, SECONDARY, SECURE_DATA, 0x10) == BLOCK);
    expectTrue("SAI auth only as due", groupAction(&authOnly, SECONDARY, SECURE_DATA, 0x00) == DECRYPT);
    expectTrue("reserved SAI", groupAction(&tpSecure, SECONDARY, SECURE_DATA, 0x30) == BLOCK);
    expectTrue("tool access on a group address", groupAction(&tpSecure, SECONDARY, SECURE_DATA, 0x90) == BLOCK);
    expectTrue("conf without auth", groupAction(&noAuth, PRIMARY, PLAIN, 0) == BLOCK);
    expectTrue("S-A_Sync never crosses", groupAction(nullptr, PRIMARY, SECURE_SYNC, 0x12) == BLOCK);
    expectTrue("unknown secure service on a proxied GA", groupAction(&tpSecure, SECONDARY, SECURE_OTHER, 0x15) == BLOCK);
}

static void groupKeyTable()
{
    static GroupKeyTable t;
    uint8_t element[2 * GROUP_KEY_SIZE], out[2 * GROUP_KEY_SIZE];
    hex("0a03 000102030405060708090a0b0c0d0e0f 43"
        "0a04 101112131415161718191a1b1c1d1e1f 81", element);

    expectTrue("write two entries", t.write(1, 2, element) == 2 && t.count() == 2);
    expectTrue("find 1/2/4", t.find(0x0A04) != nullptr && t.find(0x0A04)->settings == 0x81);
    expectTrue("no 1/2/5", t.find(0x0A05) == nullptr);
    expectTrue("read back", t.read(1, 2, out) == 2 && memcmp(out, element, sizeof(out)) == 0);

    uint8_t count[2];
    expectTrue("element count", t.read(0, 1, count) == 1 && count[0] == 0 && count[1] == 2);
    expectTrue("read beyond the end", t.read(2, 2, out) == 0);
    expectTrue("write beyond the maximum", t.write(MAX_GROUP_KEYS, 2, element) == 0);

    uint8_t blob[2 * GROUP_KEY_SIZE];
    size_t  n = t.serialize(blob, sizeof(blob));
    static GroupKeyTable u;
    u.deserialize(blob, n);
    expectTrue("NVS round trip", u.count() == 2 && u.find(0x0A03) != nullptr &&
                                     memcmp(u.find(0x0A03)->key, element + 2, 16) == 0);

    uint8_t zero[2] = {0, 0};
    expectTrue("clear by element count 0", t.write(0, 1, zero) == 1 && t.count() == 0 && t.find(0x0A03) == nullptr);
}

static void whiteLists()
{
    const uint16_t own = 0x1000;  // 1.0.0
    WhiteLists w;
    uint32_t   now = 1000;

    expectTrue("add 1.0.10 -> 1.1.50 x2", w.add(own, 0x100A, 0x1132, 2, now) == E_SUCCESS);
    expectTrue("add 1.0.10 -> 1.1.60 x2", w.add(own, 0x100A, 0x113C, 2, now) == E_SUCCESS);
    expectTrue("add 1.1.100 -> 1.0.10 x2", w.add(own, 0x1164, 0x100A, 2, now) == E_SUCCESS);

    // AN192 Example 3.
    expectTrue("1.0.10 1.1.50 2 TRUE", w.check(0x100A, 0x1132, 2, now));
    expectTrue("1.0.10 1.1.61 1 TRUE", w.check(0x100A, 0x113D, 1, now));
    expectTrue("1.0.10 1.1.60 4 FALSE", !w.check(0x100A, 0x113C, 4, now));
    expectTrue("1.0.10 1.1.70 1 FALSE", !w.check(0x100A, 0x1146, 1, now));
    expectTrue("1.0.10 1.1.100 1 FALSE", !w.check(0x100A, 0x1164, 1, now));
    expectTrue("1.1.100 1.0.10 1 TRUE", w.check(0x1164, 0x100A, 1, now));
    expectTrue("1.0.40 1.1.50 1 FALSE", !w.check(0x1028, 0x1132, 1, now));
    expectTrue("count 0: any list for the user", w.check(0x100A, 0xFFFF, 0, now));

    expectTrue("allows in both directions", w.allows(0x1133, 0x100A, now) && w.allows(0x100A, 0x1133, now));
    expectTrue("not to 1.1.52", !w.allows(0x100A, 0x1134, now));

    expectTrue("the coupler as user", w.add(own, own, 0x1132, 1, now) == E_DATA_VOID);
    expectTrue("the coupler in the range", w.add(own, 0x100A, 0x0FFF, 2, now) == E_DATA_VOID);
    expectTrue("range overflow", w.add(own, 0x100A, 0xFFFF, 2, now) == E_DATA_VOID);
    expectTrue("count 0 adds nothing", w.add(own, 0x2000, 0x1132, 0, now) == E_SUCCESS && !w.check(0x2000, 0, 0, now));

    expectTrue("retrigger unknown", w.retrigger(0x2000, now) == E_DATA_VOID);
    expectTrue("retrigger 1.0.10", w.retrigger(0x100A, now + 50000) == E_SUCCESS);
    expectTrue("1.1.100 expired after 60 s", !w.allows(0x1164, 0x100A, now + 60000));
    expectTrue("1.0.10 kept by the retrigger", w.allows(0x100A, 0x1132, now + 60000));
    expectTrue("1.0.10 expired 60 s after it", !w.allows(0x100A, 0x1132, now + 110000));

    w.add(own, 0x100A, 0x1132, 1, now);
    w.revoke(0x100A);
    expectTrue("revoked", !w.check(0x100A, 0, 0, now));

    for (uint16_t i = 0; i < MAX_WHITE_LISTS; i++)
        w.add(own, (uint16_t)(0x3000 + i), 0x1132, 1, now);
    expectTrue("no more lists", w.add(own, 0x4000, 0x1132, 1, now) == E_COMMAND_IMPOSSIBLE);
    expectTrue("the full lists are kept", w.active(now) == MAX_WHITE_LISTS);
    expectTrue("expired lists make room", w.add(own, 0x4000, 0x1132, 1, now + 60000) == E_SUCCESS);
}

static void proxy()
{
    static Proxy p;
    uint8_t result[4], resultLength = 0;
    uint32_t now = 5000;

    expectTrue("default: individual routed", p.routeIndividual(PRIMARY, 0x100A, 0x1132, now));
    expectTrue("default: broadcast routed", p.routeBroadcast(PRIMARY, false, now));

    p.ptpConfig(SECONDARY, ROUTE_BLOCK);
    expectTrue("PTP blocked onto TP", !p.routeIndividual(PRIMARY, 0x100A, 0x1132, now));
    expectTrue("PTP still routed onto IP", p.routeIndividual(SECONDARY, 0x1132, 0x2001, now));

    // Add 1.0.10 -> 1.1.50 x2 through the function property.
    uint8_t add[8] = {0x00, 0x00, 0x10, 0x0A, 0x11, 0x32, 0x00, 0x02};
    p.ptpTemp(true, add, sizeof(add), result, resultLength, 0x1000, now);
    expectBytes("PID 83 add answers", result, resultLength, "00 00");
    expectTrue("PTP through the white list", p.routeIndividual(PRIMARY, 0x100A, 0x1133, now));
    expectTrue("and back", p.routeIndividual(SECONDARY, 0x1133, 0x100A, now));

    uint8_t check[8] = {0x00, 0x00, 0x10, 0x0A, 0x11, 0x33, 0x00, 0x01};
    p.ptpTemp(false, check, sizeof(check), result, resultLength, 0x1000, now);
    expectBytes("PID 83 check", result, resultLength, "00 00 01");

    uint8_t bad[2] = {0x00, 0x07};
    p.ptpTemp(false, bad, sizeof(bad), result, resultLength, 0x1000, now);
    expectBytes("PID 83 unknown read service", result, resultLength, "f2 07");
    p.ptpTemp(true, bad, sizeof(bad), result, resultLength, 0x1000, now);
    expectBytes("PID 83 unknown write service", result, resultLength, "f2 07");

    uint8_t revoke[4] = {0x00, 0x01, 0x10, 0x0A};
    p.ptpTemp(true, revoke, sizeof(revoke), result, resultLength, 0x1000, now);
    expectBytes("PID 83 revoke", result, resultLength, "00 01");
    expectTrue("PTP blocked again", !p.routeIndividual(PRIMARY, 0x100A, 0x1133, now));

    p.bcConfig(SECONDARY, ROUTE_BLOCK);
    expectTrue("BC blocked onto TP", !p.routeBroadcast(PRIMARY, false, now));
    expectTrue("SBC blocked onto TP", !p.routeBroadcast(PRIMARY, true, now));

    uint8_t sbcOnTp[3] = {0x00, 0x01, 0x01};
    p.bcTemp(SECONDARY, true, sbcOnTp, 3, result, resultLength, now);
    expectBytes("no SBC mode on TP1", result, resultLength, "f2 01");

    uint8_t bcOn[3] = {0x00, 0x00, 0x01};
    p.bcTemp(SECONDARY, true, bcOn, 3, result, resultLength, now);
    expectBytes("PID 84 BC on", result, resultLength, "00 00");
    expectTrue("BC temporarily routed onto TP", p.routeBroadcast(PRIMARY, false, now + 59000));
    expectTrue("an SBC travels as BC on TP", p.routeBroadcast(PRIMARY, true, now + 59000));
    expectTrue("and stops after 60 s", !p.routeBroadcast(PRIMARY, false, now + 60000));

    uint8_t get[2] = {0x00, 0x00};
    p.bcTemp(SECONDARY, false, get, 2, result, resultLength, now + 60000);
    expectBytes("PID 84 reads disabled", result, resultLength, "00 00 00");

    uint8_t badMode[3] = {0x00, 0x00, 0x02};
    p.bcTemp(SECONDARY, true, badMode, 3, result, resultLength, now);
    expectBytes("PID 84 mode 2", result, resultLength, "f8 00");

    p.bcConfig(PRIMARY, ROUTE_BLOCK);
    uint8_t sbcOn[3] = {0x00, 0x01, 0x01};
    p.bcTemp(PRIMARY, true, sbcOn, 3, result, resultLength, now);
    expectTrue("SBC onto IP", p.routeBroadcast(SECONDARY, true, now));
    expectTrue("but not BC", !p.routeBroadcast(SECONDARY, false, now));

    uint8_t element[GROUP_KEY_SIZE];
    hex("0a03 000102030405060708090a0b0c0d0e0f 43", element);
    p.groupKeys().write(1, 1, element);

    expectTrue("ResetLinks keeps the configuration", !p.masterReset(0x06) && p.groupKeys().count() == 1
               && p.ptpConfig(SECONDARY) == ROUTE_BLOCK && !p.routeBroadcast(SECONDARY, true, now));
    expectTrue("07h clears the keys only", p.masterReset(0x07) && p.groupKeys().count() == 0
               && p.ptpConfig(SECONDARY) == ROUTE_BLOCK);
    expectTrue("02h back to the defaults", p.masterReset(0x02) && p.ptpConfig(SECONDARY) == ROUTE_RULE
               && p.bcConfig(PRIMARY) == ROUTE_RULE);
}

int main()
{
    frames();
    table8();
    groupKeyTable();
    whiteLists();
    proxy();

    printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
