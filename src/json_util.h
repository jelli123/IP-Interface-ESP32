/*
 *  json_util.h - Small JSON helpers.
 *
 *  Deliberately minimal: the firmware only produces flat, well formed JSON
 *  and only parses its own update manifest. Pulling in a full parser would
 *  cost flash for no gain.
 */
#pragma once

#include <Arduino.h>

/**
 * Escape a string for use as a JSON string value.
 *
 * Mandatory for anything attacker influenced - SSIDs in range, error strings
 * from the network. An unescaped quote or backslash corrupts the document or
 * injects keys.
 */
String jsonEscape(const String& in);

/** Read a flat "key": "value" pair. Returns an empty string if absent. */
String jsonGetString(const String& body, const String& key);

/**
 * Read a flat "key": <number> pair.
 *
 * @param fallback returned when the key is missing or not a number
 */
int32_t jsonGetInt(const String& body, const String& key, int32_t fallback);

/** Read a flat "key": true|false pair. Also accepts 0 and 1. */
bool jsonGetBool(const String& body, const String& key, bool fallback);

/**
 * Read "outer": { "inner": { "field": "value" } }.
 *
 * Scoping matters: the chip family string also appears elsewhere in the
 * manifest, and a plain search would match the wrong occurrence.
 */
String jsonGetNestedString(const String& body, const String& outer,
                           const String& inner, const String& field);
