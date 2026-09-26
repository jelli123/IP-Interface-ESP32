# Befunde am KNX-Stack

Diese Firmware baut auf [thelsing/knx](https://github.com/thelsing/knx) auf und
verändert die Bibliothek beim Bauen über [scripts/patch_knx.py](scripts/patch_knx.py).
Ein Teil dieser Änderungen ist projektspezifisch und gehört nirgendwo anders hin.
Ein anderer Teil sind Fehler, die jeden treffen, der den Stack als
KNXnet/IP-Schnittstelle betreibt.

Dieses Dokument trennt beides und hält die Belege fest, damit sich daraus ohne
weitere Arbeit Issues und Pull Requests machen lassen.

Bezugspunkt ist Commit `980c047ad7fc5e27bf2fae95e48acde5d5e0b4fd` (master,
04.11.2025). Alle Zeilennummern beziehen sich darauf.

Fertige Patches liegen in [upstream/](upstream/). Sie sind mit
`git format-patch` erzeugt und gegen den unveränderten Master geprüft:

```powershell
# Windows
git clone https://github.com/thelsing/knx.git
cd knx
git am --keep-cr ..\IP-Interface-ESP32\upstream\0001-*.patch
```

```bash
# Linux
git clone https://github.com/thelsing/knx.git
cd knx
git am --keep-cr ../IP-Interface-ESP32/upstream/0001-*.patch
```

`--keep-cr` ist nötig, weil `src/esp_platform.cpp` upstream CRLF-Zeilenenden
hat und `git am` die CRs sonst beim Zerlegen der Mail entfernt – dann findet
es die Stelle nicht. `.gitattributes` schützt die Patchdateien davor, dass
Git ihre Zeilenenden beim Auschecken umwandelt.

Jeder Patch steht für sich und lässt sich einzeln einreichen. Autor und
E-Mail sind Platzhalter – vor dem Einreichen `git commit --amend --reset-author`.

---

## Übersicht

| # | Kurzfassung | Art | Patch |
|---|---|---|---|
| 1 | Antwort geht an einen geschlossenen Tunnelkanal | Fehler | `0001` |
| 2 | Zeiger auf ein totes Stack-Array | undefiniertes Verhalten | `0002` |
| 3 | `isTunnelingPA()` dereferenziert einen Nullzeiger | Absturz | `0003` |
| 4 | `sendBytesUniCast()` und `sendBytesMultiCast()` melden Erfolg nach einem Fehlschlag | Fehler | `0004` |
| 5 | `propertyValueRead()` gibt uninitialisierten Heap heraus | Fehler | `0005` |
| 6 | `_couplerType` ohne definierten Wert | undefiniertes Verhalten | `0006` |
| 7 | Unprogrammiert unbrauchbar als reine Schnittstelle | Entwurf | – |
| 8 | Tunnel-Quittungen werden verworfen | Lücke | – |
| 9 | Busmonitor-Verbindung wird wortlos abgelehnt | Diagnose | – |
| 10 | Pflicht-Properties für Maske 091A fehlen | Lücke | – |
| 11 | Suchantworten versprechen Core 2 und damit TCP | Fehler | – |
| 12 | Data Secure: FDSK und Zufallszahl sind Konstanten | Sicherheitslücke | – |
| 13 | Data Secure: Folgenummern beginnen nach jedem Neustart von vorn | Fehler | – |
| 14 | Data Secure: MAC bei reiner Authentifizierung falsch berechnet | Fehler | – |
| 15 | Data Secure: Sicherheit wird entschlüsselt, aber nie geprüft | Sicherheitslücke | – |
| 16 | Data Secure im Koppler verschiebt das Speicherabbild | Entwurf | – |
| 17 | `PID_KNXNETIP_DEVICE_CAPABILITIES` meldet nur Device Management | Fehler | – |

---

## 1 – Eine Antwort landet auf einem geschlossenen Kanal

**Datei:** `src/knx/ip_data_link_layer.cpp`, Zeilen 76 und 139

`dataRequestToTunnel()` und `dataConfirmationToTunnel()` suchen den passenden
Tunnel über die physikalische Adresse, ohne den Kanal anzusehen:

```cpp
for (int i = 0; i < KNX_TUNNELING; i++)
{
    if (tunnels[i].IndividualAddress == frame.sourceAddress())
        continue;

    if (tunnels[i].IndividualAddress == frame.destinationAddress())
    {
        tun = &tunnels[i];
        break;
    }
}
```

Ein nie benutzter Slot trägt `IndividualAddress == 0`, und `Reset()` setzt ihn
beim Trennen wieder darauf zurück. Ein cEMI-Rahmen, dessen Quelladresse noch
nicht eingetragen ist, trägt ebenfalls 0. Die Suche liefert dann eine
geschlossene Verbindung, und die Antwort geht an `IpAddress` 0, Port 0.

Der Zweig für Gruppenadressen elf Zeilen darüber prüft `ChannelId != 0` bereits –
die beiden Unicast-Suchen ziehen damit nur nach.

---

## 2 – Zeiger auf ein Array, das es nicht mehr gibt

**Datei:** `src/knx/ip_data_link_layer.cpp`, `loopHandleConnectRequest()`

```cpp
else    // no tunnel PA configured, that means device is unconfigured and has 15.15.0
{
    uint8_t addrbuffer[KNX_TUNNELING * 2];
    addresses = (uint8_t*)addrbuffer;
    ...
    _ipParameters.writeProperty(PID_ADDITIONAL_INDIVIDUAL_ADDRESSES, 1, addrbuffer, count);
}
```

`addrbuffer` lebt nur bis zur schließenden Klammer, `addresses` wird weit
darunter gelesen:

```cpp
popWord(tunPa, addresses + (tunIdx * 2));
```

In der Praxis geht das oft gut, weil der Stack dazwischen nicht überschrieben
wird – undefiniertes Verhalten bleibt es trotzdem, und ein AddressSanitizer-Lauf
findet es sofort. Die Werte stehen nach `writeProperty()` ohnehin in der
Property; der `if`-Zweig darüber zeigt bereits genau dorthin.

---

## 3 – Ein Zeiger, den niemand setzt

**Dateien:** `src/knx/data_link_layer.h`, `src/knx/data_link_layer.cpp`

`DataLinkLayer` hält einen `IpParameterObject* _ipParameters`. Der Setter, der
ihn füllen würde, ist deklariert –

```cpp
void ipParameterObject(IpParameterObject* object);
```

– **hat aber im gesamten Baum keine Definition und keinen Aufrufer.** Zusätzlich
verdeckt `IpDataLinkLayer` das Member mit einer gleichnamigen *Referenz*:

```cpp
IpParameterObject& _ipParameters;   // ip_data_link_layer.h:52
```

Die Basisklasse sieht dieses Objekt also nie. `isTunnelingPA()` dereferenziert
den Zeiger, sobald mit `KNX_TUNNELING` ein Unicast-Rahmen weitergereicht wird:

```cpp
uint16_t* addresses = _ipParameters->additionalIndivualAddresses(numAddresses);
```

Auf dem ESP32 endet das als `LoadProhibited` in `InterfaceObject::property()`.

Der Patch verhindert nur den Absturz. „Keine Tunneladresse" zu antworten
schaltet lediglich eine Optimierung ab: Der Rahmen geht auf TP1, wie er es ohne
`KNX_TUNNELING` täte. Ob das Member richtig verdrahtet oder samt totem Setter
entfernt und die Prüfung virtuell gemacht wird, ist eine Entscheidung für den
Maintainer.

---

## 4 – Erfolg melden, obwohl nichts gesendet wurde

**Dateien:** `src/esp_platform.cpp`, `src/esp32_platform.cpp`,
`src/libretiny_platform.cpp`, `src/rp2040_arduino_platform.cpp`

```cpp
if (_udp.beginPacket(ucastaddr, port) == 1)
{
    _udp.write(buffer, len);

    if (_udp.endPacket() == 0)
        println("sendBytesUniCast endPacket fail");
}
else
    println("sendBytesUniCast beginPacket fail");

return true;
```

Der Fehler wird protokolliert und dann verworfen. Jeder Aufrufer, der den
Rückgabewert auswertet, erfährt, das Paket sei draußen – niemand wiederholt,
niemand meldet etwas. Der Rahmen ist weg.

Das ist kein theoretischer Fall. Auf einem ESP32 mit WLAN laufen die
Sendepuffer leer, wenn Telegramme dicht aufeinander folgen – also genau bei
einem ETS-Download. Das Fehlerbild: Der Download bricht an wechselnder Stelle
ab, im Protokoll steht `sendBytesUniCast endPacket fail`, und weil es ein
Zeitproblem ist, verschwindet der Fehler, sobald man die Diagnose einschaltet.

Nebenbei: `esp_platform.cpp` und `libretiny_platform.cpp` geben
`sendBytesUniCast endPacket fail` zusätzlich **bedingungslos** aus, bevor das
Paket überhaupt beginnt.

`esp32_idf_platform.cpp` und `linux_platform.cpp` machen es bereits richtig.

Diese Firmware geht über den Patch hinaus und wiederholt bis zu dreimal im
Abstand von 400 µs, bevor sie aufgibt – das ist eine Anwendungsentscheidung und
steht bewusst nicht im Vorschlag.

**Multicast hat denselben Fehler, stiller.** `sendBytesMultiCast()` in
`esp32_platform.cpp` wertet `endPacket()` gar nicht aus:

```cpp
_udp.beginMulticastPacket();
_udp.write(buffer, len);
_udp.endPacket();
return true;
```

Aufgefallen beim Download über KNXnet/IP-Routing, während ein großer
HTTP-Transfer die Sendepuffer belegte: Die Antworten an die ETS wurden
abgewiesen, und nichts meldete es. Diese Firmware wiederholt auch hier
(Patch 9b in `scripts/patch_knx.py`). `0004` deckt beide Funktionen auf allen
vier Arduino-Plattformen ab; `esp_platform.cpp`, `libretiny_platform.cpp` und
`rp2040_arduino_platform.cpp` haben denselben Multicast-Fehler.

---

## 5 – Uninitialisierter Heap als Rückgabewert

**Datei:** `src/knx/bau_systemB.cpp`, `BauSystemB::propertyValueRead()`

```cpp
if (startIndex > 0)
    size = elementSize * numberOfElements;
else
    size = sizeof(uint16_t);

*data = new uint8_t [size];
obj->readProperty((PropertyID)propertyId, startIndex, elementCount, *data);
...
length = size;
```

`readProperty()` setzt `elementCount` auf 0, wenn die Property nicht existiert;
`Property::read()` tut dasselbe, wenn der angeforderte Bereich nicht verfügbar
ist. In beiden Fällen wird der Puffer **nicht beschrieben** – `length` meldet
aber weiterhin die angeforderte Größe.

Wer `length` glaubt, liest uninitialisierten Heap. In dieser Firmware hat ein
Lesen von `PID_FRIENDLY_NAME` auf einem unprogrammierten Gerät die Reste eines
fremden Strings zurückgegeben, die dann über die Weboberfläche zu sehen waren –
ein Informationsleck.

`elementCount` kennt die Antwort bereits; der Patch meldet die Größe, die dazu
gehört.

---

## 6 – Ein Kopplertyp ohne definierten Wert

**Dateien:** `src/knx/network_layer_coupler.h` (Zeile 78), `.cpp`

```cpp
CouplerType _couplerType;      // kein Initialisierer
```

Der Konstruktor listet das Member nicht auf und ruft `evaluateCouplerType()`.
Diese Funktion setzt es nur für Adressen, die auf `.0` enden:

```cpp
else
{
    // Device is not a router, check if TP1 bridge or TP1 repeater
    /*
          if (PID_L2_COUPLER_TYPE.BIT0 == 0)
          ...
    */
}
```

Der `else`-Zweig ist ein auskommentierter Entwurf und lässt das Member in Ruhe.
Zwei Folgen:

* Der erste Aufruf auf einem Gerät, dessen gespeicherte Adresse nicht `x.y.0`
  ist, liest einen unbestimmten Wert.
* Ein späterer Aufruf, nachdem die ETS eine solche Adresse geschrieben hat,
  behält den vorherigen.

`routeDataIndividual()` arbeitet dann mit einem Kopplertyp, den das Gerät nicht
hat – oder steigt an seinem Zweig `//unknown coupler type, should not happen`
aus. Der verwirft **jedes physikalisch adressierte Telegramm, ohne ein Wort**.
Broadcasts laufen weiter, die ETS findet das Gerät also, kann aber nicht mit ihm
sprechen. Das ist außerordentlich schwer zu finden.

Der Patch implementiert die fehlende Bridge-/Repeater-Logik nicht, er macht den
Wert nur definiert.

---

## 7 – Unprogrammiert unbrauchbar als reine Schnittstelle

**Kein Patch** – das ist eine Entwurfsfrage, keine Fehlerkorrektur.

Ohne ETS-Download trägt das Gerät `15.15.0`. Zwei Stellen behandeln diese
Werksadresse wie eine echte Linienadresse:

`DataLinkLayer::isRoutedPA()` (`data_link_layer.cpp`):

```cpp
uint16_t ownpa = _deviceObject.individualAddress();
...
return (pa & own_sm) != ownpa;
```

`NetworkLayerCoupler::isRoutedIndividualAddress()`:

```cpp
if (ZS != ownSNA)
    return false;              // IGNORE_TOTALLY
```

Ein Ziel wie `1.1.3` gehört nicht zu `15.15.x`. Also entscheidet
`dataRequestFromTunnel()`, ein Koppler werde den Rahmen schon tragen, und kehrt
vor `sendFrame()` zurück – die Busleitung sieht die ETS-Anfrage nie. Der
Anwender kann genau ein Gerät erreichen: dieses hier, weil die lokale
Zustellung vorher abzweigt.

Für einen Linienkoppler ist die Logik richtig. Für ein Gerät, das noch gar nicht
weiß, wo es steht, ist sie es nicht – und ohne Projektierung ist ein
KNXnet/IP-Gerät nun einmal eine Schnittstelle, kein Koppler.

Vorschlag zur Diskussion: Solange keine Filtertabelle geladen ist, sollten beide
Prüfungen „nicht geroutet" antworten. Diese Firmware macht das über einen
Schalter, der auch von Hand setzbar ist.

---

## 8 – Tunnel-Quittungen werden verworfen

**Datei:** `src/knx/ip_data_link_layer.cpp`

```cpp
case TunnelingAck:
{
    //TOOD nothing to do now
    //println("got Ack");
    break;
}
```

Die KNXnet/IP-Spezifikation verlangt, dass der Server eine Quittung abwartet und
den Rahmen nach einer Sekunde einmal wiederholt. Nichts davon existiert: Es gibt
keine Struktur, die einen gesendeten Rahmen mit Sequenznummer und Zeitstempel
vorhält, und keinen Wiederholungstimer.

Praktische Folge für die Fehlersuche: „Die ETS zeigt nichts an" und „die Rahmen
kommen nie an" sind von der Geräteseite aus nicht unterscheidbar. Schon ein
Zähler beider Richtungen würde das trennen.

---

## 9 – Die Busmonitor-Verbindung wird wortlos abgelehnt

**Datei:** `src/knx/ip_data_link_layer.cpp`, `loopHandleConnectRequest()`

```cpp
if (connRequest.cri().type() == TUNNEL_CONNECTION && connRequest.cri().layer() != 0x02)
{
    //We only support 0x02!
#ifdef KNX_LOG_TUNNELING
    println("Only LinkLayer ist supported!");
#endif
    KnxIpConnectResponse connRes(0x00, E_TUNNELING_LAYER);
```

Der Busmonitor der ETS verlangt `TUNNEL_BUSMONITOR`. Die Absage ist korrekt,
aber die einzige Erklärung steht hinter `KNX_LOG_TUNNELING`, das in einem
normalen Build nicht gesetzt ist. Der Anwender sieht nur eine Verbindung, die
nicht zustande kommt.

Diese eine Meldung gehört nicht hinter ein Diagnose-Flag. (Der Gruppenmonitor
funktioniert, der Busmonitor nicht – das ist eine häufige Verwechslung und
lohnt einen Satz in der Dokumentation.)

Kleinigkeit an derselben Stelle: `KnxIpTunnelConnection::Reset()` gibt

```
Close Tunnel-Connection[?], Channel: 0x1
```

aus – das `?` ist fest verdrahtet, obwohl der Aufrufer den Index kennt.

---

## 10 – Pflicht-Properties für Maske 091A fehlen

**Dateien:** `src/knx/router_object.cpp`, `src/knx/ip_parameter_object.cpp`

Produktdaten für KNXnet/IP-Router der Maske 091A fügen in deren Ladeprozedur
ein Fragment ein (`MergedProcedure`), das `PID_COUPL_SERV_CONTROL` (57) im
Router-Objekt und `PID_ROUTING_BUSY_WAIT_TIME` (78) im
KNXnet/IP-Parameterobjekt schreibt. Die Maske selbst schreibt dort nur
`LCCONFIG` (52 bis 55). Beide fehlen im Stack:

- `RouterObject::initialize()` legt PID 57 nur für Koppler-Modell 2.0 an,
  `Bau091A` nutzt aber Modell 1.x. Laut 06 Profiles A.3.3 gehört die Property
  auch zu Maske 091A (Lesen Stufe 3, Schreiben Stufe 0). In `bau091A.cpp` steht
  sie bereits als ToDo.
- `IpParameterObject` kennt PID 78 nicht, obwohl 03_08_03 2.5.28 sie für jeden
  KNXnet/IP-Router verlangt (Vorgabe 100 ms, 20 bis 100 ms).

Das Gerät beantwortet den Schreibzugriff mit null Elementen, der Download bricht
ab:

```
PropertyValueWrite    0139100110
PropertyValueResponse 01390001
```

`patch_knx.py` legt beide als reine Datenproperties an – PID 57 mit
`EN_SNA_READ` gesetzt (Vorgabe nach 03_05_01 4.5.8), PID 78 mit 100 ms.
Ausgewertet werden sie noch nicht.

Beim Nachrüsten ist Vorsicht geboten: Jede zusätzliche beschreibbare Property
verändert das Speicherabbild im Flash. `Memory::readMemory()` liest die
Interface-Objekte als einen Bytestrom ohne Länge je Objekt und prüft nur
`DeviceObject::apiVersion`, Hersteller, Hardwaretyp und Version. Ein Gerät, das
mit dem alten Stand programmiert wurde, liest nach einem Update ab der neuen
Property verschobene Bytes. `DataProperty::restore()` übernimmt daraus
Elementzahlen, reserviert Speicher dafür, und das Gerät startet nicht mehr.

Ein Upstream-Patch sollte deshalb `apiVersion` erhöhen. Dann verwirft der Stack
den alten Flashinhalt, und das Gerät muss neu programmiert werden, statt beim
Start abzustürzen.

---

## 11 – Suchantworten versprechen Core 2 und damit TCP

**Dateien:** `src/knx/knx_ip_search_response.cpp`,
`src/knx/knx_ip_search_response_extended.cpp`

```cpp
_supportedServices.serviceVersion(Core, KNX_SERVICE_FAMILY_CORE);
```

`KNX_SERVICE_FAMILY_CORE` schaltet zwei Dinge zugleich: ob der Stack
`SEARCH_REQUEST_EXTENDED` beantwortet, und welche Core-Version er in der
Liste der unterstützten Dienste ankündigt. Core 2 umfasst aber auch
KNXnet/IP über TCP, und das implementiert der Stack nicht – jeder Endpunkt,
den er herausgibt, ist `IPV4_UDP`. Die Beschreibungsantwort meldet dagegen
fest Core 1, das Gerät widerspricht sich also selbst.

Die ETS 6 liest die 2 aus der Suche und baut ihren Tunnel über TCP auf. Das
Gerät weist die Verbindung ab („der Zielcomputer verweigerte die
Verbindung“), und kein einziger KNXnet/IP-Rahmen kommt an. Über Routing geht
es, weil dafür keine Verbindung aufgebaut wird.

Diese Firmware kündigt in beiden Suchantworten Core 1 an und beantwortet die
erweiterte Suche trotzdem (Patch 18 in `scripts/patch_knx.py`). Upstream
gehören die beiden Bedeutungen getrennt: die angekündigte Version darf nur
so hoch sein wie das, was der Stack tatsächlich kann.

Seit KNXnet/IP über TCP außerhalb des Stacks läuft (`src/knxip_shim.cpp`),
kündigt die Firmware wieder Core 2 an – aber nur, solange der TCP-Listener
tatsächlich läuft.

---

## 12 – FDSK und Zufallszahl sind Konstanten

**Dateien:** `src/knx/security_interface_object.cpp`,
`src/knx/secure_application_layer.cpp`

```cpp
const uint8_t SecurityInterfaceObject::_fdsk[] = { 0x00, 0x01, 0x02, ... 0x0F };
...
uint64_t SecureApplicationLayer::getRandomNumber()
{
    return 0x000102030405; // TODO: generate random number
}
```

Der FDSK ist der Tool-Key eines Geräts im Auslieferungszustand und steht
auf seinem Aufkleber. Im Stack ist er für jedes jemals gebaute Gerät derselbe
– wer ihn kennt, kennt ihn für alle, und die sichere Inbetriebnahme schützt
vor niemandem. Die „Zufallszahl“ ist die Challenge jedes `S-A_Sync_Req` und
die Maske jedes `S-A_Sync_Res`.

Diese Firmware erzeugt den FDSK je Gerät beim ersten Start und hält ihn im
NVS (`src/knx_secure_store.cpp`), setzt ihn nach dem Start und nach einem
Master-Reset als Tool-Key ein und liefert Zufallszahlen aus einem mit
Hardware-Entropie geimpften CTR-DRBG (Patch 21 und 22). Upstream bräuchte es
eine Plattformfunktion für beides.

---

## 13 – Folgenummern beginnen nach jedem Neustart von vorn

**Datei:** `src/knx/secure_application_layer.h`

```cpp
uint64_t _sequenceNumberToolAccess = 50;
uint64_t _sequenceNumber = 0;
```

Data Secure verwirft jeden Rahmen, dessen Folgenummer nicht über der zuletzt
angenommenen desselben Absenders liegt. Die Zähler leben nur im RAM; nach
einem Neustart sendet das Gerät Nummern, die die ETS längst gesehen hat, und
seine Antworten werden verworfen. Dasselbe gilt für die zuletzt angenommene
Nummer des Tools – nach einem Neustart ließe sich ein aufgezeichneter Rahmen
wiederholen. Und was die ETS bei der Inbetriebnahme in
`PID_SEQUENCE_NUMBER_SENDING` schreibt, liest der Stack nie zurück.

Nebenbei führt der Stack zwei Sendezähler (für Tool-Zugriff mit einer
nicht genormten PID 250); AN158 kennt einen.

Diese Firmware hält Hochwassermarken im NVS, reserviert in Blöcken zu 1000
und lässt beide Zähler vom höchsten bekannten Wert weiterzählen, die
geschriebenen Properties eingeschlossen (Patch 22).

---

## 14 – MAC bei reiner Authentifizierung falsch berechnet

**Datei:** `src/knx/secure_application_layer.cpp`, `calcAuthOnlyMac()` und
`decrypt()`

Drei Abweichungen von AN158, die sich gegenseitig nicht aufheben:

* Die Zusatzdaten sind SCF und APDU; der Stack lässt das SCF weg.
* B0 kündigt bei reiner Authentifizierung keine Nutzdaten an (Längenoktett
  0); der Stack trägt die APDU-Länge ein.
* Der MAC ist der **letzte** Chiffreblock; der Stack nimmt den ersten.

Dazu überschreibt `decrypt()` die empfangene APDU danach mit
`memcpy(plainApdu, secureAsdu, …)` – `secureAsdu` zeigt auf das SCF, nicht
auf die APDU.

Geprüft gegen den Referenzvektor aus bussard (Schlüssel `00..0F`,
Folgenummer 42, 1.1.1 → 1/2/3, APDU `00 81`): richtig ist `2e51ca4a`, die
Rechnung des Stacks ergibt `b2f48e88` (`test/secure_crypto_test.cpp`). Die
ETS nutzt für Tool-Zugriff Authentifizierung mit Verschlüsselung, deshalb
fällt es beim Programmieren nicht auf; gesicherte Gruppentelegramme mit
reiner Authentifizierung scheitern. Behoben in Patch 22.

---

## 15 – Sicherheit wird entschlüsselt, aber nie geprüft

**Dateien:** `src/knx/application_layer.cpp`, `src/knx/bau_systemB.cpp`

Der Secure Application Layer entschlüsselt `S-A_Data` und reicht die
gefundene Sicherheit (`SecurityControl`) weiter – ausgewertet wird sie nur
für Gruppenobjekte. Im Secure-Modus nimmt das Gerät ein ungesichertes
`A_PropertyValue_Write` genauso an wie ein gesichertes, und Tool-Key,
Gruppenschlüssel und Passwort-Hashes lassen sich von jedem lesen.

3/5/1 ordnet jedem Dienst und jeder Property eine Access Policy zu, je Rolle
und Sicherheitsstufe, getrennt für Secure-Modus an und aus. Diese Firmware
prüft sie an den drei Stellen, an denen der Application Layer eingehende
Dienste an die BAU übergibt, und im cEMI-Server (Patch 23,
`src/knx_access_policy.cpp`). Eine abgewiesene Property-Anfrage wird mit
0 Elementen bzw. `AccessDenied` beantwortet.

Nebenbefund: der Secure Application Layer schreibt jeden Rahmen ins Log,
entschlüsselte im Klartext – bei der Inbetriebnahme also auch den Tool-Key,
den die ETS schreibt. Seine Ausgaben sind hier abgeschaltet.

---

## 16 – Data Secure im Koppler verschiebt das Speicherabbild

**Dateien:** `src/knx/bau_systemB_coupler.cpp`, `src/knx/bau091A.cpp`

Mit `USE_DATASECURE` meldet `BauSystemBCoupler` sein Sicherheitsobjekt direkt
hinter dem Geräteobjekt für das Speicherabbild an. Alles dahinter rutscht,
und ein Gerät, das mit einer Firmware ohne Data Secure programmiert wurde,
liest seine Tabellen nach dem Update vom falschen Ort. Eine Versionskennung,
die das erkennen ließe, hat das Abbild nicht.

Außerdem verlangt `Bau091A::configured()` ein geladenes Sicherheitsobjekt.
Ein Gerät, das die ETS ohne Secure programmiert, lädt es nie und gilt damit
für immer als unprogrammiert.

Diese Firmware hält das Sicherheitsobjekt im NVS statt im Abbild und verlangt
es nur im Secure-Modus (Patch 21). Upstream wäre eine Versionskennung des
Abbilds der bessere Weg.

---

## 17 – `PID_KNXNETIP_DEVICE_CAPABILITIES` meldet nur Device Management

**Datei:** `src/knx/ip_parameter_object.cpp`

PID 68 ist nach 03_08_03 2.5.19 ein Bitfeld der unterstützten Dienstfamilien:
Bit 0 Device Management, Bit 1 Tunnelling, Bit 2 Routing, Bit 6 Security. Der
Stack liefert fest `0001h`, obwohl er tunnelt und routet. 03_08_09 2.6.2.1
verlangt bei einem Secure-Gerät zusätzlich das Security-Bit.

Diese Firmware liefert `0047h` (Patch 24). Upstream gehört der Wert aus den
tatsächlich übersetzten Familien zusammengesetzt, also mindestens Bit 1 und 2
bei `KNX_TUNNELING` bzw. Routing und Bit 6 bei KNXnet/IP Secure.

---

## Was hier bleibt und nicht nach oben gehört

| Änderung in `patch_knx.py` | Warum sie projektspezifisch ist |
|---|---|
| Herstellerproperties 204/209 im `IpParameterObject` | Ladeprozeduren fremder Produkte schreiben herstellereigene Properties; ohne sie bricht der Download dort ab. Genau diese Klasse von Fehlern meldet die knxprod-Prüfung im Dashboard vorab, siehe `KnxLink::objectsJson()` |
| PID 57 und 78 als `VolatileDataProperty` (nur im RAM) | Hält das Flash-Abbild programmierter Geräte gültig, ohne `apiVersion` zu erhöhen; upstream gehören sie gespeichert, siehe Punkt 10 |
| Busmonitor-Haken in `data_link_layer.cpp` | Dient allein der Aufzeichnung im Dashboard dieser Firmware |
| Schleifenerkennung für Routing-Indications | Reaktion auf eine konkrete Anlage mit einem zweiten Interface auf derselben Linie |
| `sbipRouteUnfiltered` in `router_object.cpp` | Bewusste Abweichung von der Norm, siehe [README](README.md) |
| Messpunkte hinter `SBIP_KNX_TRACE` | Diagnose für dieses Projekt |
| Zähler für unquittierte Tunnelrahmen | Behelf; die saubere Lösung wäre Punkt 8 |
| Management-Sperre pro Weg (`sbipManagementHook`) | Einstellung dieser Firmware; der Standard sieht dafür KNX Data Secure vor |
| KNXnet/IP über TCP und KNXnet/IP Secure (`src/knxip_shim.cpp`) | Liegt außerhalb des Stacks, der beides nicht kennt; er sieht weiter nur UDP. Upstream wäre das eine eigene Transportschicht |
| Keine Extended Frames an den TP-UART (Patch 25) | Der Emulator auf dem SB-Interface kann sie noch nicht; eine Eigenschaft dieser Hardware, nicht des Stacks |
| IP-Secure-Properties als Callback-Properties (Patch 19) | Halten das Speicherabbild programmierter Geräte gültig; die Werte liegen im NVS der Firmware |
| Tunnelwahl je Secure-Benutzer (Patch 20) | Gehört upstream in `loopHandleConnectRequest()`, sobald der Stack Secure Sessions kennt |
