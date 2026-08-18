/*
 *  improv_service.cpp - Improv-WiFi provisioning over the USB serial port.
 */

#include <Arduino.h>

#include "interface_config.h"
#include "improv_service.h"

#include "log_buffer.h"
ImprovService improvService;

#ifndef DISABLE_IMPROV

#include <ImprovWiFiLibrary.h>

static ImprovWiFi improv(&Serial);
static bool       s_connected = false;

static void onImprovError(ImprovTypes::Error err)
{
    sysLog.printf("Improv error: %d\n", (int)err);
}

static void onImprovConnected(const char* ssid, const char* password)
{
    (void)password;
    sysLog.printf("Improv provisioned SSID %s\n", ssid);
    s_connected = true;
}

void ImprovService::begin()
{
    improv.onImprovError(onImprovError);
    improv.onImprovConnected(onImprovConnected);

    // The chip family is purely informational - the browser shows it during
    // provisioning, nothing depends on it. Upstream ImprovTypes::ChipFamily
    // only knows ESP32, C3, S2, S3 and ESP8266; there is no C6 constant (that
    // is why ip4knx uses a patched fork). Reporting plain ESP32 for the C6
    // keeps us on the unmodified library at no functional cost.
#if defined(CONFIG_IDF_TARGET_ESP32C3)
    const ImprovTypes::ChipFamily family = ImprovTypes::ChipFamily::CF_ESP32_C3;
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
    const ImprovTypes::ChipFamily family = ImprovTypes::ChipFamily::CF_ESP32_S2;
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
    const ImprovTypes::ChipFamily family = ImprovTypes::ChipFamily::CF_ESP32_S3;
#else
    const ImprovTypes::ChipFamily family = ImprovTypes::ChipFamily::CF_ESP32;
#endif

    improv.setDeviceInfo(family, DEVICE_NAME, FIRMWARE_VERSION, DEVICE_NAME);
}

void ImprovService::loop()
{
    improv.handleSerial();
    _provisioned = s_connected;
}

void ImprovService::serviceFor(unsigned long durationMs)
{
    unsigned long deadline = millis() + durationMs;
    while ((int32_t)(millis() - deadline) < 0)
    {
        improv.handleSerial();
        delay(10);
    }
}

#else  /* DISABLE_IMPROV */

void ImprovService::begin() {}
void ImprovService::loop() {}
void ImprovService::serviceFor(unsigned long) {}

#endif /* DISABLE_IMPROV */
