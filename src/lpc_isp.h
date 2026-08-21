/*
 *  lpc_isp.h - Programming the SB-Interface's LPC1115 over the KNX UART.
 *
 *  Every LPC11xx carries a ROM bootloader that speaks a line based ASCII
 *  protocol (UM10398, chapter 26) on UART0. Pulling PIO0_1 low while /RESET
 *  is released selects it instead of the user program, so two GPIOs next to
 *  the KNX UART are enough to identify, erase and program the chip without
 *  a debug probe.
 *
 *  The KNX stack owns that UART, so every job pauses it first - see
 *  KnxLink::suspend(). Nothing here may run on the main task: the jobs block
 *  for seconds at a time and are driven from their own FreeRTOS task.
 */
#pragma once

#include <Arduino.h>
#include <stdint.h>

class HardwareSerial;

class LpcIsp
{
public:
    /** Biggest LPC11xx flash, and therefore the size of the staging buffer. */
    static const uint32_t MAX_IMAGE = 64u * 1024u;

    /** What the last probe found. Only written by the job task. */
    struct Info
    {
        bool     answered   = false; //!< the ROM bootloader synchronised
        uint32_t partId     = 0;
        char     part[24]   = {0};   //!< empty when the ID is not in our table
        uint32_t flashSize  = 0;
        uint32_t ramSize    = 0;
        uint8_t  bootMajor  = 0;
        uint8_t  bootMinor  = 0;
        uint32_t uid[4]     = {0, 0, 0, 0};
        bool     blank      = false; //!< sector 0 has never been written
        bool     imageValid = false; //!< the vector checksum adds up
        uint32_t stackPtr   = 0;     //!< first word of the vector table
        uint32_t resetVec   = 0;     //!< second word, entry point
    };

    /** Put both control lines into their idle state. Call once, at startup. */
    void begin();

    /** @return true if both control lines are configured */
    bool available() const;

    bool busy() const { return _job != NONE; }

    /** Identify the chip and report what is programmed. */
    bool startProbe(String& error);

    /** Erase and program the staged image, then verify it. */
    bool startFlash(String& error);

    /** Release both lines and reset the LPC into its user program. */
    bool startRun(String& error);

    /**
     * Fetch the staged image from the manifest in the hardware profile.
     *
     * Not an update check: the TP-UART emulator has no version command, so
     * the device cannot tell what is already in the LPC. This only puts the
     * offered file where an upload would have put it - writing it stays a
     * second, deliberate click.
     */
    bool startFetch(String& error);

    /*
     * Staging of the image to be written.
     *
     * Fed straight from the upload handler, which runs on the web server
     * task. Intel Hex and raw binary are both accepted; the format is decided
     * by the first byte.
     */
    bool uploadBegin(String& error);
    bool uploadData(const uint8_t* data, size_t len);
    bool uploadEnd(String& error);
    void uploadDiscard();

    /** Current state as a JSON object, for /api/lpc. */
    String statusJson() const;

private:
    enum Job : uint8_t
    {
        NONE = 0,
        PROBE,
        FLASH,
        RUN,
        FETCH
    };

    static void jobTask(void* arg);
    static void fetchTask(void* arg);
    bool start(Job job, String& error);
    void run();
    void fetch();

    /* --- control lines --------------------------------------------------- */
    void pinIdle(int8_t pin);
    void pinAssert(int8_t pin);
    void enterIsp();
    void runUser();

    /* --- ISP protocol ---------------------------------------------------- */
    int  readByte(uint32_t timeoutMs);
    bool readLine(char* out, size_t max, uint32_t timeoutMs);
    void sendLine(const char* text);
    int  command(const char* cmd, uint32_t timeoutMs = 2000);
    bool synchronize(uint32_t baud);
    bool identify();
    void inspect();
    bool readMemory(uint32_t address, uint8_t* out, size_t length);
    bool sendUu(const uint8_t* data, size_t length);
    bool writeImage();

    /* --- image parsing --------------------------------------------------- */
    bool ensure(uint32_t need);
    bool storeByte(uint32_t address, uint8_t value);
    bool parseHexLine(const char* line);

    void stage(const char* code, const char* note = nullptr);
    void fail(const char* text);

    volatile Job _job = NONE;

    HardwareSerial* _uart   = nullptr;
    int8_t          _rx     = -1;
    int8_t          _tx     = -1;
    int8_t          _reset  = -1;
    int8_t          _isp    = -1;
    bool            _invert = false;

    Info _info;

    /*
     * Progress and messages are written here and read from the web server
     * task without a lock. A torn read costs one stale poll; the buffers are
     * fixed and their last byte is never written, so there is nothing to run
     * past.
     */
    char              _stage[64] = {0};
    char              _error[96] = {0};
    volatile uint32_t _progress  = 0;
    volatile uint32_t _total     = 0;
    volatile bool     _lastOk    = false;
    volatile bool     _ran       = false;

    /* --- staged image ---------------------------------------------------- */
    uint8_t* _image     = nullptr;
    uint32_t _capacity  = 0;   //!< allocated, grows with the file
    uint32_t _imageSize = 0;   //!< highest written address plus one
    bool     _imageOk   = false;
    bool     _uploading = false;
    bool     _hexFormat = false;
    bool     _hexEnded  = false;
    uint32_t _hexBase   = 0;   //!< from record type 04
    uint32_t _rawAt     = 0;

    /** Version the manifest names for the staged file, for the dashboard. */
    char     _offered[24] = {0};
    char     _line[600] = {0};
    uint16_t _lineLen   = 0;
    String   _uploadError;
};

extern LpcIsp lpcIsp;
