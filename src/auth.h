/*
 *  auth.h - Optional password for the dashboard and the API.
 *
 *  Digest rather than Basic: the password never crosses the wire in clear.
 *  That is the whole of what it buys, and the limit is worth naming - without
 *  TLS everything else stays readable, so this keeps out the neighbour on the
 *  same network, not someone capturing traffic.
 */
#pragma once

#include <Arduino.h>

class AsyncWebServer;

namespace Auth
{
/** @return true when a password is stored and switched on */
bool enabled();

/**
 * Whether a password may be set at all.
 *
 * Requires a button assigned to the factory reset. It is the only way back
 * into a device whose password has been forgotten - without one the answer
 * would be a USB cable and erase_flash, which is not a recovery path for
 * something sitting in a distribution board.
 */
bool recoveryPossible();

/** The user name, empty when no password is set. */
String user();

/**
 * Install the check on every route.
 *
 * Skipped while the provisioning access point is up: nothing is configured
 * yet at that point, and a password prompt in front of the setup page is how
 * a device becomes unreachable.
 */
void attach(AsyncWebServer& server);

/**
 * Store or clear the credentials. Takes effect on the next restart.
 *
 * @param user     1..31 characters, empty clears the password
 * @param password at least 8 characters when a user is given
 * @param error    receives a readable reason on failure
 */
bool set(const String& user, const String& password, String& error);
} // namespace Auth
