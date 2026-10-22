/*
 *  i2c_bus.h - One start of the I2C bus, for whoever needs it first.
 *
 *  The RTC (time_service.cpp) and the FRAM of the sequence backup
 *  (knx_seq_backup.cpp) share the bus, and the KNX stack starts before the
 *  time service. Both ask here; the bus is started once, with the pins of the
 *  hardware profile.
 */
#pragma once

/** True if the profile enables I2C and the bus runs. */
bool i2cBusBegin();
