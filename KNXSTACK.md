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
git am ..\IP-Interface-ESP32\upstream\0001-*.patch
```

```bash
# Linux
git clone https://github.com/thelsing/knx.git
cd knx
git am ../IP-Interface-ESP32/upstream/0001-*.patch
```

Jeder Patch steht für sich und lässt sich einzeln einreichen. Autor und
E-Mail sind Platzhalter – vor dem Einreichen `git commit --amend --reset-author`.

---

## Übersicht

| # | Kurzfassung | Art | Patch |
|---|---|---|---|
| 1 | Antwort geht an einen geschlossenen Tunnelkanal | Fehler | `0001` |
| 2 | Zeiger auf ein totes Stack-Array | undefiniertes Verhalten | `0002` |
| 3 | `isTunnelingPA()` dereferenziert einen Nullzeiger | Absturz | `0003` |
| 4 | `sendBytesUniCast()` meldet Erfolg nach einem Fehlschlag | Fehler | `0004` |
| 5 | `propertyValueRead()` gibt uninitialisierten Heap heraus | Fehler | `0005` |
| 6 | `_couplerType` ohne definierten Wert | undefiniertes Verhalten | `0006` |
| 7 | Unprogrammiert unbrauchbar als reine Schnittstelle | Entwurf | – |
| 8 | Tunnel-Quittungen werden verworfen | Lücke | – |
| 9 | Busmonitor-Verbindung wird wortlos abgelehnt | Diagnose | – |

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

## Was hier bleibt und nicht nach oben gehört

| Änderung in `patch_knx.py` | Warum sie projektspezifisch ist |
|---|---|
| Herstellerproperties 204/209 im `IpParameterObject` | Nachbildung des ABB IPR/S 3.1.1; die Ladeprozedur dieses Produkts schreibt sie |
| Busmonitor-Haken in `data_link_layer.cpp` | Dient allein der Aufzeichnung im Dashboard dieser Firmware |
| Schleifenerkennung für Routing-Indications | Reaktion auf eine konkrete Anlage mit einem zweiten Interface auf derselben Linie |
| `sbipRouteUnfiltered` in `router_object.cpp` | Bewusste Abweichung von der Norm, siehe [README](README.md) |
| Messpunkte hinter `SBIP_KNX_TRACE` | Diagnose für dieses Projekt |
| Zähler für unquittierte Tunnelrahmen | Behelf; die saubere Lösung wäre Punkt 8 |
