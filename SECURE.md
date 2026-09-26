# KNX Secure für den Router

Dieses Dokument plant und begleitet die Erweiterung um **KNXnet/IP Secure**
und **KNX Data Secure**. Es hält fest, was die Spezifikation von einem
KNXnet/IP-Router verlangt, wie es in dieser Firmware umgesetzt ist, was davon
geprüft ist und was erst ein Test mit der ETS zeigen kann.

Maßgeblich ist der **KNX Standard v3.0.0**. Die Abschnittsnummern in diesem
Dokument und in den Quelltexten beziehen sich auf:

| Kürzel | Dokument |
|---|---|
| 3/8/9 | 03_08_09 KNX IP Secure v01.01.02 AS |
| 3/8/2 | 03_08_02 Core v01.06.02 AS (TCP: 8.4.3) |
| 3/8/3 | 03_08_03 Management v01.07.03 AS |
| 3/8/4 | 03_08_04 Tunnelling v01.07.01 AS |
| 3/5/1 | 03_05_01 Resources v01.10.01 AS (FDSK: 6.1, Security-Objekt: 6.3) |
| AN193 | AN193 v04 Access Policies AS |
| AN192 | AN192 v06 Coupler security extensions AS |
| TSSJ/TSSK | 08 Konformitätstests Data Security v1.11 / KNXnet/IP Security v1.1 |

Die Byte-Formate sind zusätzlich gegen die Beispiele in Annex A von 3/8/9
geprüft (Host-Test und Selbsttest beim Start).

---

## 1. Reihenfolge – und warum sie nicht ganz „erst IP, dann Data“ ist

Gewünscht war zuerst IP Secure, dann Data Secure. Daran hält sich der Plan im
Großen, mit einer Einschränkung, die aus der Spezifikation folgt:

**Die ETS schreibt die Schlüssel für IP Secure über Data Secure.** Backbone
Key, Device Authentication Code und Passwort-Hashes (PID 91–93) haben die
Access Policy `008/008`, Tunnel-User und gesicherte Dienste `15F/04C`
(3/8/9 2.3.1, AN193): schreibbar nur mit *Tool Access, Authentication +
Confidentiality*, also als `S-A_Data` mit dem Tool Key – im Auslieferungs-
zustand dem FDSK. 3/8/9 2.2.2.1: „This Secure Backbone Key is transferred …
using a secure connection to the devices (via KNX Data Security possibly using
secured KNXnet/IP Device Management).“ Und 3/8/9 2.5.1.2 verlangt für jedes
IP-Secure-Profil das Profilmodul „S-AL“.

| Stufe | Inhalt | Status |
|---|---|---|
| **A** | Gemeinsame Grundlage: Krypto, FDSK je Gerät, echte Zufallszahlen, Folgenummern, Secure Application Layer für das *Management* des Routers, Access Policies (AN193) | umgesetzt |
| **B** | KNXnet/IP Secure: TCP (Core v2), Secure Sessions, Secure Routing mit Timer-Synchronisation, DIBs, Zugriffsregeln | umgesetzt |
| **C** | KNX Data Secure als Koppler: Durchleiten gesicherter Telegramme | umgesetzt; **auf TP ohne Extended Frames stark eingeschränkt**, siehe 2.3 |
| **D** | Produktdatenbank, Dashboard, Doku | umgesetzt |
| **E** | Test mit ETS 6 und einem zweiten Secure-Router | offen – braucht Hardware |

---

## 2. Anforderungen und Umsetzung

### 2.1 KNXnet/IP Secure (3/8/9)

| Anforderung | Stelle | Umsetzung |
|---|---|---|
| `SECURE_WRAPPER`, CCM mit A = Header + Session-ID, B0/Ctr0 nach Fig. 8/9, MAC 16 Oktette | 2.2.1.3 | `src/knxip_secure_frames.cpp`, Annex A geprüft |
| Wrapper unter 44 Oktetten verwerfen | 2.2.1.3.3 | ja |
| Session: X25519, Session Key = SHA-256(Shared Secret)[0..15], neues Schlüsselpaar je Session | 2.2.3.1 | `KnxIpSecure::sessionRequest` |
| Secure Sessions **nur über TCP**; `SESSION_REQUEST` über UDP oder ohne TCP-Route-back-HPAI verwerfen | 2.2.3.3, 2.2.3.6.4 | ja |
| Keine freie Session → **keine** Antwort | 2.2.3.7.6 | ja |
| Folgenummer je Richtung ab 0, ≤ letzter verwerfen; Message Tag 0 | 2.2.3.3, 2.2.1.3.1 | ja |
| Session-Zustandsautomat: 10 s bis zur Anmeldung, 60 s Idle, Keepalive, Close, Unauthenticated | 2.2.3.5.2 | `KnxIpSecure::loop`, `sessionUnwrap` |
| `SESSION_AUTHENTICATE`: 24 Oktette, reserviertes Oktett 0, User 1..7Fh mit Passwort-Hash, nur einmal | 2.2.3.8, 2.4.1 | ja |
| Auslieferungszustand: Device Authentication Code = **FDSK**, User 1 = leeres Passwort (`E9C304B9…`), Backbone Key 0, nichts gesichert, Latenz 2000 ms, Anteil 1Ah | 2.3.1.2–2.3.1.8 | `KnxIpSecure::clearConfiguration` |
| Zugriff auf `CONNECT_REQUEST`: ungesichert nur, wenn die Familie nicht gesichert ist; in einer Session Device Management nur User 1 (wenn gesichert), Tunnel nur nach PID 97 (wenn gesichert) | 2.2.1.4.2, 2.3.1.8 | `KnxIpShim::checkConnect`, Patch 20 |
| Erweitertes CRI: `E_NO_TUNNELLING_ADDRESS` 2Dh, `E_AUTHORISATION_ERROR` 28h, `E_CONNECTION_IN_USE` 2Eh | 3/8/2 Tab. 8 | ja |
| Verbindungen einer Session nur in dieser Session; `DISCONNECT`/`CONNECTIONSTATE` von außerhalb → `E_CONNECTION_ID` | 2.4.1 | `KnxIpShim::checkChannel` |
| cEMI `M_Prop` immer anonym (ohne Data Security, Rolle „unlisted“) | 2.2.1.4.3 | `checkCemiAccess` |
| Kein Wrapper im Wrapper, keine Remote Configuration | 2.2.1.2.2.2, 2.2.1.4.7 | ja |
| Secure Routing: gesicherte Wrapper **genau dann**, wenn Routing gesichert ist; dann jede Routing.ind gewrappt, keine TIMER_NOTIFY sonst | 2.2.1.4.5 | ja |
| Multicast-Timer 48 bit, nie rückwärts, auch nicht über Stromausfall: Persistenz höchstens je Stunde Timerzeit, beim Start + Intervall | 2.2.2.2.2, 2.2.4.2 | `persistTimer`, `restartSync` |
| Neuer Backbone Key → Timer 0, Synchronisation neu | 2.2.2.3.2.5 E11, 2.3.1.2.3 | ja |
| Timer-Zustandsautomat E01–E11, Zeitkonstanten aus Latenztoleranz und Anteil | 2.2.2.3.2 | `timerCheck`, `reschedule` |
| Start: erster TIMER_NOTIFY nach 0–10 s Zufall (sofort, wenn gesendet werden muss); authentischen Timer abwarten, bis dahin keine Wrapper verarbeiten, aber ihren Zeitstempel übernehmen | 2.2.2.3.2.8 | ja |
| TIMER_NOTIFY genau 36 Oktette, MAC vor der Auswertung prüfen | 2.2.2.4.4 | ja |
| Discovery ungesichert; Familie 09h Version 1 **nur** in der erweiterten Suchantwort; DIB 06h nur dort und nur wenn etwas gesichert ist | 2.6.2 | `rewriteDescription` |
| `PID_KNXNETIP_DEVICE_CAPABILITIES` mit Security-Bit | 2.6.2.1, 3/8/3 2.5.19 | Patch 24 (0047h) |
| PID 94 als Funktions-Property: Write `[0,0,Familie,Version]` → `[RC,0]`; Read `[0,0,Familie]` → `[RC,0,Familie,Version]`; F2h/F8h | 2.3.1.5 | `propertyFunction` |
| PID 79 `PID_TUNNELLING_ADDRESSES` vom Gerät gefüllt (1..n) | 3/8/3 2.5.29 | ja |
| Schlüssel nie lesbar | 2.3.1.2.2 ff. | Callback liefert 0 Elemente; Access Policy 008/008 |

### 2.2 TCP (3/8/2 8.4.3, 3/8/4 2.6.2)

| Anforderung | Umsetzung |
|---|---|
| Rahmen aus dem Strom anhand der Kopflänge; fehlerhafter Kopf → schließen; zu lang → überspringen, nicht schließen | `KnxIpShim::readTcp` |
| Halbgeschlossene Verbindung → schließen | ja |
| Ohne Verbindung/Session 10 s ohne Oktett → schließen; mit aktiver nie | `KnxIpShim::loop` |
| Letzte Verbindung/Session vom Server wegen Timeout beendet → TCP schließen | ja |
| Mehrere Sessions und ungesicherte Verbindungen je TCP-Verbindung | Pseudo-Port je Session |
| Kein `TUNNELLING_ACK`, Sequenzzähler nicht auswerten | ACKs verworfen, Zähler umgeschrieben |
| Route-back-HPAI in `CONNECT_RESPONSE` | ja |
| `SEARCH_REQUEST` nur über UDP; `SEARCH_REQUEST_EXTENDED` auch über TCP | ja |

### 2.3 KNX Data Secure (3/5/1 6, AN193, 3/3/7)

| Anforderung | Umsetzung |
|---|---|
| FDSK je Gerät; aktiv, solange kein Tool Key gesetzt ist; nach Master Reset 02h/07h wieder aktiv | NVS, Patch 21 (Stack: fest `00 01 … 0F`) |
| FDSK über keine Schnittstelle lesbar (3/5/1 6.1.3) | Kompromiss: Dashboard zeigt ihn nur, solange er Tool Key ist – das Gerät hat keinen Aufkleber |
| Echte Zufallszahlen | CTR-DRBG mit Hardware-Entropie, Patch 22 (Stack: Konstante) |
| Folgenummern steigen über Neustarts; zuletzt gültige Nummer des Tools gespeichert | NVS-Hochwassermarke, Patch 22 |
| MAC „nur Authentifizierung“ nach AN158 | Patch 22 (Stack rechnete falsch, KNXSTACK.md 14) |
| Access Policies für Dienste und Daten | `src/knx_access_policy.cpp` nach AN193, Patch 23 |
| Abgewiesen: Property-Dienste mit 0 Elementen / `AccessDenied`; `A_DeviceDescriptor_Read` mit FFFFh bzw. Typ 3Fh; Beschreibung mit genullten Feldern | Patch 23 (AN193 2.2.1, 2.2.4.5) |

### 2.4 Koppler

| Anforderung | Umsetzung |
|---|---|
| Gesicherte Telegramme unverändert weiterleiten (Filtertabelle wie üblich) | Stack routet APDU-transparent |
| Security Proxy (AN192) | **nicht umgesetzt** – optionales Profilmodul |
| **Langrahmen**: ein S-A_Data-Rahmen trägt 13 Oktette Overhead; mit Standardrahmen (15 Oktette APDU) passt praktisch kein gesichertes Management hindurch | Der TP-UART-Emulator auf dem SB-Interface kann noch keine Extended Frames (SBLib). Deshalb meldet der Router `PID_MAX_APDULENGTH_ROUTING` = 15, und Patch 25 gibt Extended Frames nicht an den Emulator weiter, sondern bestätigt sie negativ. **Folge:** Geräte hinter diesem Router lassen sich nicht sicher programmieren, bis die SBLib Extended Frames kann; dann `-DSBIP_TP_EXTENDED_FRAMES=1`. Der Router selbst ist davon nicht betroffen – er wird über IP programmiert |

### 2.5 Was die Spezifikation verlangt und hier fehlt

Das Profil „KNXnet/IP Router“ in 3/8/9 2.5.1.1 verlangt außerdem:

* **Tunnelling v2** – `TUNNELLING_FEATURE_GET/SET/INFO`. Der Stack kennt nur
  v1 und kündigt v1 an; die ETS weicht darauf aus.
* **Device Management v2** – cEMI-Transportschicht über die Device-Management-
  Verbindung. Der Stack kennt nur `M_Prop`; die ETS programmiert den Router
  über einen Tunnel mit `A_`-Diensten.
* **KNX IP System Broadcast** (`ROUTING_SYSTEM_BROADCAST`). Fehlt im Stack.

Solange das fehlt, ist das Gerät kein KNX-zertifizierbarer IP Secure Router.
Für die Inbetriebnahme mit der ETS sollte es trotzdem reichen – genau das ist
noch zu testen.

---

## 3. Architektur

```
             UDP 3671 (unicast + multicast)      TCP 3671
                   │                                │
                   ▼                                ▼
        ┌──────────────────────── SbipPlatform (src/knx_link.cpp) ─────────────┐
        │ readBytesMultiCast()  ── KnxIpShim::receive() ── KnxIpSecure            │
        │ sendBytesUniCast()    ── KnxIpShim::sendUnicast() ─ (Sessions, Routing, │
        │ sendBytesMultiCast()  ── KnxIpShim::sendMulticast()  Timer, Konfig)     │
        └──────────────────────────────┬────────────────────────────────────────┘
                                       │ nur Klartext-KNXnet/IP über „UDP“
                                       ▼
                        IpDataLinkLayer (Stack, unverändert bis auf Patches)
```

Der Stack spricht weiterhin nur ungesichertes KNXnet/IP über UDP. TCP-Rahmen
bekommen den Absender `127.77.0.<Verbindung>` und als Port 3671 (ungesichert)
oder 40001 + Session; daran erkennt der Shim, wohin und wie verschlüsselt die
Antwort des Stacks gehört.

Eingriffe im Stack, alle in `scripts/patch_knx.py`:

| Patch | Datei | Zweck |
|---|---|---|
| 19 | `ip_parameter_object.cpp` | PID 79, 91–97 als Callback-Properties (nicht im Speicherabbild), PID 94 als Funktions-Property |
| 20 | `ip_data_link_layer.cpp` | Tunnelwahl fragt, welche Slots der verbindende User bzw. das erweiterte CRI zulässt |
| 21 | `bau_systemB_coupler.cpp`, `bau091A.cpp`, `security_interface_object.cpp` | Sicherheitsobjekt nicht im Speicherabbild, `configured()` ohne Secure-Modus, Master-Reset setzt den FDSK des Geräts |
| 22 | `secure_application_layer.cpp/.h` | Zufall, Folgenummern, MAC bei reiner Authentifizierung, keine Klartext-Ausgaben |
| 23 | `application_layer.cpp`, `cemi_server.cpp` | Access Policies an den drei Indication-Verteilern und im cEMI-Server |
| 24 | `ip_parameter_object.cpp` | `PID_KNXNETIP_DEVICE_CAPABILITIES` = 0047h |
| 25 | `tpuart_data_link_layer.cpp` | keine Extended Frames an einen TP-UART, der sie nicht kann |

---

## 4. Tests

```
test/run_host_tests.sh
```

* `test/secure_crypto_test.cpp` – alle Beispiele aus Annex A von 3/8/9
  (Session Response, Session Authenticate, Wrapper in beide Richtungen,
  Routing Indication, TIMER_NOTIFY), dazu Data Secure mit und ohne
  Verschlüsselung.
* `test/access_policy_test.cpp` – AN193, vor allem: ohne Secure-Modus bleibt
  alles, wie es war, nur die Schlüssel sind geschützt.

Beim Start prüft `knxsec::selfTest()` auf dem Chip X25519 gegen RFC 7748, die
Serverseite der Beispiel-Session aus Annex A.2 (Public Key, Shared Secret,
Session Key) und CBC-MAC/CTR des Routing-Beispiels. Ergebnis im Protokoll und
auf der Karte *KNX Secure*.

---

## 5. Was sich nur mit der ETS prüfen lässt

1. **Gerätezertifikat.** Die ETS will 36 Zeichen Base32 aus Seriennummer und
   FDSK. Die Spezifikation beschreibt die Kodierung nicht (3/5/1 6.1.3: „no
   storage standardised“); ob in den vier überzähligen Bits eine Prüfsumme
   steht, ist offen. Hier sind sie 0.
2. **TCP.** Die ETS 6 baut Tunnel über TCP auf, sobald das Gerät Core 2
   ankündigt – jetzt auch ohne Secure. Bei Problemen TCP abschalten; dann
   gibt es allerdings auch keine Secure Sessions.
3. **Secure Commissioning** über den eigenen Tunnel des Routers: Sync mit dem
   FDSK, Tool Key, Secure-Modus, PID 91–97.
4. **Secure Tunnel** mit Benutzer und Passwort, Zuordnung nach PID 97.
5. **Secure Routing** gegen einen zweiten Secure-Router.
6. Verhalten der ETS gegenüber Tunnelling v1 bei einem Secure-Gerät.

### Testplan

1. Firmware flashen, Protokoll: `SECURE: crypto self test OK`,
   `KNX: KNXnet/IP over TCP on port 3671`.
2. Ohne Secure: ETS-Tunnel (jetzt TCP), Download der bisherigen
   Produktdatenbank – muss unverändert funktionieren.
3. Secure-Produktdatenbank (`make_knxprod.py --secure --app-version 3`,
   signieren), Identität im Dashboard setzen.
4. In der ETS mit Secure-Inbetriebnahme einfügen, Zertifikat aus dem
   Dashboard, Download über einen Tunnel **dieses** Routers. Danach im
   Dashboard: Data Secure aktiv, Tool-Key von der ETS, gesicherte Familien.
5. Ungesicherten Tunnel versuchen: Ablehnung mit 22h. Mit Keyring: geht.
6. Neustart, erneuter Download: keine Folgenummernfehler.
7. Mit zweitem Secure-Router: Routing in beide Richtungen.

---

## Quellen

* KNX Standard v3.0.0, Dokumente wie oben.
* xknx (MIT) und calimero – Gegenprüfung der Implementierungsdetails.
* bussard (`docs/knx-secure-spec.md`) – Mitschnitte einer echten
  ETS-Inbetriebnahme, u. a. Secured-DIB `06 06 03 01 04 01` und Ablehnung mit
  22h an einem Seriengerät.
