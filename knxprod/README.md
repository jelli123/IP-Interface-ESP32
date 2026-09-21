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
* Mindestens ein Element im `Dynamic`-Teil, sonst verweigert der Export
  mit „Dynamic hat keine Elemente“. Hier ist es eine Parameterseite mit nur
  einem Hinweis: dass das Gerät über sein Web-Dashboard eingestellt wird.
  Ein Parameter ist das nicht, es bleibt dabei, dass die ETS hier nichts
  einstellt.
* Die Bestellnummer **kodiert** in den Ids von Produkt und Katalogeintrag
  (`SBIP-1` als `SBIP.2D1`) – über genau diese Form findet der Import den
  Katalogeintrag.

Nach dem Import unter *Allgemeines → Hardware* den Haken **„Gerät ist ein
Koppler“** setzen. Die Datei trägt `IsCoupler="true"`, aber der Import von
Kaenx-Creator liest dieses Flag nicht, und der Export schreibt es nur mit
gesetztem Haken – ohne ihn fehlt es in der signierten knxprod, und die ETS
sieht ein Endgerät statt eines Kopplers.

Die Frage, ob die Hersteller-ID des Projekts auf `00FA` umgestellt werden
soll, mit *Ja* beantworten. Sonst exportiert Kaenx-Creator unter einer
anderen Kennung, und die ETS findet zum Gerät keine passende Datenbank.

**Unsigniert.** Die ETS nimmt eine knxprod erst nach dem Signieren an; das
verlangt ihre eigenen Bibliotheken und kann dieses Skript nicht. Es schreibt
aber neben die knxprod dieselbe Datenbank als eine Datei,
`M-00FA_A-0001-01.xml`, und die signiert
[OpenKNXproducer](https://github.com/OpenKNX/OpenKNXproducer) auf einem
Rechner mit ETS:

```
OpenKNXproducer knxprod M-00FA_A-0001-01.xml
```

OpenKNXproducer teilt die Datei selbst wieder auf, signiert und legt die
offiziellen KNX-Stammdaten bei; dafür braucht es beim ersten Lauf
Internetzugang.

**Kaenx-Creator zum Signieren nicht verwenden.** Sein Datenmodell kennt
`Options/LineCoupler0912NewProgrammingStyle` nicht und verwirft den Schalter
beim Import. Ohne ihn wählt die ETS für Maske 091A die Prozedur, die den
Koppler über BCU1-Speicher programmiert, und bricht den Download ab
(„unterstützt die Managementprozedur 'LoadLCConfigApp' nicht“). Die Hinweise
zum Import oben gelten weiter, falls Kaenx-Creator den Schalter einmal
kennt.
Werkzeuge, die die Signatur nicht prüfen – etwa SB-Project – kommen mit der
unsignierten Datei zurecht.

Die ausführliche Begründung, warum hier weder Parameter noch
Kommunikationsobjekte stehen, steht im [README](../README.md#eine-eigene-produktdatenbank).
