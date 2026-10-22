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
| 3/8/4 | 03_08_04 Tunnelling v01.07.01 AS (Tunnelling v2, AN185) |
| 3/6/3 | 03_06_03 EMI/IMI v01.04.02 AS (cEMI-Transportschicht, `M_FuncProp`) |
| 3/5/1 | 03_05_01 Resources v01.10.01 AS (FDSK: 6.1, Security-Objekt: 6.3) |
| AN193 | AN193 v04 Access Policies AS |
| AN192 | AN192 v06 Coupler security extensions AS |
| AN203 | AN203 v03 USB Local TL for Secure devices AS (Data Secure ohne Adressen) |
| TSSJ/TSSK | 08 Konformitätstests Data Security v1.11 / KNXnet/IP Security v1.1 – für den IP System Broadcast (AN183 selbst liegt dem Standard nicht bei) die maßgebliche Beschreibung |

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
| **C** | KNX Data Secure als Koppler: Durchleiten gesicherter Telegramme | umgesetzt; auf TP mit Extended Frames bis 64 Oktette (Emulator ab 0.10), sonst stark eingeschränkt, siehe 2.4 |
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
| Security Proxy (AN192) | umgesetzt, siehe 2.8 |
| **Langrahmen**: ein S-A_Data-Rahmen trägt 13 Oktette Overhead; mit Standardrahmen (15 Oktette APDU) passt praktisch kein gesichertes Management hindurch | Der TP-UART-Emulator ab 0.10 sendet Extended Frames bis 64 Oktette und meldet das per `U_EmuIdentify`. `KnxLink::applyTransceiver()` übernimmt den Wert nach jeder Erkennung, der Router meldet dann `PID_MAX_APDULENGTH_ROUTING` = 55, und Patch 25 bestätigt nur längere Rahmen negativ. Ohne diese Angabe (älterer Emulator, echter TP-UART ohne `-DSBIP_TP_EXTENDED_FRAMES=1`) bleibt es bei 15. **Folge:** Mit dem Emulator ab 0.10 lassen sich Geräte hinter diesem Router sicher programmieren, sonst nicht. Der Router selbst ist davon nicht betroffen – er wird über IP programmiert |

### 2.5 Tunnelling v2 (3/8/4 3, 5.4.3; TSSK 2.7)

| Anforderung | Umsetzung |
|---|---|
| `TUNNELLING_FEATURE_GET/SET` mit denselben Sequenzzählern und Quittungen wie `TUNNELLING_REQUEST`, Antwort `TUNNELLING_FEATURE_RESPONSE` | Patch 26 im Stack, wo die Zähler liegen |
| Features 01 EMI (0004h), 02 Maske, 03 Busverbindung, 04 Hersteller, 05 aktives EMI (03h), 06 Tunneladresse, 07 max. APDU, 08 INFO an/aus | `tunnelFeature()` in `src/knx_link.cpp`; 08 je Verbindung, bei jedem Verbindungsaufbau aus |
| Rückgabecodes `E_ACCESS_READ_ONLY`, `E_DATA_TYPE_CONFLICT`, `E_DATA_VOID`, `E_ADDRESS_VOID` (ohne Wert) | ja |
| 06 schreibbar nur ohne Security-Modus; nicht eigene, nicht aus PID 53, nicht belegt; dauerhaft (PID 53 des Slots, Abbild gespeichert); `TUNNELLING_FEATURE_INFO` im Moment der Wirkung | ja |
| `TUNNELLING_FEATURE_INFO` für die Busverbindung bei jeder Änderung | `superviseTpLink()` |
| Tunnelling-Info-DIB auch in der Beschreibungsantwort; max. APDU = `PID_MAX_INTERFACE_APDU_LENGTH` | Shim; PID 68/69 im cEMI-Server-Objekt (flüchtig, Patch 26) |
| Fehlerhafte CRI ohne Antwort verwerfen | ja |
| Benutzer ohne Eintrag in PID 97 → `E_CONNECTION_TYPE`; Raw-Modus bei Security-Modus → `E_CONNECTION_OPTION` | ja |
| `E_NO_MORE_UNIQUE_CONNECTIONS`, wenn nur noch doppelte Adressen frei wären | ja; außerdem überspringt die Slotwahl belegte Adressen (der Stack gab sonst auf) |

### 2.6 KNX IP System Broadcast (3/5/1 4.5.16, 3/3/7 5.2, TSSK 2.4)

| Anforderung | Umsetzung |
|---|---|
| `ROUTING_SYSTEM_BROADCAST` (0533h) nur auf 224.0.23.12, nie gewrappt – auch nicht bei gesichertem Routing | Shim; empfangen nur, solange die Routing-Multicast-Adresse 224.0.23.12 ist (der Socket kennt die Zielgruppe nicht) |
| cEMI: L_Data.ind, SB = 0, Gruppenadresse 0 | ja; Routing-Indications mit SB = 0 werden verworfen |
| `PID_IP_SBC_CONTROL` (Router-Objekt, PID 120): Funktionsformat, Rückfall nach 20 s, F2h/F3h/F8h, Access Policy 3FF/00C | Patch 27, `ipSbcFunction()`; F3h, wenn der Koppler Broadcasts sperrt |
| TP → IP bei eingeschaltetem Modus: Seriennummer lesen (Programmiermodus), Domain-Adresse 4 Oktette, S-A_Data/Sync-Request mit SBC = 1 und T = 1 – als System Broadcast statt Routing-Indication | Shim |
| IP → TP bei eingeschaltetem Modus: nur Seriennummer-Antwort und Sync-Response (SBC, T) | Hook im Koppler (Patch 27) |
| Eigenes Management, unabhängig vom Modus: Seriennummer im Programmiermodus, Domain-Adresse schreiben, Data Secure mit SBC; Antwort als System Broadcast | Shim + Stack |
| Domain-Adresse: 4 Oktette ungesichert nur ohne Security-Modus; gesichert nur mit Tool-Key und Verschlüsselung; 4 Oktette schaltet gesichertes Routing ab, 21 Oktette (Version 01h + Backbone-Key) schaltet es und den Security-Modus ein; danach Neustart | `applyDomainAddress()` |
| Routing-Version 2 in der Liste der Dienstfamilien | ja |

Der Stack leitete System-Broadcast-Dienste im Koppler überhaupt nicht weiter
(Befund 18 in [KNXSTACK.md](KNXSTACK.md)); Patch 27 behebt das mit.

### 2.7 Device Management v2 (3/8/3 2.6, 3/6/3 4.1.6/4.1.7)

| Anforderung | Umsetzung |
|---|---|
| `T_Data_Connected.req`/`T_Data_Individual.req` über die Device-Management-Verbindung an die eigene Anwendungsschicht, Antwort als `.ind` | Patch 28: lokaler Modus der Transportschicht |
| `T_Connect.ind`/`T_Disconnect.ind` mit der Verbindung | beim ersten Transportrahmen bzw. beim Ende der Verbindung – eine Verbindung, über die die ETS nur Properties liest, sperrt so nicht den `T_Connect` über ihren Tunnel |
| Data Secure ohne Adressen und Transport-Sequenznummer in B0 (AN158, AN203) | Patch 28 in der Secure Application Layer |
| `M_FuncPropCommand.req`, `M_FuncPropStateRead.req` (auch für v1 Pflicht; der Stack meldete „not implemented“) | `cemiService()`; ohne Funktions-Property ohne Rückgabecode, abgewiesen mit 0Bh |

### 2.8 Security Proxy (AN192 v06)

Eine Gruppenadresse kann auf einer Seite des Routers mit Data Secure
gesichert und auf der anderen ungesichert sein. Der Proxy übersetzt dann
zwischen beiden; Punkt-zu-Punkt- und Broadcast-Telegramme kann die ETS auf
eine Seite sperren und sich selbst für jeweils 60 s freigeben. Primär ist IP,
sekundär TP. Logik in `src/knx_proxy_core.cpp` (ohne Stack, Host-Test),
Anbindung in `src/knx_security_proxy.cpp`, Öffnungen im Stack: Patch 31.

| Anforderung | Abschnitt | Umsetzung |
|---|---|---|
| PID 80 `PID_PTP_CONFIG`, 82 `PID_BC_CONFIG` je Seite; Vorgabe RULE, nichtflüchtig, unabhängig vom Ladezustand | 2.3.1.1, 2.3.1.2 | primär im Router-Objekt des Stacks (Callback-Properties, Patch 31), sekundär im zweiten Router-Objekt; NVS `sbip-proxy` |
| Zweites Router-Objekt bei Maske 091A, nur mit den Proxy-Properties der Sekundärseite | 2.3.1 | `SbipBau`, hinter den Objekten des Stacks (Index 6, PID_IO_LIST Element 7, Instanz 2) – kein bekannter Index verschiebt sich |
| PID 81 `PID_SECURITY_PROXY_GRP_KEY_TABLE`, GA + Schlüssel + Einstellungen, 19 Oktette | 2.3.1.5 | 128 Einträge; wirkt nur, solange das Router-Objekt geladen ist |
| Tabelle 8: entschlüsseln, verschlüsseln, durchleiten oder sperren; gleiche Einstellung auf beiden Seiten sperrt | 2.2.2.3, 2.3.1.5.6 | `knxproxy::groupAction()` |
| SAI muss zu den Security Flags passen; Folgenummer > letzte gültige aus der Sicherheits-IA-Tabelle, danach fortschreiben | 2.2.2.3, 2.3.3.1 | Absender ohne Eintrag in PID 54 wird gesperrt – ohne letzte gültige Nummer wäre jede Aufnahme wiederholbar |
| Verschlüsselt mit der eigenen Adresse und der eigenen Folgenummer | 2.2.2.3 | ein Sendezähler mit der Secure Application Layer (Patch 31) |
| S-A_Sync über Gruppenadressen nie weiterleiten | 2.2.2.3, 2.2.2.4 | ja, auch ohne Tabelle |
| Punkt-zu-Punkt: BLOCK lässt nur durch, was eine Temporary Unicast White List erlaubt | 2.2.2.5 | 8 Listen mit je 8 Bereichen, unabhängig von der Richtung |
| PID 83 `PID_PTP_ROUTING_TEMP`: Hinzufügen, Widerrufen, Nachtriggern, Prüfen; 60 s; Fehlercodes | 2.3.1.3 | nur primär; Prüfung bereichsweise, nicht Adresse für Adresse |
| (System-)Broadcast: BC_BLOCK lässt nur durch, was PID 84 auf Zeit freigibt | 2.2.2.6 | UND-verknüpft mit `PID_IP_SBC_CONTROL` und dessen Regeln (2.6) |
| PID 84 `PID_BC_ROUTING_TEMP`: BC- und SBC-Modus, ein Timer je Objekt; SBC-Modus auf TP1 unzulässig | 2.3.1.4, 2.7.2.1 | ja; auf TP läuft ein System Broadcast als Broadcast und fällt unter den BC-Modus |
| Master Reset nach den Tabellen der Properties | je Property | `Proxy::masterReset()`; „Secure-Konfiguration löschen“ nimmt die Schlüssel mit |
| Zugriff 80/82 3FF/00C, 81 00C/00C, 83/84 3FF/0CC | 2.1.1 | `knx_access_policy.cpp`; 2.1.1 nennt für 80/82 0CC, die Property-Definitionen 00C – genommen ist die strengere |

**Tunnel gehören zu TP.** Ihre Telegramme erreichen die Linie, ohne den
Koppler zu durchlaufen; ihre Adressen gehören zur TP-Linie. Ein ungesicherter
Tunnel-Client auf einer Adresse, die auf TP gesichert ist, wird deshalb nicht
übersetzt – wie ein ungesichertes TP-Gerät auch.

**Letzte gültige Folgenummern.** Sie stehen im Sicherheitsobjekt, das nur mit
dem Speicherabbild gespeichert wird. Der Proxy schreibt sie mit jedem
entschlüsselten Telegramm fort, und zwar sofort in eine Sicherung
(`src/knx_seq_backup.cpp`), die beim Start zurückgespielt wird – die größere
Nummer gewinnt:

| Sicherung | wann | übersteht | NVS |
|---|---|---|---|
| FRAM an I2C 0x50 | Fujitsu MB85RC ab 64 kbit per Device-ID erkannt, oder `fram_fitted` im Hardware-Profil | alles | nicht mehr nötig |
| RTC-RAM des ESP32 | sonst | Neustart, Watchdog, Absturz – keinen Stromausfall | höchstens alle 5 min |

Ohne FRAM ließe sich nach einem Stromausfall ein Telegramm aus den Minuten
davor einmal wiederholen. Ein EEPROM an 0x50 wird nie benutzt: Ohne Device-ID
und ohne Eintrag im Profil bleibt es beim RTC-RAM. Der Flash selbst ist kein
Engpass – das Sicherheitsobjekt ist bei diesem Koppler rund 350 Byte groß,
alle 5 min gespeichert reicht der 20-KiB-NVS-Bereich rechnerisch Jahrzehnte.

**Länge.** Ein gesichertes Gruppentelegramm ist 13 Oktette länger. Ab vier
Oktetten APDU (z. B. DPT 9) braucht es auf TP einen Langrahmen, also den
Emulator ab 0.10 (2.4); sonst lehnt Patch 25 den Rahmen ab.

**ETS.** Ob die ETS den Router als Proxy anbietet, entscheidet die
Produktdatenbank (AN192 3.5.1). Im Schema der ETS 6 (`project/23`) heißen die
Angaben:

* `CouplerCapabilities="SecurityProxy"` am `Hardware2Program`,
* `MaxSecurityProxyGroupKeyTableEntries` am `ApplicationProgram`,
  daneben wie bisher `IsSecureEnabled` und `MaxSecurityIndividualAddressEntries`.

PID 80 – 84 stehen nicht in den Ladeprozeduren der Masken 0912 und 091A in
`knx_master.xml` – die ETS schreibt sie selbst, wenn die Fähigkeit angesagt
ist; nach AN192 2.5.2 mit dem Download der Filtertabelle. Wie sie das zweite
Router-Objekt anspricht (Objektindex oder Typ und Instanz), ist offen.

`make_knxprod.py --proxy` setzt `CouplerCapabilities="SecurityProxy"`
(Segmentkoppler ist dieser Router nicht), 128 Gruppenschlüssel,
`MinEtsVersion="6.0"` und das Schema `project/23`; `--secure` ist
eingeschlossen. Der Rest der Datenbank stammt aus `project/14` – ob
OpenKNXproducer und die ETS ihn unter dem neuen Schema annehmen, zeigt erst
das Signieren.

**Sicherheits-IA-Tabelle.** Sie muss jeden gesicherten Absender auf einer
übersetzten Gruppenadresse fassen. Der Stack hat 32 Einträge. Reicht das
nicht, ist die Größe in `security_interface_object.cpp` (Patch)
und `SECURITY_IA_ENTRIES` in `make_knxprod.py` gemeinsam zu erhöhen; das
Objekt liegt in NVS, das Speicherabbild bleibt davon unberührt.

**Nicht umgesetzt:** die Security Proxy Zone Key Table (LTE, Objekttyp 7) –
der Stack kennt keinen LTE-Modus, und die ETS füllt sie nicht (3.5.2.1); und
die Segmentkoppler-Funktion (AN195), die 2.7 als Voraussetzung nennt. Sie
betrifft TP/TP-Koppler, nicht diesen IP-Router.

### 2.9 Was fehlt

* **Security Proxy:** Zonenschlüsseltabelle (LTE); die Datenbank mit
  `--proxy` ist noch nicht signiert und nicht mit der ETS geprüft, siehe 2.8.
* **Extended Frames über 64 Oktette auf TP** – der Emulator begrenzt auf 64, siehe 2.4.
* **IP System Broadcast bei anderer Routing-Multicast-Adresse** als
  224.0.23.12: dazu müsste der Socket die Zielgruppe eines Pakets kennen
  (`IP_PKTINFO`), was das Arduino-lwIP nicht liefert.
* **Wiederholung** von `TUNNELLING_FEATURE_RESPONSE`/`_INFO` über UDP ohne
  Quittung – wie bei den Tunnelrahmen des Stacks.

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
| 25 | `tpuart_data_link_layer.cpp` | kein Rahmen an den TP-UART, der länger ist, als er senden kann |
| 26 | `ip_data_link_layer.cpp/.h`, `cemi_server_object.cpp` | Tunnelling-v2-Feature-Dienste, Abfragen für Slots und Device-Management-Verbindung, PID 68/69 im cEMI-Server-Objekt |
| 27 | `router_object.cpp`, `network_layer_coupler.cpp`, `application_layer.cpp` | `PID_IP_SBC_CONTROL`, System-Broadcast-Dienste über den Koppler, Routing-Freigabe IP → TP, Länge der Domain-Adresse, gesicherte Domain-Adresse an die BAU |
| 28 | `transport_layer.cpp/.h`, `secure_application_layer.cpp`, `cemi_server.cpp` | lokaler Modus der Transportschicht (cEMI), B0 ohne Adressen, Funktions-Properties und Transportrahmen über cEMI |
| 31 | `router_object.cpp`, `network_layer_coupler.cpp`, `secure_application_layer.cpp/.h` | Security Proxy: PID 80 – 84 im Router-Objekt, letzte Entscheidung über jeden gerouteten Rahmen, gemeinsamer Sendezähler |

---

## 4. Tests

```
test/run_host_tests.sh
```

* `test/secure_crypto_test.cpp` – alle Beispiele aus Annex A von 3/8/9
  (Session Response, Session Authenticate, Wrapper in beide Richtungen,
  Routing Indication, TIMER_NOTIFY), dazu Data Secure mit und ohne
  Verschlüsselung.
* `test/security_proxy_test.cpp` – AN192: Rahmen gegen den Gruppenvektor von
  bussard in beiden Richtungen, Tabelle 8, Gruppenschlüsseltabelle, das
  Beispiel 3 zu den White Lists, PID 83/84 und Master Reset.
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
6. **Tunnelling v2, Device Management v2, Routing v2**: Das Gerät kündigt
   jetzt alle drei an. Programmiert die ETS den Router damit über die
   Device-Management-Verbindung (cEMI-Transportschicht) statt über ihren
   Tunnel, ist das der erste Praxistest dieses Wegs.
7. **IP System Broadcast**: Seriennummer lesen und Domain-Adresse schreiben
   für KNX-IP-Geräte hinter bzw. vor dem Router.
8. **Security Proxy**: ob die ETS PID 81 – 84 so schreibt und liest, wie
   AN192 es beschreibt; ob sie die Absender in die Sicherheits-IA-Tabelle des
   Proxys einträgt (ohne Eintrag sperrt der Proxy, 2.8); ob sie die
   Antwort auf das Schreiben der Schlüsseltabelle prüft. Und zuerst: ob sie
   eine mit `make_knxprod.py --proxy` gebaute Datenbank signiert annimmt und
   den Router dann als Proxy führt.

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

## 6. Schlüssel im Flash

FDSK, Tool-Key, Backbone-Key, Passwort-Hashes und die Gruppen- und
P2P-Schlüssel liegen im NVS bzw. im Speicherabbild – im Klartext, wer den
Flash ausliest, hat sie. Die Krypto-Hardware des ESP32 hilft dagegen nicht:
sie beschleunigt AES, SHA und Bignum (mbedTLS nutzt sie für IP Secure, der
Stack für Data Secure seine eigene Software-AES), hält aber keine Schlüssel
fest. Was schützt, sind **Flash Encryption** mit **NVS Encryption** (Schlüssel
in eFuses, bei S2/S3/C3/C6 über das HMAC-Peripheral nicht auslesbar) und
**Secure Boot v2** gegen fremde Firmware. Beides brennt eFuses unumkehrbar und
braucht einen eigenen Bootloader (pioarduino mit `custom_sdkconfig`); es ist
deshalb nicht Teil dieses Zweigs. Ein externes Secure Element (ATECC608)
bringt wenig: es kann weder X25519 noch die vielen Data-Secure-Schlüssel
halten, und die Firmware bräuchte die Schlüssel für CCM doch im RAM.

---

## Quellen

* KNX Standard v3.0.0, Dokumente wie oben.
* xknx (MIT) und calimero – Gegenprüfung der Implementierungsdetails.
* bussard (`docs/knx-secure-spec.md`) – Mitschnitte einer echten
  ETS-Inbetriebnahme, u. a. Secured-DIB `06 06 03 01 04 01` und Ablehnung mit
  22h an einem Seriengerät.
