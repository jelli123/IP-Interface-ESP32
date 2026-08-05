/*
 *  json_util.cpp - Small JSON helpers.
 */

#include "json_util.h"

String jsonEscape(const String& in)
{
    String out;
    out.reserve(in.length() + 8);

    for (size_t i = 0; i < in.length(); i++)
    {
        char c = in[i];
        switch (c)
        {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if ((uint8_t)c < 0x20)
            {
                char buf[7];
                snprintf(buf, sizeof(buf), "\\u%04x", (uint8_t)c);
                out += buf;
            }
            else
            {
                out += c;
            }
        }
    }
    return out;
}

/*
 * Locate "key" and return the index just past the following colon.
 *
 * Checking for the colon is what makes this usable for a configuration
 * document: a bare indexOf("\"eth_cs\"") would also hit "eth_cs_foo", and the
 * hardware profile has several keys that are prefixes of others
 * ("knx_rx" / "knx_rx_pin", "i2c_sda" / ...). Returns -1 when not found.
 */
static int findValueStart(const String& body, const String& key, int from = 0)
{
    String pattern = "\"" + key + "\"";

    for (int at = body.indexOf(pattern, from); at >= 0;
         at = body.indexOf(pattern, at + 1))
    {
        int i = at + (int)pattern.length();
        while (i < (int)body.length() && isspace((int)body[i]))
        {
            i++;
        }
        if (i < (int)body.length() && body[i] == ':')
        {
            return i + 1;
        }
    }
    return -1;
}

String jsonGetString(const String& body, const String& key)
{
    int start = findValueStart(body, key);
    if (start < 0) return "";

    int open = body.indexOf('"', start);
    if (open < 0) return "";

    int close = body.indexOf('"', open + 1);
    if (close < 0) return "";

    return body.substring(open + 1, close);
}

int32_t jsonGetInt(const String& body, const String& key, int32_t fallback)
{
    int i = findValueStart(body, key);
    if (i < 0) return fallback;

    while (i < (int)body.length() && isspace((int)body[i]))
    {
        i++;
    }

    int start = i;
    if (i < (int)body.length() && (body[i] == '-' || body[i] == '+'))
    {
        i++;
    }

    int digits = 0;
    while (i < (int)body.length() && isdigit((int)body[i]))
    {
        i++;
        digits++;
    }

    if (digits == 0) return fallback;

    return (int32_t)strtol(body.substring(start, i).c_str(), nullptr, 10);
}

bool jsonGetBool(const String& body, const String& key, bool fallback)
{
    int i = findValueStart(body, key);
    if (i < 0) return fallback;

    while (i < (int)body.length() && isspace((int)body[i]))
    {
        i++;
    }

    if (body.startsWith("true", i))  return true;
    if (body.startsWith("false", i)) return false;
    if (i < (int)body.length() && body[i] == '1') return true;
    if (i < (int)body.length() && body[i] == '0') return false;

    return fallback;
}

String jsonGetNestedString(const String& body, const String& outer,
                           const String& inner, const String& field)
{
    int outerAt = body.indexOf("\"" + outer + "\"");
    if (outerAt < 0) return "";

    int innerAt = body.indexOf("\"" + inner + "\"", outerAt);
    if (innerAt < 0) return "";

    int open = body.indexOf('{', innerAt);
    if (open < 0) return "";

    int close = body.indexOf('}', open);
    if (close < 0) return "";

    return jsonGetString(body.substring(open, close + 1), field);
}
