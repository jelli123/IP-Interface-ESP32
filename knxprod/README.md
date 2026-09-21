# Produktdatenbank des Selfbus KNX/IP Interface

Die drei XML-Dateien in `M-00FA/` sind eine vollständige Produktdatenbank:
Applikationsprogramm, Hardware und Katalogeintrag. Dazu kommt `knx_master.xml`,
ein Auszug aus den KNX-Stammdaten mit der Maske MV-091A – er landet in der
Wurzel des Archivs und trägt die Ladeprozedur, die das Applikationsprogramm
übernimmt. Zusammengepackt ergeben sie eine `.knxprod` – das Archiv selbst
gehört nicht ins Repository, es entsteht aus diesen Quellen:

```
python3 ../scripts/make_knxprod.py --identity sbip-identity.json
```

Die Kennungen darin müssen zu denen im Gerät passen (Dashboard, Karte
*Geräteidentität*). Vorgabe ist Hersteller `0x00FA`, Applikation 1 Version 1,
Maske `MV-091A`, zehn Tunneladressen – dieselben Werte, die
`include/interface_config.h` als Vorgabe einkompiliert.

**Import in Kaenx-Creator.** Der Import greift auf einige Elemente zu, ohne
zu prüfen, ob es sie gibt; fehlt eines, endet er mit „Object reference not
set to an instance of an object“. Deshalb stehen hier auch Dinge, die für
das Gerät bedeutungslos sind:

* `Static/ParameterTypes`, auch wenn die Liste leer ist.
* `Static/BusInterfaces` mit einem Eintrag je Tunneladresse, sobald
  `AdditionalAddressesCount` nicht null ist. `make_knxprod.py` schreibt die
  Liste passend zu `--tunnels` neu.
* Die Bestellnummer **kodiert** in den Ids von Produkt und Katalogeintrag
  (`SBIP-1` als `SBIP.2D1`) – über genau diese Form findet der Import den
  Katalogeintrag.

Die Frage, ob die Hersteller-ID des Projekts auf `00FA` umgestellt werden
soll, mit *Ja* beantworten. Sonst exportiert Kaenx-Creator unter einer
anderen Kennung, und die ETS findet zum Gerät keine passende Datenbank.

**Unsigniert.** Die ETS nimmt eine knxprod erst nach dem Signieren an; das
verlangt ihre eigenen Bibliotheken und kann dieses Skript nicht. Zum Signieren
die XML-Dateien in [Kaenx-Creator](https://github.com/OpenKNX/Kaenx-Creator)
oder [OpenKNXproducer](https://github.com/OpenKNX/OpenKNXproducer) geben.
Werkzeuge, die die Signatur nicht prüfen – etwa SB-Project – kommen mit der
unsignierten Datei zurecht.

Die ausführliche Begründung, warum hier weder Parameter noch
Kommunikationsobjekte stehen, steht im [README](../README.md#eine-eigene-produktdatenbank).
