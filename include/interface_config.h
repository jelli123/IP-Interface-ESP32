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

#define FIRMWARE_VERSION      "0.2.0"

/* ------------------------------------------------------------------------- *
 * KNX device identity
 *
 * ETS matches a device against its product database by these values, so they
 * decide which knxprod can commission this device.
 *
 * Everything here is only the DEFAULT: the Selfbus identity, and the one the
 * product database in knxprod/ declares. What the firmware actually runs on
 * lives in NVS and can be replaced through the dashboard - by hand or by
 * loading a knxprod, which the browser reads for exactly these fields, see
 * src/knx_identity.cpp.
 *
 * That is deliberately the only way to present another vendor's identity.
 * The firmware carries none: a device that emulates a commercial product
 * does so because its owner fed it that product's knxprod, on their own
 * decision and with their own copy of the file. Nothing here claims to be
 * anyone else.
 *
 * Each value can still be overridden at build time with -DSBIP_KNX_..., for
 * a series that is to ship with an identity of its own.
 * ------------------------------------------------------------------------- */

/** Name for the dashboard and the log. Not sent anywhere. */
#ifndef SBIP_KNX_PRODUCT_NAME
#define SBIP_KNX_PRODUCT_NAME    "Selfbus KNX/IP"
#endif

/**
 * Manufacturer 0x00FA is the id of the KNX Association itself, which is why
 * ETS lists such a device under "KNX Association". It is the thelsing/knx
 * default and what open projects such as OpenKNX build on - there is no
 * neutral "unknown vendor" id to use instead. The name ETS shows comes from
 * its own master data, not from our knxprod, so only another id changes it.
 */
#ifndef SBIP_KNX_MANUFACTURER_ID
#define SBIP_KNX_MANUFACTURER_ID 0x00FA
#endif

/*
 * Application 1 version 1, which is what knxprod/ declares. Zero would be a
 * legal value for the device but no product data can carry it: an ETS
 * application id is built from this number, so the file would have no name
 * to be found under.
 */
#ifndef SBIP_KNX_APP_NUMBER
#define SBIP_KNX_APP_NUMBER      0x0001
#endif
#ifndef SBIP_KNX_APP_VERSION
#define SBIP_KNX_APP_VERSION     0x01
#endif

/** PID_VERSION, the VersionNumber of the hardware in the product data. */
#ifndef SBIP_KNX_DEVICE_VERSION
#define SBIP_KNX_DEVICE_VERSION  0x0001
#endif

/**
 * Tunnel addresses the product data manages.
 *
 * Has to be at most KNX_TUNNELING, which is what the stack can really serve
 * - the addresses live in arrays of that size. See platformio.ini.
 */
#ifndef SBIP_KNX_TUNNELS
#define SBIP_KNX_TUNNELS         10
#endif

/**
 * Hardware type, six octets.
 *
 * ETS compares this against the product data before a download. knxprod/
 * declares nothing here, so zero is what matches it - and zero is also what
 * an unconfigured stack reports. If ETS ever rejects an application although
 * everything else lines up, this is the first value to look at.
 */
#ifndef SBIP_KNX_HARDWARE_TYPE
#define SBIP_KNX_HARDWARE_TYPE 0, 0, 0, 0, 0, 0
#endif

/*
 * Order number, at most ten characters of PID_ORDER_INFO.
 *
 * Informational: ETS shows it in the device info, no download depends on it.
 * Empty leaves the property at zero, which is what an unconfigured stack
 * reports and therefore the safe answer wherever the real number does not
 * fit. The knxprod import fills it.
 */
#ifndef SBIP_KNX_ORDER_INFO
#define SBIP_KNX_ORDER_INFO      "SBIP-1"
#endif

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
 * Addressable status LED (WS2812 / SK6812)
 *
 * Only the default row of the profile is described here. The chain length
 * follows from how many LED rows share the pin, so there is no count to set.
 * -1 means the board has none.
 * ------------------------------------------------------------------------- */

#ifndef SBIP_RGB_PIN
#define SBIP_RGB_PIN           -1
#endif
/** 0 = WS2812, 1 = SK6812 (SK68xx). Selects the bit timing. */
#ifndef SBIP_RGB_TYPE
#define SBIP_RGB_TYPE           0
#endif

/* ------------------------------------------------------------------------- *
 * In-system programming of the SB-Interface (LPC1115)
 *
 * Two control lines next to the KNX UART let the ESP32 put the LPC into its
 * ROM bootloader and program it. -1 on either disables the feature; the KNX
 * UART is then never taken away from the stack.
 *
 * Both lines are driven open drain by default: asserted means the ESP32 pulls
 * them low, idle means it lets go and the pull-ups on the LPC take over. That
 * is what makes a floating pin during the ESP32's own boot harmless - the LPC
 * keeps running. Set lpc_invert in the profile for a board that puts an
 * inverter in between; those need a real push-pull level.
 * ------------------------------------------------------------------------- */

/** LPC /RESET, active low. */
#ifndef SBIP_LPC_RESET_PIN
#define SBIP_LPC_RESET_PIN     (-1)
#endif
/** LPC PIO0_1, sampled at the rising edge of /RESET. Low selects the ISP. */
#ifndef SBIP_LPC_ISP_PIN
#define SBIP_LPC_ISP_PIN       (-1)
#endif

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

/**
 * Default series resistance of the RTC trickle charger, in ohms.
 *
 * 0 means "do not charge" and is the only safe default: the firmware cannot
 * see what is fitted on the backup pin, and charging a primary cell - a
 * plain CR2032 - is dangerous. The resistors sit inside the RV-3028 and only
 * 3000, 5000, 9000 and 15000 exist; anything else falls back to off.
 *
 * Set this for a board that carries a supercap or a rechargeable cell, or
 * switch it on per device in the hardware profile.
 */
#ifndef SBIP_RTC_CHARGE_OHMS
#define SBIP_RTC_CHARGE_OHMS    0
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
 * Manifest for the SB-Interface firmware download. Empty disables it; the
 * file upload from the dashboard stays available either way.
 *
 * Same layout as above, own block. The LPC has no version command, so this is
 * a download on request rather than an update check - the device cannot know
 * what is already in the LPC:
 *   { "version": "1.0.3",
 *     "lpc": { "LPC1115": { "path": "tpuart2emu.hex",
 *                           "sha256": "<64 hex>" } } }
 */
#define LPC_MANIFEST_URL      ""

/** Which entry of the "lpc" block this firmware asks for. */
#define LPC_MANIFEST_KEY      "LPC1115"

/**
 * Uptime after which a freshly flashed OTA image is marked valid, cancelling
 * the bootloader rollback. Must be long enough to cover a crash loop.
 */
#define OTA_VALIDATE_AFTER_MS 30000UL
