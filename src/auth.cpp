/*
 *  auth.cpp - Optional password for the dashboard and the API.
 */

#include <ESPAsyncWebServer.h>
#include <Preferences.h>

#include "auth.h"
#include "hw_config.h"
#include "log_buffer.h"
#include "net_manager.h"

namespace
{
Preferences  authPrefs;
const char*  AUTH_NS   = "sbip-auth";
const char*  KEY_USER  = "user";
const char*  KEY_HASH  = "hash";

/** Part of the stored hash, so changing it invalidates every password. */
const char*  REALM = "Selfbus KNX/IP";

const size_t USER_MAX    = 31;
const size_t PASS_MIN    = 8;

// Lives as long as the server does; the middleware is not copied in.
AsyncAuthenticationMiddleware guard;

String storedUser;
String storedHash;

void load()
{
    authPrefs.begin(AUTH_NS, false);
    storedUser = authPrefs.isKey(KEY_USER) ? authPrefs.getString(KEY_USER, "") : String();
    storedHash = authPrefs.isKey(KEY_HASH) ? authPrefs.getString(KEY_HASH, "") : String();
    authPrefs.end();
}
} // namespace

bool Auth::enabled()
{
    if (storedUser.isEmpty()) load();
    return !storedUser.isEmpty() && !storedHash.isEmpty();
}

bool Auth::recoveryPossible()
{
    return hwConfig.active().findButtonFor(HW_BTNF_FACTORY) >= 0;
}

String Auth::user()
{
    if (storedUser.isEmpty()) load();
    return storedUser;
}

void Auth::attach(AsyncWebServer& server)
{
    load();

    if (storedUser.isEmpty() || storedHash.isEmpty())
    {
        sysLog.println("Auth: no password set, dashboard is open");
        return;
    }

    if (netManager.isApMode())
    {
        sysLog.println("Auth: skipped while the provisioning AP is up");
        return;
    }

    guard.setRealm(REALM);
    guard.setUsername(storedUser.c_str());
    guard.setPasswordHash(storedHash.c_str());
    guard.setAuthType(AsyncAuthType::AUTH_DIGEST);
    guard.setAuthFailureMessage("Authentication required");

    server.addMiddleware(&guard);
    sysLog.printf("Auth: digest authentication for user %s\n", storedUser.c_str());
}

bool Auth::set(const String& user, const String& password, String& error)
{
    if (user.isEmpty())
    {
        authPrefs.begin(AUTH_NS, false);
        authPrefs.clear();
        authPrefs.end();

        storedUser = "";
        storedHash = "";

        sysLog.println("Auth: password cleared, open after the next restart");
        return true;
    }

    if (user.length() > USER_MAX)
    {
        error = "user name too long";
        return false;
    }
    if (user.indexOf(':') >= 0)
    {
        error = "user name must not contain a colon"; // it separates the hash parts
        return false;
    }
    if (password.length() < PASS_MIN)
    {
        error = "password needs at least 8 characters";
        return false;
    }
    if (!recoveryPossible())
    {
        error = "assign a button to the factory reset first - otherwise a "
                "forgotten password can only be cleared over USB";
        return false;
    }

    // Hash rather than the password itself: it is what digest compares, so
    // the plain text never has to be stored.
    AsyncAuthenticationMiddleware maker;
    maker.setRealm(REALM);
    maker.setUsername(user.c_str());
    maker.setPassword(password.c_str());
    maker.setAuthType(AsyncAuthType::AUTH_DIGEST);

    if (!maker.generateHash())
    {
        error = "could not derive the password hash";
        return false;
    }

    authPrefs.begin(AUTH_NS, false);
    authPrefs.putString(KEY_USER, user);
    authPrefs.putString(KEY_HASH, maker.credentials());
    authPrefs.end();

    storedUser = user;
    storedHash = maker.credentials();

    sysLog.printf("Auth: password set for user %s, active after the next restart\n",
                  user.c_str());
    return true;
}
