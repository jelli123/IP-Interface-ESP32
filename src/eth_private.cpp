/*
 *  eth_private.cpp - The two things ETHClass does not offer.
 *
 *  ETH.begin() returns without deleting the MAC and PHY objects when
 *  esp_eth_driver_install() fails. The W5500 MAC keeps its receive task,
 *  which polls the INT line once a second and then calls into the driver
 *  that was already freed - a panic in w5500_tsk (PC 0xfffffffd). Both
 *  pointers are private, so this file opens the class up for itself only.
 *  Everything ETH.h pulls in is included before, with "private" untouched.
 */

#include "interface_config.h"

#if SBIP_ETH_COMPILED
#include <Arduino.h>
#include <Network.h>
#include <SPI.h>
#include <esp_eth.h>

#define private public
#include <ETH.h>
#undef private

#include "eth_private.h"

bool ethDriverInstalled()
{
    return ETH._eth_handle != nullptr;
}

void ethReleaseFailedDriver()
{
    // Only for a driver that never got installed; an installed one belongs
    // to ETH.end().
    if (ETH._eth_handle != nullptr) return;

    // del() stops the receive task and frees the MAC; the INT handler was
    // already removed when its init failed.
    if (ETH._mac != nullptr)
    {
        ETH._mac->del(ETH._mac);
        ETH._mac = nullptr;
    }
    if (ETH._phy != nullptr)
    {
        ETH._phy->del(ETH._phy);
        ETH._phy = nullptr;
    }
}
#endif
