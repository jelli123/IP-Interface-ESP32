"""
Build tweaks that platformio.ini cannot express.

1. Make the Arduino core's own libraries visible to each other.

Arduino-ESP32 3.x ships its functionality as separate libraries under
framework-arduinoespressif32/libraries (WiFi, Network, SPI, FS, ...). They
include each other's headers - WiFiGeneric.h pulls in "Network.h", ETH.h pulls
in "SPI.h" - but none of them declares a `depends=` entry in its
library.properties.

PlatformIO's Library Dependency Finder therefore builds each of them as a
standalone library whose include path contains only its own src directory, and
the cross-references fail to resolve:

    WiFiGeneric.h:44:10: fatal error: Network.h: No such file or directory

This only surfaces once something drags those libraries into the LDF graph -
here the external dependencies (knx, ESPAsyncWebServer) that include <WiFi.h>.

Adding every core library's src directory to the global include path removes
the ordering problem entirely. It costs nothing at runtime: unreferenced
headers are never opened, and unused code is dropped by -ffunction-sections
plus --gc-sections at link time.

2. Silence -Wvolatile for third party code.

C++20 deprecated the read-modify-write on volatile that the KNX stack uses for
its frame counters (tpuart_data_link_layer.cpp). Patching .pio/libdeps would be
undone by the next library update. The flag goes into CXXFLAGS rather than
build_flags because gcc rejects it for C sources, and build_src_flags in
platformio.ini re-enables the warning for our own code - there a ++ on a
volatile would be a real hint at a non-atomic access shared with an ISR.
"""

import os

Import("env")  # noqa: F821  (injected by SCons)

env.Append(CXXFLAGS=["-Wno-volatile"])  # noqa: F821

FRAMEWORK_DIR = env.PioPlatform().get_package_dir(  # noqa: F821
    "framework-arduinoespressif32"
)

if FRAMEWORK_DIR:
    libraries_dir = os.path.join(FRAMEWORK_DIR, "libraries")
    include_dirs = []

    if os.path.isdir(libraries_dir):
        for entry in sorted(os.listdir(libraries_dir)):
            src_dir = os.path.join(libraries_dir, entry, "src")
            if os.path.isdir(src_dir):
                include_dirs.append(src_dir)

    if include_dirs:
        env.Append(CPPPATH=include_dirs)  # noqa: F821
