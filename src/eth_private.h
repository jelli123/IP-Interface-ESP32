/*
 *  eth_private.h - The two things ETHClass does not offer, see eth_private.cpp.
 */
#pragma once

/** @return true while ETH holds an installed driver (after begin(), until end()) */
bool ethDriverInstalled();

/** Free the MAC and PHY that ETH.begin() left behind after a failed install. */
void ethReleaseFailedDriver();
