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

    /**
     * Both application slots, as a JSON array.
     *
     * Each entry carries the label, whether it is the one executing, its
     * rollback state and which firmware it holds.
     *
     * The image's own esp_app_desc_t is useless for the last part: it is
     * filled by the prebuilt ESP-IDF inside the Arduino libraries, so its
     * version and build date describe when Espressif compiled those - the
     * same value in both slots. Instead every boot records the running
     * firmware under its slot label, and this reads those records back. A
     * slot that has never run therefore reports its contents as unknown.
     *
     * A firmware upload always lands in the slot that is not running; the
     * running one cannot be overwritten while it executes. There is nothing
     * to choose at upload time, only which slot boots next.
     *
     * Cached: reading a partition descriptor stalls the flash cache, which is
     * not something the two second status poll should do.
     */
    static String partitionsJson();

    /**
     * Boot from the other slot next time.
     *
     * Refuses when that slot holds no valid image. Takes effect on restart,
     * and the same call switches back.
     */
    static bool switchPartition();

private:
    static void checkTask(void* arg);
    static void installTask(void* arg);
    static void setError(const String& message);
    static const char* stateName(State state);
    static int  compareVersions(const String& a, const String& b);

    /** Note in NVS which firmware runs from the current slot. */
    static void recordOwnSlot();

    bool _validationPending = true;
    bool _slotRecorded      = false;
};

extern OtaService otaService;
