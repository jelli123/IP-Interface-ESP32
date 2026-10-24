/*
 *  knx_secure_diag.cpp - Why Data Secure dropped a telegram.
 *  See knx_secure_diag.h.
 */

#include "knx_secure_diag.h"

#include "log_buffer.h"

KnxSecureDiag knxSecureDiag;

/*
 * Called by the secure application layer for every telegram it drops, see
 * scripts/patch_knx.py 37. Runs in the stack's loop.
 */
void (*sbipSecureRejectHook)(uint8_t reason, uint16_t src, uint16_t dst, bool group,
                             uint8_t scf, uint64_t seq, uint64_t expected) = nullptr;

/** At most one line per sender and reason in this time. */
static const uint32_t LOG_INTERVAL_MS = 60000UL;

void KnxSecureDiag::begin()
{
    sbipSecureRejectHook = &KnxSecureDiag::onReject;
}

void KnxSecureDiag::onReject(uint8_t reason, uint16_t src, uint16_t dst, bool group,
                             uint8_t scf, uint64_t seq, uint64_t expected)
{
    if (reason == 0 || reason >= REJECT_REASONS) return;
    knxSecureDiag.report((Reason)reason, src, dst, group, scf, seq, expected);
}

bool KnxSecureDiag::throttled(Reason reason, uint16_t src)
{
    uint32_t now    = millis();
    Recent*  oldest = &_recent[0];

    for (Recent& r : _recent)
    {
        if (r.at != 0 && r.src == src && r.reason == reason)
        {
            if ((uint32_t)(now - r.at) < LOG_INTERVAL_MS) return true;
            r.at = now;
            return false;
        }
        if (r.at == 0 || (uint32_t)(now - r.at) > (uint32_t)(now - oldest->at)) oldest = &r;
    }

    *oldest = Recent{src, (uint8_t)reason, now != 0 ? now : 1};
    return false;
}

void KnxSecureDiag::report(Reason reason, uint16_t src, uint16_t dst, bool group,
                           uint8_t scf, uint64_t seq, uint64_t expected)
{
    bool tool = (scf & 0x80) != 0;

    _count[reason]++;
    if (reason == REJECT_MAC && tool) _toolMacFail++;

    if (throttled(reason, src)) return;

    static const char* const SERVICES[] = {"data", "?", "sync.req", "sync.res"};
    uint8_t     service = scf & 0x07;
    const char* name    = service < 4 ? SERVICES[service] : "?";

    char from[12], to[12];
    snprintf(from, sizeof(from), "%u.%u.%u", src >> 12, (src >> 8) & 0x0F, src & 0xFF);
    if (group)
        snprintf(to, sizeof(to), "%u/%u/%u", dst >> 11, (dst >> 8) & 0x07, dst & 0xFF);
    else
        snprintf(to, sizeof(to), "%u.%u.%u", dst >> 12, (dst >> 8) & 0x0F, dst & 0xFF);

    const char* key = tool ? "the tool key" : group ? "the group key" : "the key of the sender";

    char why[96];
    switch (reason)
    {
    case REJECT_NO_KEY:
        snprintf(why, sizeof(why), "dropped: no key for it");
        break;
    case REJECT_REPLAY:
        snprintf(why, sizeof(why), "dropped: sequence number %llu, expected at least %llu",
                 (unsigned long long)seq, (unsigned long long)expected);
        break;
    case REJECT_SERIAL:
        snprintf(why, sizeof(why), "dropped: the serial number is not ours");
        break;
    case REJECT_SYNC_TOO_SOON:
        snprintf(why, sizeof(why), "ignored: less than 1 s after the last sync.res");
        break;
    case REJECT_NO_CHALLENGE:
        snprintf(why, sizeof(why), "dropped: no sync.req of ours pending for it");
        break;
    case REJECT_MAC:
        snprintf(why, sizeof(why), "dropped: the MAC does not match %s", key);
        break;
    default:
        why[0] = '\0';
        break;
    }

    sysLog.printf("SECURE: %s %s -> %s%s %s\n", name, from, to, tool ? " (tool access)" : "", why);
}

String KnxSecureDiag::json() const
{
    String j = "{";
    j += "\"no_key\":" + String(_count[REJECT_NO_KEY]);
    j += ",\"replays\":" + String(_count[REJECT_REPLAY]);
    j += ",\"serial\":" + String(_count[REJECT_SERIAL]);
    j += ",\"sync_too_soon\":" + String(_count[REJECT_SYNC_TOO_SOON]);
    j += ",\"no_challenge\":" + String(_count[REJECT_NO_CHALLENGE]);
    j += ",\"mac_fail\":" + String(_count[REJECT_MAC]);
    j += ",\"tool_mac_fail\":" + String(_toolMacFail);
    j += "}";
    return j;
}
