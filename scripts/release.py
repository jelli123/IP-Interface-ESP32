"""Builds every environment and lays the images out for the update manifest.

Run by hand, not by PlatformIO:

    python scripts/release.py            # alle Umgebungen
    python scripts/release.py -e esp32dev -e esp32s3
    python scripts/release.py --no-build # nur einsammeln, was schon gebaut ist

Result in release/:

    firmware_<env>_<version>.bin   one per environment
    manifest.json                  ready for UPDATE_MANIFEST_URL

The manifest is keyed by chip family, mirroring UPDATE_CHIP_KEY in
src/ota_service.cpp - a device only ever looks up its own family. Two
environments of the same family (esp32dev and esp32dev_8mb) therefore cannot
both appear; the canonical one wins and the other is only written as a file.
That is on purpose: the 8 MB variants differ from their 4 MB counterparts in
the partition table, and a partition table is not something an update can
change.
"""

import argparse
import configparser
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RELEASE_DIR = os.path.join(ROOT, "release")

# Same mapping as UPDATE_CHIP_KEY in src/ota_service.cpp. Keep them in step:
# a key the firmware does not ask for makes the update look misconfigured.
CHIP_KEYS = {
    "esp32dev": "ESP32",
    "esp32c3": "ESP32-C3",
    "esp32c6": "ESP32-C6",
    "esp32s2": "ESP32-S2",
    "esp32s3": "ESP32-S3",
}


def platformio():
    """The PlatformIO executable, which is rarely on PATH."""
    for name in ("pio", "platformio"):
        found = shutil.which(name)
        if found:
            return found

    home = os.path.expanduser("~")
    for candidate in (
        os.path.join(home, ".platformio", "penv", "Scripts", "platformio.exe"),
        os.path.join(home, ".platformio", "penv", "bin", "platformio"),
    ):
        if os.path.isfile(candidate):
            return candidate

    sys.exit("platformio not found - install it or put it on PATH")


def environments():
    parser = configparser.ConfigParser()
    parser.read(os.path.join(ROOT, "platformio.ini"), encoding="utf-8")
    return [s[4:] for s in parser.sections() if s.startswith("env:")]


def firmware_version():
    header = os.path.join(ROOT, "include", "interface_config.h")
    with open(header, "r", encoding="utf-8") as handle:
        match = re.search(r'#define\s+FIRMWARE_VERSION\s+"([^"]+)"', handle.read())

    if not match:
        sys.exit("FIRMWARE_VERSION not found in include/interface_config.h")
    return match.group(1)


def build_number():
    try:
        with open(os.path.join(ROOT, "build_number.txt"), "r", encoding="utf-8") as h:
            return int(h.read().strip())
    except (OSError, ValueError):
        return 0


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def tracked(path):
    """Is this file already committed? Then replacing it is worth a word."""
    try:
        return subprocess.call(
            ["git", "ls-files", "--error-unmatch", path],
            cwd=ROOT,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        ) == 0
    except OSError:
        return False


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-e", "--env", action="append", dest="envs",
                    help="only this environment, may be repeated")
    ap.add_argument("--no-build", action="store_true",
                    help="collect what .pio/build already holds")
    args = ap.parse_args()

    version = firmware_version()
    envs = args.envs or environments()
    unknown = [e for e in envs if e not in environments()]
    if unknown:
        sys.exit("unknown environment(s): " + ", ".join(unknown))

    os.makedirs(RELEASE_DIR, exist_ok=True)

    if not args.no_build:
        command = [platformio(), "run"]
        for env in envs:
            command += ["-e", env]

        print("Building: " + ", ".join(envs), flush=True)
        if subprocess.call(command, cwd=ROOT) != 0:
            sys.exit("the build failed - nothing was collected")

    ota = {}
    taken = {}

    for env in envs:
        source = os.path.join(ROOT, ".pio", "build", env, "firmware.bin")
        if not os.path.isfile(source):
            print("  %-14s no firmware.bin, skipped" % env)
            continue

        name = "firmware_%s_%s.bin" % (env, version)
        target = os.path.join(RELEASE_DIR, name)

        # An image that is already committed has been published, and the
        # digest in the manifest is what a device checks against. Every build
        # differs anyway - BUILD_NUMBER sits in it - so this is about the
        # version number, not about the bytes.
        if os.path.isfile(target) and tracked(target):
            print("  ! %s is already committed - bump FIRMWARE_VERSION" % name)

        shutil.copyfile(source, target)
        digest = sha256(target)
        size = os.path.getsize(target)

        print("  %-14s %-42s %7.1f KiB  %s" % (env, name, size / 1024.0, digest[:16]))

        key = CHIP_KEYS.get(env)
        if key is None:
            continue                      # a variant, file only

        if key in taken:
            print("      ^ %s already covered by %s, not in the manifest"
                  % (key, taken[key]))
            continue

        taken[key] = env
        ota[key] = {"path": name, "sha256": digest, "build": build_number()}

    manifest = {"version": version, "ota": ota}
    path = os.path.join(RELEASE_DIR, "manifest.json")

    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(manifest, handle, indent=2)
        handle.write("\n")

    print("\nmanifest.json: version %s, %d target(s)" % (version, len(ota)))
    print("Point update_url at the raw URL of this file.")


if __name__ == "__main__":
    main()
