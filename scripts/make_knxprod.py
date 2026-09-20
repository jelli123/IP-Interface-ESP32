#!/usr/bin/env python3
"""Packt die Produktdatenbank aus knxprod/ zu einer .knxprod-Datei.

Kein PlatformIO-Skript - von Hand aufzurufen:

    python3 scripts/make_knxprod.py
    python3 scripts/make_knxprod.py --tunnels 5 --identity sbip-identity.json

Eine .knxprod ist ein ZIP-Archiv mit den XML-Dateien eines Herstellers. Mehr
braucht es nicht, damit das Gerät und die Datei zueinander passen - und mehr
kann dieses Skript auch nicht: Eine **Signatur** kann es nicht erzeugen, die
verlangt die ETS-Bibliotheken. Ohne Signatur nimmt die ETS die Datei nicht an;
Werkzeuge wie SB-Project prüfen sie nicht und kommen damit zurecht. Zum
Signieren die XML-Dateien in Kaenx-Creator oder OpenKNXproducer geben.

Das Gegenstück im Gerät ist die Karte "Geräteidentität" im Dashboard. Mit
--identity schreibt dieses Skript die JSON-Datei, die sich dort über "JSON
laden" einspielen lässt; alternativ die erzeugte .knxprod im Bearbeiten-Dialog
einlesen - beides führt zu denselben Werten.
"""
import argparse
import json
import os
import re
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SOURCE = os.path.join(ROOT, "knxprod")

# Was in den mitgelieferten XML-Dateien steht. Alles andere leitet sich per
# Ersetzung daraus ab, damit die drei Dateien untereinander stimmig bleiben.
BASE_MANUFACTURER = 0x00FA
BASE_APP = 0x0001
BASE_APP_VERSION = 0x01

#: Maske dieser Firmware. Nicht einstellbar - siehe src/knx_identity.h.
MASK = 0x091A


def attribute(text, name, value):
    """Ersetzt jedes name="..." durch den neuen Wert."""
    pattern = re.compile(r'(\b%s=")[^"]*(")' % re.escape(name))
    return pattern.sub(lambda m: m.group(1) + str(value) + m.group(2), text)


def build(args):
    folder = "M-%04X" % args.manufacturer
    files = {}

    for name in sorted(os.listdir(os.path.join(SOURCE, "M-00FA"))):
        if not name.endswith(".xml"):
            continue

        with open(os.path.join(SOURCE, "M-00FA", name), encoding="utf-8") as handle:
            text = handle.read()

        # Die Kennungen stecken in den Ids, und die Ids verweisen aufeinander
        # - deshalb über alle Dateien dieselbe Ersetzung, nicht je Datei eine
        # eigene.
        text = text.replace("M-%04X" % BASE_MANUFACTURER, folder)
        text = text.replace(
            "-%04X-%02X-0000" % (BASE_APP, BASE_APP_VERSION),
            "-%04X-%02X-0000" % (args.app, args.app_version),
        )

        text = attribute(text, "ApplicationNumber", args.app)
        text = attribute(text, "ApplicationVersion", args.app_version)
        text = attribute(text, "AdditionalAddressesCount", args.tunnels)
        text = attribute(text, "VersionNumber", args.device_version)

        if args.order:
            text = attribute(text, "OrderNumber", args.order)
            text = attribute(text, "Number", args.order)

        target = name.replace("M-%04X" % BASE_MANUFACTURER, folder)
        files["%s/%s" % (folder, target)] = text

    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--manufacturer", type=lambda v: int(v, 0),
                        default=BASE_MANUFACTURER,
                        help="Herstellerkennung, Vorgabe 0x00FA")
    parser.add_argument("--app", type=lambda v: int(v, 0), default=BASE_APP,
                        help="Applikationsnummer, Vorgabe 1")
    parser.add_argument("--app-version", type=lambda v: int(v, 0),
                        default=BASE_APP_VERSION,
                        help="Applikationsversion, Vorgabe 1")
    parser.add_argument("--device-version", type=lambda v: int(v, 0), default=1,
                        help="Geräteversion (PID_VERSION), Vorgabe 1")
    parser.add_argument("--tunnels", type=int, default=10,
                        help="verwaltete Tunneladressen; muss zu KNX_TUNNELING "
                             "der Firmware passen, Vorgabe 10")
    parser.add_argument("--order", default="SBIP-1",
                        help="Bestellnummer, höchstens zehn Zeichen")
    parser.add_argument("--name", default="Selfbus KNX/IP",
                        help="Produktname für die Identitätsdatei")
    parser.add_argument("--out", help="Zieldatei, Vorgabe knxprod/<name>.knxprod")
    parser.add_argument("--identity", help="zusätzlich die passende JSON-Kennung "
                                           "für das Dashboard schreiben")
    args = parser.parse_args()

    if len(args.order) > 10:
        sys.stderr.write("Die Bestellnummer passt nur mit zehn Zeichen in "
                         "PID_ORDER_INFO.\n")
        return 1
    if not 1 <= args.tunnels <= 255:
        sys.stderr.write("Tunneladressen: 1 bis 255.\n")
        return 1

    files = build(args)

    out = args.out or os.path.join(
        SOURCE, "M-%04X_A-%04X-%02X.knxprod" % (args.manufacturer, args.app,
                                                args.app_version))

    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as archive:
        for name, text in sorted(files.items()):
            archive.writestr(name, text)

    print("geschrieben: %s" % out)
    for name in sorted(files):
        print("  %s" % name)
    print("  Hersteller 0x%04X, Applikation 0x%04X v0x%02X, Maske MV-%04X, "
          "%d Tunneladressen" % (args.manufacturer, args.app, args.app_version,
                                 MASK, args.tunnels))
    print("  unsigniert - die ETS nimmt die Datei erst nach dem Signieren an")

    if args.identity:
        identity = {
            "name": args.name,
            "manufacturer": args.manufacturer,
            "app_number": args.app,
            "app_version": args.app_version,
            "device_version": args.device_version,
            "mask": MASK,
            "tunnels": args.tunnels,
            "hardware_type": "",
            "order_info": args.order,
        }
        with open(args.identity, "w", encoding="utf-8") as handle:
            json.dump(identity, handle, indent=2, ensure_ascii=False)
            handle.write("\n")
        print("geschrieben: %s" % args.identity)

    return 0


if __name__ == "__main__":
    sys.exit(main())
