/*
 *  lpc_isp.cpp - Programming the SB-Interface's LPC1115 over the KNX UART.
 *
 *  Protocol per NXP UM10398 chapter 26. Commands are single letters with
 *  decimal arguments, one line each, and the answer is a decimal return code.
 *  Payload travels UU-encoded in blocks of at most 20 lines, each block
 *  closed by a checksum line the other side acknowledges with OK or RESEND.
 *
 *  Flash is never written directly: data goes into the chip's RAM first (W),
 *  then a sector is unlocked (U), prepared (P) and copied over (C). Since the
 *  RAM copy is still there afterwards, the compare command (M) verifies the
 *  result without transferring anything twice.
 */

#include "lpc_isp.h"

#include <HardwareSerial.h>
#include <esp_heap_caps.h>

#include "hw_config.h"
#include "json_util.h"
#include "knx_link.h"
#include "log_buffer.h"

LpcIsp lpcIsp;

namespace
{

const uint32_t CRYSTAL_KHZ  = 12000;   //!< what the sync handshake announces
const uint32_t UNLOCK_CODE  = 23130;   //!< fixed by the bootloader
const uint32_t SECTOR_SIZE  = 4096;    //!< uniform across the LPC11xx family
const uint32_t RAM_BUFFER   = 0x10000300; //!< above what the bootloader uses
const uint32_t CHUNK        = 1024;    //!< fits the 4 KB RAM of the smallest part
const uint32_t RAM_BASE     = 0x10000000;
const uint32_t GROW         = 4096;    //!< allocation granularity of the staging buffer

/** Internal RAM the rest of the firmware must keep when there is no PSRAM. */
const uint32_t HEADROOM     = 48u * 1024u;

/*
 * Where the job runs. Core 0's idle task is watched by the task watchdog
 * (CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0), and a job that waits on the
 * UART for seconds has no business competing with it.
 */
#if CONFIG_FREERTOS_UNICORE
const BaseType_t JOB_CORE = 0;
#else
const BaseType_t JOB_CORE = 1;
#endif

/*
 * Part identification numbers of the LPC11xx family, from UM10398 table 349.
 *
 * Worth carrying in full: the answer to "which chip is on the UART" is a lot
 * more useful as a type name than as a 32 bit number, and the flash size that
 * comes with it is what bounds the image.
 */
struct PartId
{
    uint32_t    id;
    const char* name;
    uint16_t    flashKb;
    uint8_t     ramKb;
};

const PartId PARTS[] = {
    {0x0A07102B, "LPC1110/002",   4,  1}, {0x1A07102B, "LPC1110/002",   4,  1},
    {0x0A16D02B, "LPC1111/002",   8,  2}, {0x1A16D02B, "LPC1111/002",   8,  2},
    {0x041E502B, "LPC1111/101",   8,  2}, {0x2516D02B, "LPC1111/102",   8,  2},
    {0x00010013, "LPC1111/103",   8,  2}, {0x0416502B, "LPC1111/201",   8,  4},
    {0x2516902B, "LPC1111/202",   8,  4}, {0x00010012, "LPC1111/203",   8,  4},
    {0x042D502B, "LPC1112/101",  16,  2}, {0x2524D02B, "LPC1112/102",  16,  2},
    {0x0A24902B, "LPC1112/102",  16,  4}, {0x1A24902B, "LPC1112/102",  16,  4},
    {0x00020023, "LPC1112/103",  16,  2}, {0x0425502B, "LPC1112/201",  16,  4},
    {0x2524902B, "LPC1112/202",  16,  4}, {0x00020022, "LPC1112/203",  16,  4},
    {0x0434502B, "LPC1113/201",  24,  4}, {0x2532902B, "LPC1113/202",  24,  4},
    {0x00030032, "LPC1113/203",  24,  4}, {0x0434102B, "LPC1113/301",  24,  8},
    {0x2532102B, "LPC1113/302",  24,  8}, {0x00030030, "LPC1113/303",  24,  8},
    {0x0A40902B, "LPC1114/102",  32,  4}, {0x1A40902B, "LPC1114/102",  32,  4},
    {0x0444502B, "LPC1114/201",  32,  4}, {0x2540902B, "LPC1114/202",  32,  4},
    {0x00040042, "LPC1114/203",  32,  8}, {0x0444102B, "LPC1114/301",  32,  8},
    {0x2540102B, "LPC1114/302",  32,  8}, {0x00040040, "LPC1114/303",  32,  8},
    {0x00040060, "LPC1114/323",  32,  8}, {0x00040070, "LPC1114/333",  32,  8},
    {0x00050080, "LPC1115/303",  64,  8},
    {0x2500102B, "LPC1102",      32,  8},
    {0x1421102B, "LPC11C12/301", 16,  8}, {0x1440102B, "LPC11C14/301", 32,  8},
    {0x1431102B, "LPC11C22/301", 16,  8}, {0x1430102B, "LPC11C24/301", 32,  8},
};

/** UU-encode one 6 bit value. Zero is a backtick, not a space. */
inline char uuChar(uint8_t value)
{
    return value ? (char)(value + 32) : '`';
}

inline uint8_t uuValue(char c)
{
    return (c == '`') ? 0 : (uint8_t)((c - 32) & 0x3F);
}

inline uint32_t word32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int hexDigit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

} // namespace

/* ------------------------------------------------------------------------- *
 * Control lines
 * ------------------------------------------------------------------------- */

/*
 * Idle means "let go".
 *
 * Both lines have pull-ups on the LPC - /RESET and PIO0_1 alike - so an input
 * pin leaves the chip running its user code. That is what makes the whole
 * arrangement safe while the ESP32 itself boots, when its GPIOs are floating
 * inputs anyway. Only a board with an inverter in the path needs a driven
 * level, and then both directions have to be driven.
 */
void LpcIsp::pinIdle(int8_t pin)
{
    if (pin < 0) return;

    if (_invert)
    {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
    }
    else
    {
        pinMode(pin, INPUT);
    }
}

void LpcIsp::pinAssert(int8_t pin)
{
    if (pin < 0) return;

    pinMode(pin, OUTPUT);
    digitalWrite(pin, _invert ? HIGH : LOW);
}

/*
 * PIO0_1 is sampled at the rising edge of /RESET and is released right
 * afterwards: the bootloader may reconfigure that pin as an output, and two
 * drivers on one line is how transistors die.
 */
void LpcIsp::enterIsp()
{
    pinAssert(_isp);
    delay(10);
    pinAssert(_reset);
    delay(50);
    pinIdle(_reset);
    delay(20);
    pinIdle(_isp);
    delay(300); // the bootloader brings up its UART
}

void LpcIsp::runUser()
{
    pinIdle(_isp);
    delay(10);
    pinAssert(_reset);
    delay(50);
    pinIdle(_reset);
    delay(200);
}

/* ------------------------------------------------------------------------- *
 * Serial primitives
 * ------------------------------------------------------------------------- */

int LpcIsp::readByte(uint32_t timeoutMs)
{
    uint32_t deadline = millis() + timeoutMs;

    for (;;)
    {
        int c = _uart->read();
        if (c >= 0) return c;
        if ((int32_t)(millis() - deadline) >= 0) return -1;

        // Yields, which is what keeps the idle task fed while a job runs.
        vTaskDelay(1);
    }
}

bool LpcIsp::readLine(char* out, size_t max, uint32_t timeoutMs)
{
    size_t n = 0;
    out[0]   = '\0';

    for (;;)
    {
        int c = readByte(timeoutMs);
        if (c < 0)
        {
            out[n] = '\0';
            return false;
        }
        if (c == '\n')
        {
            while (n > 0 && out[n - 1] == '\r') n--;
            out[n] = '\0';
            return true;
        }
        if (n + 1 < max) out[n++] = (char)c;
    }
}

void LpcIsp::sendLine(const char* text)
{
    _uart->write((const uint8_t*)text, strlen(text));
    _uart->write((const uint8_t*)"\r\n", 2);
    _uart->flush();
}

/**
 * Send one command and collect its return code.
 *
 * @return the code, or -1 when nothing intelligible came back
 */
int LpcIsp::command(const char* cmd, uint32_t timeoutMs)
{
    while (_uart->read() >= 0) {} // whatever the previous step left behind

    sendLine(cmd);

    char line[40];
    if (!readLine(line, sizeof(line), timeoutMs)) return -1;

    // The echo is switched off during the handshake, but a bootloader that
    // missed that command would put the command line here first.
    if (line[0] < '0' || line[0] > '9')
    {
        if (!readLine(line, sizeof(line), timeoutMs)) return -1;
    }
    if (line[0] < '0' || line[0] > '9') return -1;

    return atoi(line);
}

/* ------------------------------------------------------------------------- *
 * Handshake
 * ------------------------------------------------------------------------- */

/*
 * Autobaud, then two acknowledged lines.
 *
 * The bootloader measures the bit time of a single '?' to find the baud rate,
 * so exactly one is sent per attempt - a second one arriving while it is
 * still measuring is what makes this fail intermittently.
 */
bool LpcIsp::synchronize(uint32_t baud)
{
    char line[48];
    char freq[16];

    _uart->end();
    _uart->begin(baud, SERIAL_8N1, _rx, _tx);
    _uart->setTimeout(0);

    enterIsp();
    while (_uart->read() >= 0) {}

    _uart->write('?');
    _uart->flush();

    if (!readLine(line, sizeof(line), 1500)) return false;
    if (strstr(line, "Synchronized") == nullptr) return false;

    sendLine("Synchronized");
    if (!readLine(line, sizeof(line), 1000)) return false;
    if (strcmp(line, "Synchronized") == 0 &&
        !readLine(line, sizeof(line), 1000)) return false;
    if (strcmp(line, "OK") != 0) return false;

    snprintf(freq, sizeof(freq), "%u", (unsigned)CRYSTAL_KHZ);
    sendLine(freq);
    if (!readLine(line, sizeof(line), 1000)) return false;
    if (strcmp(line, freq) == 0 && !readLine(line, sizeof(line), 1000)) return false;
    if (strcmp(line, "OK") != 0) return false;

    // Echo off - from here on every answer is exactly one line.
    sendLine("A 0");
    if (!readLine(line, sizeof(line), 1000)) return false;
    if (strcmp(line, "A 0") == 0 && !readLine(line, sizeof(line), 1000)) return false;

    return atoi(line) == 0;
}

/* ------------------------------------------------------------------------- *
 * Reading the chip
 * ------------------------------------------------------------------------- */

bool LpcIsp::identify()
{
    char line[40];

    if (command("J") != 0) return false;
    if (!readLine(line, sizeof(line), 2000)) return false;

    _info.partId    = strtoul(line, nullptr, 10);
    _info.flashSize = 0;
    _info.part[0]   = '\0';

    for (size_t i = 0; i < sizeof(PARTS) / sizeof(PARTS[0]); i++)
    {
        if (PARTS[i].id == _info.partId)
        {
            strlcpy(_info.part, PARTS[i].name, sizeof(_info.part));
            _info.flashSize = (uint32_t)PARTS[i].flashKb * 1024u;
            _info.ramSize   = (uint32_t)PARTS[i].ramKb * 1024u;
            break;
        }
    }

    // An unknown ID is not a reason to stop - the family shares the protocol,
    // and the smallest sensible assumption still lets a small image through.
    if (_info.flashSize == 0)
    {
        _info.flashSize = 32u * 1024u;
        _info.ramSize   = 8u * 1024u;
    }

    _info.answered = true;

    if (command("K") == 0 &&
        readLine(line, sizeof(line), 1000))
    {
        _info.bootMajor = (uint8_t)strtoul(line, nullptr, 10);
        if (readLine(line, sizeof(line), 1000))
        {
            _info.bootMinor = (uint8_t)strtoul(line, nullptr, 10);
        }
    }

    if (command("N") == 0)
    {
        for (int i = 0; i < 4; i++)
        {
            if (!readLine(line, sizeof(line), 1000)) break;
            _info.uid[i] = strtoul(line, nullptr, 10);
        }
    }

    return true;
}

/*
 * What is programmed, as far as the bootloader can tell.
 *
 * "Blank" comes from the blank check on sector 0; "valid" applies the same
 * rule the bootloader itself uses to decide whether to start user code
 * (UM10398 26.3.3): the first eight vector table words have to add up to
 * zero, and the first of them is the initial stack pointer, which has to
 * point into RAM.
 */
void LpcIsp::inspect()
{
    char line[40];
    int  rc = command("I 0 0", 3000);

    _info.blank = (rc == 0);
    if (rc == 8)
    {
        // Offset and contents of the first non-blank word follow.
        readLine(line, sizeof(line), 500);
        readLine(line, sizeof(line), 500);
    }

    _info.imageValid = false;
    _info.stackPtr   = 0;
    _info.resetVec   = 0;

    uint8_t vectors[32];
    if (!readMemory(0, vectors, sizeof(vectors))) return;

    uint32_t sum = 0;
    for (int i = 0; i < 8; i++) sum += word32(vectors + i * 4);

    _info.stackPtr = word32(vectors);
    _info.resetVec = word32(vectors + 4);

    bool stackInRam = (_info.stackPtr > RAM_BASE) &&
                      (_info.stackPtr <= RAM_BASE + _info.ramSize);

    _info.imageValid = (sum == 0) && stackInRam;
}

bool LpcIsp::readMemory(uint32_t address, uint8_t* out, size_t length)
{
    char cmd[48];
    snprintf(cmd, sizeof(cmd), "R %lu %lu",
             (unsigned long)address, (unsigned long)length);

    if (command(cmd) != 0) return false;

    char     line[80];
    size_t   got = 0;
    uint32_t sum = 0;

    while (got < length)
    {
        if (!readLine(line, sizeof(line), 2000)) return false;

        size_t n = uuValue(line[0]);
        if (n == 0 || n > 45 || got + n > length) return false;
        if (strlen(line) < 1 + ((n + 2) / 3) * 4) return false;

        const char* src = line + 1;
        for (size_t i = 0; i < n; i += 3)
        {
            uint8_t c0 = uuValue(src[0]), c1 = uuValue(src[1]);
            uint8_t c2 = uuValue(src[2]), c3 = uuValue(src[3]);
            src += 4;

            const uint8_t group[3] = {
                (uint8_t)((c0 << 2) | (c1 >> 4)),
                (uint8_t)(((c1 & 0x0F) << 4) | (c2 >> 2)),
                (uint8_t)(((c2 & 0x03) << 6) | c3),
            };

            for (size_t k = 0; k < 3 && i + k < n; k++)
            {
                out[got++] = group[k];
                sum += group[k];
            }
        }
    }

    // The block is closed by a checksum the bootloader waits to have
    // acknowledged before it accepts the next command.
    if (!readLine(line, sizeof(line), 2000)) return false;

    bool match = (strtoul(line, nullptr, 10) == sum);
    sendLine(match ? "OK" : "RESEND");
    return match;
}

/* ------------------------------------------------------------------------- *
 * Writing
 * ------------------------------------------------------------------------- */

bool LpcIsp::sendUu(const uint8_t* data, size_t length)
{
    size_t offset = 0;

    while (offset < length)
    {
        size_t blockEnd = offset + 20 * 45;
        if (blockEnd > length) blockEnd = length;

        uint32_t sum = 0;

        for (size_t at = offset; at < blockEnd; )
        {
            size_t n = blockEnd - at;
            if (n > 45) n = 45;

            char   line[64];
            size_t o = 0;
            line[o++] = (char)(n + 32);

            for (size_t i = 0; i < n; i += 3)
            {
                uint8_t b0 = data[at + i];
                uint8_t b1 = (i + 1 < n) ? data[at + i + 1] : 0;
                uint8_t b2 = (i + 2 < n) ? data[at + i + 2] : 0;

                line[o++] = uuChar(b0 >> 2);
                line[o++] = uuChar(((b0 & 0x03) << 4) | (b1 >> 4));
                line[o++] = uuChar(((b1 & 0x0F) << 2) | (b2 >> 6));
                line[o++] = uuChar(b2 & 0x3F);
            }

            _uart->write((const uint8_t*)line, o);
            _uart->write((const uint8_t*)"\r\n", 2);

            for (size_t i = 0; i < n; i++) sum += data[at + i];
            at += n;
        }

        char checksum[16];
        snprintf(checksum, sizeof(checksum), "%lu", (unsigned long)sum);
        sendLine(checksum);

        char answer[16];
        if (!readLine(answer, sizeof(answer), 3000)) return false;
        if (strcmp(answer, "OK") != 0) return false;

        offset = blockEnd;
    }

    return true;
}

bool LpcIsp::writeImage()
{
    // The copy command only accepts 256, 512, 1024 or 4096 bytes, so the tail
    // is padded rather than special cased. At most a kilobyte of flash is
    // spent on erased padding.
    uint32_t size = ((_imageSize + CHUNK - 1) / CHUNK) * CHUNK;

    if (size > _info.flashSize)
    {
        fail("image does not fit into the flash of this chip");
        return false;
    }
    if (!ensure(size))
    {
        fail("not enough memory to pad the image");
        return false;
    }

    char    cmd[64];
    char    note[64];
    uint8_t lastSector = (uint8_t)((size - 1) / SECTOR_SIZE);

    stage("erase", "erasing the flash");
    snprintf(cmd, sizeof(cmd), "U %lu", (unsigned long)UNLOCK_CODE);
    if (command(cmd) != 0)                                  { fail("unlock refused"); return false; }
    snprintf(cmd, sizeof(cmd), "P 0 %u", lastSector);
    if (command(cmd) != 0)                                  { fail("sector prepare refused"); return false; }
    snprintf(cmd, sizeof(cmd), "E 0 %u", lastSector);
    if (command(cmd, 15000) != 0)                           { fail("erase failed"); return false; }

    stage("write", "writing the image");
    _total    = size;
    _progress = 0;

    for (uint32_t at = 0; at < size; at += CHUNK)
    {
        uint8_t first = (uint8_t)(at / SECTOR_SIZE);
        uint8_t last  = (uint8_t)((at + CHUNK - 1) / SECTOR_SIZE);

        snprintf(cmd, sizeof(cmd), "W %lu %lu",
                 (unsigned long)RAM_BUFFER, (unsigned long)CHUNK);
        if (command(cmd) != 0)                { fail("write to RAM refused"); return false; }
        if (!sendUu(_image + at, CHUNK))      { fail("data transfer failed"); return false; }

        snprintf(cmd, sizeof(cmd), "U %lu", (unsigned long)UNLOCK_CODE);
        if (command(cmd) != 0)                { fail("unlock refused"); return false; }
        snprintf(cmd, sizeof(cmd), "P %u %u", first, last);
        if (command(cmd) != 0)                { fail("sector prepare refused"); return false; }

        snprintf(cmd, sizeof(cmd), "C %lu %lu %lu",
                 (unsigned long)at, (unsigned long)RAM_BUFFER, (unsigned long)CHUNK);
        if (command(cmd, 5000) != 0)
        {
            snprintf(note, sizeof(note), "programming failed at 0x%08lX", (unsigned long)at);
            fail(note);
            return false;
        }

        // The RAM copy is still in place, so verifying costs one command and
        // no second transfer.
        snprintf(cmd, sizeof(cmd), "M %lu %lu %lu",
                 (unsigned long)at, (unsigned long)RAM_BUFFER, (unsigned long)CHUNK);
        if (command(cmd, 5000) != 0)
        {
            snprintf(note, sizeof(note), "verify failed at 0x%08lX", (unsigned long)at);
            fail(note);
            return false;
        }

        _progress = at + CHUNK;
    }

    return true;
}

/* ------------------------------------------------------------------------- *
 * Jobs
 * ------------------------------------------------------------------------- */

void LpcIsp::begin()
{
    const HwProfile& hw = hwConfig.active();

    _reset  = hw.lpcResetPin;
    _isp    = hw.lpcIspPin;
    _invert = hw.lpcInvert;

    if (!available()) return;

    pinIdle(_reset);
    pinIdle(_isp);

    sysLog.printf("LPC: ISP lines on GPIO %d (reset) and %d (ISP)%s\n",
                  (int)_reset, (int)_isp, _invert ? ", inverted" : "");
}

bool LpcIsp::available() const
{
    return _reset >= 0 && _isp >= 0;
}

/*
 * The stage is a code, not a sentence: the dashboard turns it into whatever
 * language it is showing, and the log gets the longer wording.
 */
void LpcIsp::stage(const char* code, const char* note)
{
    strlcpy(_stage, code, sizeof(_stage) - 1);
    sysLog.printf("LPC: %s\n", note ? note : code);
}

void LpcIsp::fail(const char* text)
{
    strlcpy(_error, text, sizeof(_error) - 1);
    sysLog.printf("LPC: %s\n", text);
}

void LpcIsp::run()
{
    _error[0] = '\0';
    _lastOk   = false;
    _progress = 0;
    _total    = 0;

    const HwProfile& hw = hwConfig.active();
    _rx     = hw.knxRxPin;
    _tx     = hw.knxTxPin;
    _reset  = hw.lpcResetPin;
    _isp    = hw.lpcIspPin;
    _invert = hw.lpcInvert;
    _uart   = knxLink.uart();

    if (_uart == nullptr)
    {
        fail("the KNX UART was never opened");
        return;
    }

    stage("pause", "pausing the KNX stack");
    if (!knxLink.suspend())
    {
        fail("the KNX stack did not stop - is the main loop stuck?");
        return;
    }

    if (_job == RUN)
    {
        stage("reset", "resetting the SB-Interface");
        runUser();
        _lastOk = true;
    }
    else
    {
        // Whatever the last job found says nothing about this one.
        _info = Info();

        stage("detect", "looking for the ROM bootloader");

        // Three attempts, the last one slower: a long or noisy line can make
        // the autobaud measurement miss at 115200 and still work at 57600.
        bool synced = false;
        for (int attempt = 0; attempt < 3 && !synced; attempt++)
        {
            synced = synchronize(attempt < 2 ? 115200 : 57600);
        }

        if (!synced)
        {
            fail("no answer from the ROM bootloader - check the reset and ISP wiring");
        }
        else if (!identify())
        {
            fail("the chip did not report its part number");
        }
        else if (_job == FLASH)
        {
            if (!_imageOk || _imageSize == 0)
            {
                fail("no image has been uploaded");
            }
            else if (writeImage())
            {
                inspect();
                _lastOk = true;

                // Its job is done, and on a board without PSRAM it is a
                // sizeable piece of the internal heap.
                uploadDiscard();
            }
        }
        else
        {
            inspect();
            _lastOk = true;
        }

        stage("reset", "restarting the SB-Interface");
        runUser();
        _uart->end();
    }

    knxLink.resume();
    stage(_lastOk ? "done" : "failed");
    _ran = true;
}

void LpcIsp::jobTask(void* arg)
{
    LpcIsp* self = (LpcIsp*)arg;
    self->run();
    self->_job = NONE;
    vTaskDelete(nullptr);
}

bool LpcIsp::start(Job job, String& error)
{
    if (!available())
    {
        error = "no ISP pins are configured";
        return false;
    }
    if (_job != NONE)
    {
        error = "a job is already running";
        return false;
    }

    _job = job;

    // 6 KB is comfortable: the deepest frame here is one UU line plus a
    // handful of small buffers, and nothing recurses.
    if (xTaskCreatePinnedToCore(jobTask, "lpc_isp", 6144, this, 1, nullptr,
                                JOB_CORE) != pdPASS)
    {
        _job  = NONE;
        error = "no memory for the programming task";
        return false;
    }

    return true;
}

bool LpcIsp::startProbe(String& error) { return start(PROBE, error); }
bool LpcIsp::startRun(String& error)   { return start(RUN, error); }

bool LpcIsp::startFlash(String& error)
{
    if (!_imageOk || _imageSize == 0)
    {
        error = "no image has been uploaded";
        return false;
    }
    return start(FLASH, error);
}

/* ------------------------------------------------------------------------- *
 * Staging the image
 *
 * Fed from the upload handler on the web server task, one chunk at a time.
 * Intel Hex and raw binary are both accepted - the first byte decides, since
 * a hex file always starts with a colon and a binary never does (word zero of
 * the vector table is the stack pointer, which ends in 0x00 or 0x10).
 * ------------------------------------------------------------------------- */

bool LpcIsp::uploadBegin(String& error)
{
    if (_job != NONE)
    {
        error = "a job is running";
        return false;
    }

    uploadDiscard();

    _uploading   = true;
    _hexFormat   = false;
    _hexEnded    = false;
    _hexBase     = 0;
    _rawAt       = 0;
    _lineLen     = 0;
    _uploadError = "";
    return true;
}

/*
 * Grow the staging buffer to hold at least @p need bytes.
 *
 * Sized to the file rather than to the 64 KiB an LPC1115 could take: without
 * PSRAM this competes with the network stack for internal RAM, and a
 * TP-UART emulator is a fraction of that. New space reads as erased flash,
 * so anything the file leaves out is written as what is already there.
 */
bool LpcIsp::ensure(uint32_t need)
{
    if (need <= _capacity) return true;
    if (need > MAX_IMAGE) return false;

    uint32_t want = ((need + GROW - 1) / GROW) * GROW;
    if (want > MAX_IMAGE) want = MAX_IMAGE;

    uint8_t* grown = (uint8_t*)heap_caps_realloc(_image, want, MALLOC_CAP_SPIRAM);

    if (grown == nullptr)
    {
        // No PSRAM on this board. Leave the rest of the firmware room to
        // breathe instead of succeeding here and failing somewhere unrelated.
        if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < want + HEADROOM)
        {
            return false;
        }
        grown = (uint8_t*)heap_caps_realloc(_image, want,
                                            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }

    if (grown == nullptr) return false;

    memset(grown + _capacity, 0xFF, want - _capacity);
    _image    = grown;
    _capacity = want;
    return true;
}

bool LpcIsp::storeByte(uint32_t address, uint8_t value)
{
    if (address >= MAX_IMAGE)
    {
        _uploadError = "the file reaches beyond 64 KiB";
        return false;
    }
    if (!ensure(address + 1))
    {
        _uploadError = "not enough memory for a file this size";
        return false;
    }

    _image[address] = value;
    if (address + 1 > _imageSize) _imageSize = address + 1;
    return true;
}

/**
 * One Intel Hex record: ':' LL AAAA TT <data> CC, checksum included.
 *
 * Only the record types that can appear in a flat 64 KiB image are honoured;
 * a segmented or high-address file is refused rather than silently folded
 * into the wrong place.
 */
bool LpcIsp::parseHexLine(const char* line)
{
    if (line[0] != ':')
    {
        _uploadError = "not an Intel Hex record";
        return false;
    }

    size_t len = strlen(line);
    if (len < 11 || (len % 2) == 0)
    {
        _uploadError = "malformed Intel Hex record";
        return false;
    }

    uint8_t bytes[64 + 5];
    size_t  count = (len - 1) / 2;
    if (count > sizeof(bytes))
    {
        _uploadError = "Intel Hex record too long";
        return false;
    }

    uint8_t sum = 0;
    for (size_t i = 0; i < count; i++)
    {
        int hi = hexDigit(line[1 + i * 2]);
        int lo = hexDigit(line[2 + i * 2]);
        if (hi < 0 || lo < 0)
        {
            _uploadError = "Intel Hex record holds a non-hex character";
            return false;
        }
        bytes[i] = (uint8_t)((hi << 4) | lo);
        sum      = (uint8_t)(sum + bytes[i]);
    }

    if (sum != 0)
    {
        _uploadError = "Intel Hex checksum mismatch";
        return false;
    }
    if (bytes[0] + 5u != count)
    {
        _uploadError = "Intel Hex length mismatch";
        return false;
    }

    uint8_t  length = bytes[0];
    uint16_t offset = (uint16_t)((bytes[1] << 8) | bytes[2]);
    uint8_t  type   = bytes[3];

    switch (type)
    {
    case 0x00:
        for (uint8_t i = 0; i < length; i++)
        {
            if (!storeByte(_hexBase + offset + i, bytes[4 + i])) return false;
        }
        return true;

    case 0x01:
        _hexEnded = true;
        return true;

    case 0x04:
        // Extended linear address. Anything but 0 puts the data outside the
        // LPC's flash, which cannot be what was meant.
        _hexBase = ((uint32_t)bytes[4] << 24) | ((uint32_t)bytes[5] << 16);
        if (_hexBase != 0)
        {
            _uploadError = "the file addresses memory above 64 KiB";
            return false;
        }
        return true;

    case 0x03:
    case 0x05:
        return true; // start address, of no interest to the bootloader

    default:
        _uploadError = "unsupported Intel Hex record type";
        return false;
    }
}

bool LpcIsp::uploadData(const uint8_t* data, size_t len)
{
    if (!_uploading || _uploadError.length()) return false;

    if (_imageSize == 0 && _rawAt == 0 && _lineLen == 0 && len > 0 && !_hexFormat)
    {
        _hexFormat = (data[0] == ':');
    }

    if (!_hexFormat)
    {
        for (size_t i = 0; i < len; i++)
        {
            if (!storeByte(_rawAt++, data[i])) return false;
        }
        return true;
    }

    for (size_t i = 0; i < len; i++)
    {
        char c = (char)data[i];

        if (c == '\r' || c == '\n')
        {
            if (_lineLen > 0)
            {
                _line[_lineLen] = '\0';
                _lineLen        = 0;
                if (!parseHexLine(_line)) return false;
            }
            continue;
        }

        if (_lineLen + 1 >= sizeof(_line))
        {
            _uploadError = "Intel Hex line too long";
            return false;
        }
        _line[_lineLen++] = c;
    }

    return true;
}

bool LpcIsp::uploadEnd(String& error)
{
    _uploading = false;

    if (_uploadError.length())
    {
        error = _uploadError;
        _imageSize = 0;
        return false;
    }

    if (_hexFormat && _lineLen > 0)
    {
        _line[_lineLen] = '\0';
        _lineLen        = 0;
        if (!parseHexLine(_line))
        {
            error      = _uploadError;
            _imageSize = 0;
            return false;
        }
    }

    if (_hexFormat && !_hexEnded)
    {
        error      = "the Intel Hex file has no end record - truncated?";
        _imageSize = 0;
        return false;
    }

    if (_imageSize < 32)
    {
        error      = "the file is too small to hold a vector table";
        _imageSize = 0;
        return false;
    }

    /*
     * Patch the vector table checksum (UM10398 26.3.3).
     *
     * The bootloader adds up the first eight words and only starts user code
     * when the result is zero; word seven exists for nothing else. Toolchains
     * differ on whether they fill it in, so it is computed here either way -
     * an image that skips this stays in the bootloader and looks like a dead
     * board.
     */
    uint32_t sum = 0;
    for (int i = 0; i < 7; i++) sum += word32(_image + i * 4);

    uint32_t checksum = (uint32_t)(0u - sum);
    uint32_t stored   = word32(_image + 0x1C);

    if (stored != checksum)
    {
        for (int i = 0; i < 4; i++)
        {
            _image[0x1C + i] = (uint8_t)(checksum >> (8 * i));
        }
        sysLog.printf("LPC: vector checksum patched, 0x%08lX -> 0x%08lX\n",
                      (unsigned long)stored, (unsigned long)checksum);
    }

    _imageOk = true;
    sysLog.printf("LPC: %s image staged, %lu bytes\n",
                  _hexFormat ? "Intel Hex" : "binary",
                  (unsigned long)_imageSize);
    return true;
}

void LpcIsp::uploadDiscard()
{
    free(_image);
    _image     = nullptr;
    _capacity  = 0;
    _imageSize = 0;
    _imageOk   = false;
    _uploading = false;
    _lineLen   = 0;
}

/* ------------------------------------------------------------------------- *
 * Status
 * ------------------------------------------------------------------------- */

String LpcIsp::statusJson() const
{
    static const char* const JOBS[] = {"idle", "probe", "flash", "run"};

    String j = "{";
    j += "\"available\":" + String(available() ? "true" : "false") + ",";
    j += "\"busy\":" + String(busy() ? "true" : "false") + ",";
    j += "\"job\":\"" + String(JOBS[_job]) + "\",";
    j += "\"stage\":\"" + jsonEscape(String(_stage)) + "\",";
    j += "\"error\":\"" + jsonEscape(String(_error)) + "\",";
    j += "\"ran\":" + String(_ran ? "true" : "false") + ",";
    j += "\"ok\":" + String(_lastOk ? "true" : "false") + ",";
    j += "\"progress\":" + String(_progress) + ",";
    j += "\"total\":" + String(_total) + ",";
    j += "\"image_size\":" + String(_imageOk ? _imageSize : 0) + ",";
    j += "\"reset_pin\":" + String(_reset) + ",";
    j += "\"isp_pin\":" + String(_isp) + ",";

    // Whether the TP-UART emulator answers is the functional counterpart to
    // everything above: the bootloader can tell us a valid image is present,
    // only the KNX stack can tell us it is the right one.
    j += "\"tp_connected\":" + String(knxLink.tpConnected() ? "true" : "false") + ",";

    char partId[16];
    snprintf(partId, sizeof(partId), "0x%08lX", (unsigned long)_info.partId);

    j += "\"chip\":{";
    j += "\"answered\":" + String(_info.answered ? "true" : "false") + ",";
    j += "\"part_id\":\"" + String(partId) + "\",";
    j += "\"part\":\"" + jsonEscape(String(_info.part)) + "\",";
    j += "\"flash\":" + String(_info.flashSize) + ",";
    j += "\"ram\":" + String(_info.ramSize) + ",";
    j += "\"boot\":\"" + String(_info.bootMajor) + "." + String(_info.bootMinor) + "\",";

    char uid[40];
    snprintf(uid, sizeof(uid), "%08lX-%08lX-%08lX-%08lX",
             (unsigned long)_info.uid[0], (unsigned long)_info.uid[1],
             (unsigned long)_info.uid[2], (unsigned long)_info.uid[3]);
    j += "\"uid\":\"" + String(uid) + "\",";

    j += "\"blank\":" + String(_info.blank ? "true" : "false") + ",";
    j += "\"image_valid\":" + String(_info.imageValid ? "true" : "false");
    j += "}}";
    return j;
}
