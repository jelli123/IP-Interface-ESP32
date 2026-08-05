/*
 *  interface_config.h - Compile time configuration of the Selfbus KNXnet/IP
 *                       interface.
 *
 *  Everything in the "hardware" sections below is only a DEFAULT. The values
 *  the firmware actually runs on live in NVS and can be replaced through the
 *  dashboard, so one image supports several boards - see src/hw_config.cpp.
 *  These defines seed that profile on first boot and act as the fallback when
 *  a stored profile is missing, invalid or failed to boot.
 *
 *  All build-time symbols use the SBIP_ prefix, so a compiler flag reads
 *  -DSBIP_KNX_RX_PIN=16 rather than a -D plus GW_ combination that looks
 *  like a "DGW" token at first glance.
 */
#pragma once

#include <stdint.h>

/* ------------------------------------------------------------------------- *
 * Firmware identity
 * ------------------------------------------------------------------------- */

#define FIRMWARE_VERSION      "0.1.0"
#define DEVICE_NAME           "Selfbus KNX/IP"
/** mDNS host name, reachable as http://<MDNS_HOSTNAME>.local */
#define MDNS_HOSTNAME         "sbip"
/** Prefix of the provisioning access point; the MAC suffix is appended. */
#define AP_NAME_PREFIX        "SB-IP AP "

/* ------------------------------------------------------------------------- *
 * KNX TP1 interface (Selfbus SB-Interface running the TP-UART 2 emulator)
 * ------------------------------------------------------------------------- */

#ifndef SBIP_KNX_UART_NUM
#define SBIP_KNX_UART_NUM       2
#endif
#ifndef SBIP_KNX_RX_PIN
#define SBIP_KNX_RX_PIN         16
#endif
#ifndef SBIP_KNX_TX_PIN
#define SBIP_KNX_TX_PIN         17
#endif

/**
 * Host interface baud rate.
 *
 * A TP-UART 2 runs at 19200 baud, 8E1 and never switches. Only the NCN512x
 * family supports 38400, and only after a U_Configure handshake the emulator
 * does not implement.
 */
#define SBIP_KNX_BAUDRATE       19200

/* ------------------------------------------------------------------------- *
 * Programming LED and button
 * ------------------------------------------------------------------------- */

#ifndef SBIP_LED_PIN
#define SBIP_LED_PIN            2
#endif
#ifndef SBIP_LED_ACTIVE_LOW
#define SBIP_LED_ACTIVE_LOW     0
#endif
#ifndef SBIP_BUTTON_PIN
#define SBIP_BUTTON_PIN         0
#endif

/** Hold the button this long to force the provisioning access point. */
#define BUTTON_AP_HOLD_MS     2000

/* ------------------------------------------------------------------------- *
 * Networking
 * ------------------------------------------------------------------------- */

/** How long after boot Improv provisioning stays available, in milliseconds. */
#define IMPROV_WINDOW_MS      120000UL
/** Let the core auto-reconnect try this long before forcing a reconnect. */
#define WIFI_WATCHDOG_GRACE_MS 30000UL
/** Cadence of the forced reconnect attempts once the grace window passed. */
#define WIFI_WATCHDOG_RETRY_MS 30000UL
/** Interval of the link check in loop(), in milliseconds. */
#define WIFI_CHECK_INTERVAL_MS 5000UL

/* ------------------------------------------------------------------------- *
 * Optional W5500 Ethernet (SPI)
 * ------------------------------------------------------------------------- */

/**
 * Chip select pin. -1 means "no Ethernet by default".
 *
 * With a pin configured the firmware reads the W5500 version register on
 * every boot and only starts the driver when a chip actually answers, so the
 * same image runs on boards with and without Ethernet fitted. The pins can
 * also be set at runtime through the dashboard.
 *
 * Ethernet takes precedence: when it comes up, WiFi is never started. See
 * README for why the choice is made once at boot and not switched at runtime.
 */
#ifndef SBIP_ETH_CS_PIN
#define SBIP_ETH_CS_PIN       (-1)
#endif
#ifndef SBIP_ETH_SCK_PIN
#define SBIP_ETH_SCK_PIN      (-1)
#endif
#ifndef SBIP_ETH_MISO_PIN
#define SBIP_ETH_MISO_PIN     (-1)
#endif
#ifndef SBIP_ETH_MOSI_PIN
#define SBIP_ETH_MOSI_PIN     (-1)
#endif
/** Interrupt pin. -1 puts the driver into polling mode, which is supported. */
#ifndef SBIP_ETH_IRQ_PIN
#define SBIP_ETH_IRQ_PIN      (-1)
#endif
/** Hardware reset pin. -1 relies on the chip's power-on reset. */
#ifndef SBIP_ETH_RST_PIN
#define SBIP_ETH_RST_PIN      (-1)
#endif
/** PHY address on the SPI bus. Always 1 for the W5500. */
#ifndef SBIP_ETH_PHY_ADDR
#define SBIP_ETH_PHY_ADDR     1
#endif
/**
 * SPI clock in MHz for normal operation.
 *
 * The W5500 is specified up to 80 MHz, but 20 MHz is what the ESP-IDF driver
 * defaults to and is comfortably within reach of ribbon-cable wiring.
 */
#ifndef SBIP_ETH_SPI_MHZ
#define SBIP_ETH_SPI_MHZ      20
#endif

/**
 * Compile the Ethernet driver in at all.
 *
 * Has to stay on for the runtime configuration to be able to enable
 * Ethernet on a board whose default profile has none. Set to 0 only when
 * flash is tight and Ethernet is certain never to be used - it saves roughly
 * 40 KB but also removes the option from the dashboard.
 */
#ifndef SBIP_ETH_COMPILED
#define SBIP_ETH_COMPILED     1
#endif

/* ------------------------------------------------------------------------- *
 * Optional RV-3028-C7 real time clock (I2C)
 * ------------------------------------------------------------------------- */

/**
 * Default state of the I2C bus. 0 means "no RTC fitted by default".
 *
 * With I2C enabled but no RTC present the time server still works, it just
 * has no holdover across a power cut. Can be changed at runtime.
 */
#ifndef SBIP_I2C_ENABLED
#define SBIP_I2C_ENABLED        1
#endif
#ifndef SBIP_I2C_SDA_PIN
#define SBIP_I2C_SDA_PIN        21
#endif
#ifndef SBIP_I2C_SCL_PIN
#define SBIP_I2C_SCL_PIN        22
#endif

/* ------------------------------------------------------------------------- *
 * Firmware update
 * ------------------------------------------------------------------------- */

/**
 * Manifest for the online update. Empty disables the online update; the
 * manual firmware upload via the dashboard stays available either way.
 *
 * Expected layout, sha256 is mandatory:
 *   { "version": "1.2.3",
 *     "ota": { "ESP32": { "path": "firmware_esp32.bin",
 *                        "sha256": "<64 hex>" } } }
 */
#define UPDATE_MANIFEST_URL   ""

/**
 * Uptime after which a freshly flashed OTA image is marked valid, cancelling
 * the bootloader rollback. Must be long enough to cover a crash loop.
 */
#define OTA_VALIDATE_AFTER_MS 30000UL
