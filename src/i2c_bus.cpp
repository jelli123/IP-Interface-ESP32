/*
 *  i2c_bus.cpp - One start of the I2C bus. See i2c_bus.h.
 */

#include "i2c_bus.h"

#include <Wire.h>

#include "hw_config.h"

bool i2cBusBegin()
{
    static bool started = false;

    const HwProfile& hw = hwConfig.active();
    if (!hw.i2cEnabled) return false;

    if (!started) started = Wire.begin(hw.i2cSdaPin, hw.i2cSclPin);
    return started;
}
