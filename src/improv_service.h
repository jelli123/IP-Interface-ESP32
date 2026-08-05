/*
 *  improv_service.h - Improv-WiFi provisioning over the USB serial port.
 *
 *  Wrapped in its own translation unit so the rest of the firmware does not
 *  need to know whether Improv is compiled in at all (-DDISABLE_IMPROV).
 */
#pragma once

class ImprovService
{
public:
    /**
     * Set up the Improv handler.
     *
     * Must run as early as possible in setup(): ESP Web Tools resets the
     * device by opening the port and then probes for roughly two seconds.
     */
    void begin();

    /** Pump the Improv state machine, harmless when nothing is attached. */
    void loop();

    /** Spend the given time servicing Improv only, for the early boot probe. */
    void serviceFor(unsigned long durationMs);

    /** @return true if credentials arrived over Improv in this session */
    bool provisioned() const { return _provisioned; }

private:
    bool _provisioned = false;
};

extern ImprovService improvService;
