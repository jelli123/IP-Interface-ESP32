/*
 *  ota_service.h - Firmware update, both manual upload and online pull.
 */
#pragma once

#include <Arduino.h>
#include <stdint.h>

class OtaService
{
public:
    enum State
    {
        IDLE,
        CHECKING,
        AVAILABLE,
        INSTALLING,
        DONE,
        FAILED
    };

    /**
     * Cancel the bootloader rollback once the firmware has proven itself.
     *
     * A freshly flashed OTA image stays in ESP_OTA_IMG_PENDING_VERIFY. If the
     * device resets before it is marked valid, the bootloader falls back to
     * the previous slot. Call from loop(); it acts once, after
     * OTA_VALIDATE_AFTER_MS of uptime.
     */
    void loop();

    /** Kick off the manifest check on a background task. */
    bool startCheck();

    /** Kick off the download and install on a background task. */
    bool startInstall();

    /** Current state as a JSON object, for /api/update/status. */
    String statusJson() const;

    /** Name of the partition we booted from. */
    static const char* runningPartition();

    /** Rollback state of the running partition. */
    static const char* runningPartitionState();

private:
    static void checkTask(void* arg);
    static void installTask(void* arg);
    static void setError(const String& message);
    static const char* stateName(State state);
    static int  compareVersions(const String& a, const String& b);

    bool _validationPending = true;
};

extern OtaService otaService;
