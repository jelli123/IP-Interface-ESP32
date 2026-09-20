# Produktdatenbank des Selfbus KNX/IP Interface

Die drei XML-Dateien in `M-00FA/` sind eine vollständige Produktdatenbank:
Applikationsprogramm, Hardware und Katalogeintrag. Zusammengepackt ergeben sie
eine `.knxprod` – das Archiv selbst gehört nicht ins Repository, es entsteht
aus diesen Quellen:

```
python3 ../scripts/make_knxprod.py --identity sbip-identity.json
```

Die Kennungen darin müssen zu denen im Gerät passen (Dashboard, Karte
*Geräteidentität*). Vorgabe ist Hersteller `0x00FA`, Applikation 1 Version 1,
Maske `MV-091A`, zehn Tunneladressen – dieselben Werte, die
`include/interface_config.h` für `SBIP_KNX_PRODUCT=0` einkompiliert.

**Unsigniert.** Die ETS nimmt eine knxprod erst nach dem Signieren an; das
verlangt ihre eigenen Bibliotheken und kann dieses Skript nicht. Zum Signieren
die XML-Dateien in [Kaenx-Creator](https://github.com/OpenKNX/Kaenx-Creator)
oder [OpenKNXproducer](https://github.com/OpenKNX/OpenKNXproducer) geben.
Werkzeuge, die die Signatur nicht prüfen – etwa SB-Project – kommen mit der
unsignierten Datei zurecht.

Die ausführliche Begründung, warum hier weder Parameter noch
Kommunikationsobjekte stehen, steht im [README](../README.md#eine-eigene-produktdatenbank).
