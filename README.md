# Selfbus KNXnet/IP Interface (ESP32)

KNXnet/IP-Interface aus einem ESP32 und einem **Selfbus SB-Interface
(LPC1115)**, das die [TP-UART-2-Emulation](../TPUART2-Emu) ausführt.

Konzeptionell an [ip4knx](https://github.com/tostmann/ip4knx) orientiert, aber
neu geschrieben und modularisiert. ip4knx zielt auf die Busware-TUL-Sticks mit
NCN5130-Transceiver; hier sitzt stattdessen ein Selfbus-Interface am UART.

```
   KNX TP1  ──  SB-Interface (LPC1115)  ──UART 19200 8E1──  ESP32  ──WLAN──  ETS / HA / knxd
                TP-UART-2-Emulation                        KNXnet/IP
```

---

## Funktionsumfang

| | |
|---|---|
| KNXnet/IP Routing | Multicast, Auto-Discovery in Home Assistant |
| KNXnet/IP Tunneling | 10 gleichzeitige Verbindungen (ETS, HA, Node-RED …) |
| **KNX-Zeitserver** | DPT 19.001 / 10.001 / 11.001, NTP + optionaler RV-3028-C7 || **Ethernet (optional)** | W5500 über SPI, beim Start automatisch erkannt || Web-Dashboard | Status, Buslast, Telegrammzähler, Systeminfo |
| WLAN-Einrichtung | Captive Portal (offener AP) **oder** Improv über USB |
| Programmiermodus | per Klick im Dashboard, kein Tastendruck am Gerät nötig |
| OTA-Update | Datei-Upload **und** Online-Pull aus einem Manifest |
| **LPC-Programmierung** | SB-Interface über zwei GPIO erkennen und flashen |
| Anti-Brick | zwei App-Partitionen + Bootloader-Rollback |
| WLAN-Watchdog | erkennt stille Abbrüche und verlorene DHCP-Leases |
| TP-Link-Watchdog | erneuert den Reset-Handshake, wenn das SB-Interface stumm wird |
| Auslastung | TP1-Buslast, Kern-0-Last und Hauptschleifen-Last, mit Spitzenwertmarker |
| Betriebsstunden | zählen über Neustarts und Stromausfälle hinweg weiter |
| Busmonitor | Telegramme beider Seiten im PSRAM, mit Filter und Trigger |
| mDNS | `http://sbip.local` |
| CSRF-Schutz | Origin-Prüfung auf allen schreibenden Endpunkten |

---

## Unterstützte ESP32-Varianten

| Chip | Env | Anmerkung |
|---|---|---|
| ESP32 (WROOM/WROVER) | `esp32dev` | Dual-Core, 3 UARTs |
| ESP32-S3 | `esp32s3` | Dual-Core, viel RAM – die komfortabelste Wahl |
| ESP32-C3 | `esp32c3` | Single-Core RISC-V, ausreichend |
| ESP32-C6 | `esp32c6` | Single-Core RISC-V, WiFi 6 |
| ESP32-S2 | `esp32s2` | funktioniert, Single-Core, kein BLE |
| ESP32-H2 | — | **nicht möglich**, kein WLAN (nur 802.15.4 + BLE) |
| ESP8266 | — | nicht unterstützt (RAM, UART-Anzahl) |

Die Bindung entsteht über `ARDUINO_ARCH_ESP32` in `esp32_platform.h` des
KNX-Stacks – grundsätzlich läuft jedes Arduino-ESP32-Ziel mit WLAN.

**Anforderungen:** ein freier Hardware-UART für KNX (getrennt vom Debug-/USB-
Port), WLAN, ≥ 4 MB Flash für das Dual-OTA-Layout (siehe *Flash-Größe und
Partitionslayout*).

**Zur Kernanzahl:** Auf Single-Core-Varianten teilen sich WLAN-Stack und
`loop()` einen Kern. Das ist unkritisch, weil das SB-Interface das zeitkritische
TP1-Bit-Timing und die ACK-Erzeugung selbst übernimmt – genau der Vorteil dieser
Architektur gegenüber einem direkt angebundenen Transceiver. Der ESP32 sieht nur
noch einen 19200-Baud-Bytestrom.

---

## Hardware

### Verdrahtung

| SB-Interface (LPC1115) | ESP32 |
| --- | --- |
| `PIN_TX` (PIO1_7) | RX (`SBIP_KNX_RX_PIN`) |
| `PIN_RX` (PIO1_6) | TX (`SBIP_KNX_TX_PIN`) |
| `/RESET` (PIO0_0) | `SBIP_LPC_RESET_PIN`, optional |
| `PIO0_1` | `SBIP_LPC_ISP_PIN`, optional |
| GND | GND |

Beide Seiten 3,3 V TTL, kein Pegelwandler. **19200 Baud, 8E1.**

Die beiden letzten Leitungen sind nur für das Programmieren des LPC nötig,
siehe *SB-Interface programmieren*. Ohne sie läuft alles andere unverändert.

Die Stromversorgung des ESP32 sollte **nicht** aus dem SB-Interface kommen. Das
KNX-Busnetzteil des Selfbus-Interfaces ist für die WLAN-Stromspitzen (bis
~350 mA beim RF-Init) nicht ausgelegt. ip4knx umgeht das auf der bus-powered
TULX32 mit deaktiviertem Brownout-Detector, reduziertem CPU-Takt und 1,5 s
Kondensator-Ladezeit – diese Krücken sind hier bewusst nicht übernommen.

### Pin-Belegung der vorkonfigurierten Boards

| Env | Board | KNX-UART | RX / TX | LED | Taster |
| --- | --- | --- | --- | --- | --- |
| `esp32dev` | ESP32-WROOM | UART2 | 16 / 17 | 2 | 0 |
| `esp32s3` | ESP32-S3-DevKitC | UART1 | 18 / 17 | 48 (SK6812) | 0 |
| `esp32c3` | XIAO ESP32-C3 | UART1 | 20 / 21 | 4 (low-aktiv) | 9 |
| `esp32c6` | ESP32-C6-DevKitC | UART1 | 5 / 4 | – | 9 |
| `esp32s2` | ESP32-S2-Saola | UART1 | 18 / 17 | 15 | 0 |

Auf dem **S3-** und dem **C6-DevKitC** gibt es keine einfache LED, sondern
eine **SK68/WS2812-RGB-LED**. Die braucht ein getaktetes serielles Protokoll;
`digitalWrite()` bleibt dort ohne sichtbare Wirkung. Das Profil des S3 legt
sie deshalb als adressierbare LED an, siehe *Taster und LEDs*.

> **Vorsicht beim S3-DevKitC-1: der LED-Pin hängt an der Board-Revision.**
> **v1.0 treibt sie über GPIO 48, v1.1 über GPIO 38** – beide Versionen sind
> im Handel, und Espressif nennt genau das als einzigen Unterschied. Der
> Arduino-Variantenheader kennt nur GPIO 48, weshalb auch `rgbLedWrite()` auf
> v1.1-Boards wirkungslos bleibt. Die Vorgabe hier ist 48; bleibt die LED
> dunkel, im Dashboard auf 38 umstellen – ohne Neubau.

Beim XIAO C3 ist GPIO 4 herausgeführt, aber unbestückt – dort gehört eine
eigene LED mit Vorwiderstand hin.

I2C für die optionale RTC: `esp32dev` 21/22, `esp32s2`/`esp32s3` 8/9,
`esp32c3`/`esp32c6` 6/7.

SPI für den optionalen W5500: `esp32dev` 18/19/23, CS 5; `esp32s2`/`esp32s3`
12/13/11, CS 10. Bei `esp32c3`/`esp32c6` nicht belegt – siehe *Ethernet*.

Anpassung über die `-DSBIP_*`-Flags in [platformio.ini](platformio.ini).

> **Wichtig:** Diese Flags sind nur noch **Vorgabewerte**. Was das Gerät
> tatsächlich verwendet, steht in NVS und ist über das Dashboard änderbar –
> siehe *Hardware-Profil*. Ein Image läuft damit auf mehreren Boards.

---

## Hardware-Profil

Pinbelegung, UART-Nummer und Bestückungsoptionen sind zur Laufzeit
konfigurierbar. Das Dashboard bietet ein Formular, JSON-Upload und -Download.

### Was konfigurierbar ist

| Feld | Bedeutung |
| --- | --- |
| `knx_uart`, `knx_rx`, `knx_tx` | KNX-Anbindung zum SB-Interface |
| `lpc_reset`, `lpc_isp`, `lpc_invert` | Steuerleitungen zum LPC, siehe *SB-Interface programmieren* |
| `buttons[]`, `leds[]` | vorhandene Taster und LEDs, siehe *Taster und LEDs* |
| `button_assign[]`, `led_assign[]` | wozu sie dienen |
| `i2c_enabled`, `i2c_sda`, `i2c_scl` | RV-3028-C7 |
| `eth_enabled`, `eth_sck`, `eth_miso`, `eth_mosi`, `eth_cs`, `eth_irq`, `eth_rst`, `eth_spi_mhz` | W5500 |
| `log_kib`, `monitor_kib` | PSRAM für Protokoll und Busmonitor, siehe *PSRAM aufteilen* |

`-1` bedeutet bei den Peripherie-Pins „nicht bestückt". Ein hochgeladenes JSON
darf Teilmengen enthalten – fehlende Felder behalten ihren Wert. Eine
**angegebene Liste ersetzt** die bisherige vollständig; nur so lässt sich die
letzte Zeile überhaupt löschen.

Beispiel:

```json
{
  "knx_uart": 1,
  "knx_rx": 18,
  "knx_tx": 17,
  "lpc_reset": 4,
  "lpc_isp": 5,
  "lpc_invert": false,
  "buttons": [
    { "name": "prog",  "pin": 0, "trigger": 0 },
    { "name": "setup", "pin": 0, "trigger": 1 }
  ],
  "leds": [
    { "name": "rgb", "pin": 48, "kind": 1, "rgb_type": 1, "rgb_index": 0 }
  ],
  "button_assign": [
    { "target": "prog",  "function": 0 },
    { "target": "setup", "function": 2 }
  ],
  "led_assign": [
    { "target": "rgb", "condition": 0, "colour": 0, "pattern": 1 },
    { "target": "rgb", "condition": 3, "colour": 1, "pattern": 0 },
    { "target": "rgb", "condition": 5, "colour": 6, "pattern": 4 }
  ],
  "i2c_enabled": true,
  "i2c_sda": 8,
  "i2c_scl": 9,
  "eth_enabled": true,
  "eth_sck": 12, "eth_miso": 13, "eth_mosi": 11, "eth_cs": 10,
  "eth_irq": -1, "eth_rst": -1, "eth_spi_mhz": 20
}
```

---

## Taster und LEDs

Beide sind Listen mit höchstens **acht** Zeilen. Jede Zeile trägt einen Namen,
über den eine zweite Liste ihr eine Funktion zuordnet. Die Zuordnung geht über
den **Namen**, nicht über die Zeilennummer – Umsortieren im Dashboard kann
deshalb nicht versehentlich auf andere Hardware zeigen.

Namen dürfen 1 bis 16 Zeichen aus `A-Z a-z 0-9 _ -` enthalten. Die Beschränkung
ist nicht kosmetisch: Namen landen in JSON und im Dashboard, und ein
Anführungszeichen an der falschen Stelle wäre dort ein Loch.

### Taster

| Feld | Werte |
| --- | --- |
| `name` | 1–16 Zeichen |
| `pin` | GPIO, muss als Eingang taugen |
| `trigger` | `0` kurz, `1` lang, `2` sehr lang |

Kurz ist ein Druck zwischen 40 ms und einer Sekunde und wird beim **Loslassen**
ausgewertet. Lang ab 2 s, sehr lang ab 6 s – beide feuern, **während** der
Taster noch gehalten wird. Das ist Absicht: Wer Werkeinstellungen auslöst,
soll es merken, bevor er loslässt.

Derselbe GPIO darf mehrfach vorkommen, solange sich die Auslösung
unterscheidet. Genau so ist die Vorgabe gebaut – ein Taster, kurz für den
Programmiermodus, lang für die WLAN-Einrichtung.

**Funktionen** (`function` in `button_assign`):

| Wert | Funktion | Wirkung |
| --- | --- | --- |
| 0 | Programmiermodus | schaltet den KNX-Programmiermodus um |
| 1 | Werkeinstellungen | löscht **die gesamte** NVS-Partition und startet neu |
| 2 | WLAN Grundeinstellung | öffnet den Provisioning-Accesspoint |
| 3 | Gerät neu starten | Neustart |
| 4 | WLAN ein/aus | schaltet das Funkmodul um, wirkt nach dem Neustart |

> **Vorsicht bei 1.** Werkeinstellungen löscht auch die WLAN-Zugangsdaten und
> das Hardware-Profil. Das braucht physischen Zugang, was die Absicherung ist.

**WLAN abschalten geht nur mit Ethernet-Hardware.** Voraussetzung ist, dass
der W5500 beim Start geantwortet hat – ein Kabel oder eine IP-Adresse ist
**nicht** nötig. Ein gesteckter, aber nicht verbundener Chip lässt einen Weg
zurück offen; gar kein Chip nicht.

Das wird an zwei Stellen geprüft: Der Taster verweigert das Abschalten, und
`NetManager::begin()` schaltet das Funkmodul beim Start wieder ein, wenn kein
W5500 antwortet. Damit sperrt auch ein ausgebautes Ethernet-Board niemanden
aus – das Gerät kommt beim nächsten Start von selbst mit WLAN hoch.

### LEDs

| Feld | Werte |
| --- | --- |
| `name` | 1–16 Zeichen |
| `pin` | GPIO, muss als Ausgang taugen |
| `kind` | `0` einfache LED, `1` adressierbare LED |
| `active_low` | nur bei `kind` 0 |
| `rgb_type` | nur bei `kind` 1: `0` WS2812, `1` SK6812 |
| `rgb_index` | nur bei `kind` 1: Position in der Kette, 0–63 |

> Im Dashboard wird die Position **ab 1** angezeigt – „die erste LED ist die
> 1" ist die natürlichere Zählung. Im JSON bleibt sie **ab 0**, weil sie dort
> ein Feldindex ist. Die Oberfläche rechnet um.

Eine adressierbare LED ist **eine Position in einer Kette**. Zeilen mit
demselben GPIO beschreiben dieselbe physische Leitung: Sie müssen denselben
`rgb_type` und verschiedene `rgb_index` haben. Die Kettenlänge ergibt sich aus
dem größten verwendeten Index – es gibt kein separates Anzahlfeld.

WS2812 und SK6812 sind weitgehend austauschbar: beide erwarten drei Bytes in
der Reihenfolge Grün, Rot, Blau bei 1,25 µs pro Bit. Unterschiedlich sind nur
die High-Zeiten (WS2812 0,4/0,8 µs, SK6812 0,3/0,6 µs). Falsch gewählt läuft es
meist trotzdem, nur mit weniger Reserve.

**Funktionen** (`function` in `led_assign`):

**Zuordnung** (`led_assign`): Jede Zeile verbindet eine LED mit **einem
Zustand**, einer Farbe und einem Muster. Dieselbe LED darf beliebig oft
vorkommen – genau darum geht es.

| Feld | Werte |
| --- | --- |
| `target` | Name aus `leds` |
| `condition` | `0` Programmiermodus aktiv, `1` AP-Modus offen, `2` keine TP-Verbindung, `3` online, `4` offline, `5` Heartbeat, `6` GA-Filter deaktiviert |
| `colour` | `0` rot, `1` grün, `2` blau, `3` gelb, `4` cyan, `5` magenta, `6` weiß, `7` orange |
| `pattern` | `0` Dauerlicht, `1` langsam blinken (1 Hz), `2` schnell blinken (5 Hz), `3` Doppelblitz, `4` kurzer Blitz alle 2 s |

**Die Reihenfolge ist die Rangfolge.** Pro LED gilt die oberste Zeile, deren
Zustand gerade zutrifft; alles darunter ist für diese LED verdeckt. Im
Dashboard verschieben ↑ und ↓ die markierte Zeile. Damit zeigt eine
**einzelne** LED nacheinander alles, was gerade wichtig ist – der
Programmiermodus verdeckt den Netzzustand, dieser den Herzschlag.

Die Vorgabe legt genau das an:

| # | Zustand | Farbe | Muster |
| --- | --- | --- | --- |
| 1 | Programmiermodus aktiv | rot | langsam blinken |
| 2 | AP-Modus offen | blau | Doppelblitz |
| 3 | keine TP-Verbindung | gelb | schnell blinken |
| 4 | online | grün | Dauerlicht |
| 5 | Herzschlag | weiß | kurzer Blitz |

Höchstens **zwölf** Zeilen. Derselbe Zustand zweimal auf derselben LED wird
abgelehnt – die untere Zeile wäre unerreichbar.

Die **Farbe** wirkt nur bei adressierbaren LEDs; eine einfache LED
unterscheidet die Zustände allein am Muster. Statt eines freien Farbwerts gibt
es eine feste Palette: Die Helligkeit muss ohnehin gedeckelt werden, und mehr
als eine Handvoll klar unterscheidbarer Farben kann eine 5-mm-Anzeige nicht
transportieren.

Der **Herzschlag** ist der einzige Zustand mit einem Schalter auf der
Startseite; er ist standardmäßig **aus**. Wie er aussieht, steht trotzdem im
Profil.

### Änderungen brauchen einen Neustart – meistens

Peripherie lässt sich nicht verschieben, während sie läuft. Pins, Chiptypen
und Kettenpositionen werden deshalb **genau einmal** beim Start gelesen; das
Dashboard bietet nach dem Speichern einen Neustart an.

Ändert sich nur die **Zuordnung**, wirkt das sofort. Welchen Zustand eine LED
anzeigt und was ein Taster auslöst, wird bei jedem Durchlauf frisch
nachgeschlagen – eine andere Farbe kostet also keinen Neustart. Ob das
zutrifft, entscheidet `HwConfig::sameWiring()`; die Antwort steht als
`reboot_required` in der Antwort auf den POST.

### Warum das Gerät dabei nicht unbrauchbar wird

Eine falsche Pin-Angabe kann das Gerät im Prinzip lahmlegen – etwa ein Pin am
SPI-Flash. Drei unabhängige Sicherungen verhindern das:

**1. Prüfung vor dem Speichern.** [src/hw_config.cpp](src/hw_config.cpp) lehnt
ab:

* GPIOs, die es auf diesem Chip nicht gibt (`GPIO_IS_VALID_GPIO`)
* Eingangs-only-Pins dort, wo ein Ausgang nötig ist (`GPIO_IS_VALID_OUTPUT_GPIO`)
* **Pins am SPI-Flash** – chipabhängig: ESP32 6–11, S2/S3 26–32, C3 11–17,
  C6 24–30. Diese liegen *innerhalb* der gültigen GPIO-Maske, werden also von
  den ESP-IDF-Makros nicht erfasst; ein Zugriff killt die laufende Firmware
  sofort. Deshalb explizit geprüft.
* doppelt belegte Pins – immer ein Fehler, und ein besonders unangenehmer:
  Zwei Peripherien am selben Pin erzeugen Symptome weit weg von der Ursache.
  Ausgenommen sind zwei Fälle, in denen die Doppelung Absicht ist: mehrere
  Tasterzeilen mit **verschiedener Auslösung** und mehrere LED-Zeilen auf
  **derselben Kette**.
* Namen außerhalb `A-Z a-z 0-9 _ -` oder länger als 16 Zeichen, doppelte
  Namen, zu viele Zeilen (acht je Liste, zwölf bei der LED-Zuordnung)
* Zuordnungen auf unbekannte Namen, unbekannte Funktions-, Zustands-, Farb-
  oder Musternummern, mehrere Funktionen für denselben Taster und derselbe
  Zustand zweimal auf derselben LED
* LED-Zeilen auf einer Kette mit unterschiedlichem Chiptyp oder gleicher
  Position
* UART-Nummern außerhalb `0 … SOC_UART_NUM-1`

**2. Crash-Loop-Erkennung.** Ein neu gespeichertes Profil gilt als
*unbewährt*. Jeder Start zählt einen NVS-Zähler hoch; nach 20 s Laufzeit setzt
`HwConfig::loop()` ihn zurück. Startet das Gerät zweimal nicht bis dahin
durch, wird das Profil verworfen und die Image-Werte greifen. Dasselbe Muster
wie beim OTA-Rollback.

**3. Werkeinstellungen zur Laufzeit.** Zwei Wege, beide löschen die gesamte
NVS-Partition:

* *System → Werkeinstellungen* im Dashboard (`POST /api/factory`), mit
  doppelter Rückfrage.
* Ein Taster mit dieser Funktion, siehe *Taster und LEDs*.

Der Knopf im Dashboard ist der wichtigere von beiden: Die Vorgabe enthält
**keinen** Werkeinstellungs-Taster, und ohne einen solchen blieb sonst nur das
Löschen des Flash über USB – ein schlechter Rettungsweg für ein Gerät, das im
Verteiler sitzt und über das Netz erreichbar ist.

Gelöscht wird in beiden Fällen im Haupttask, nicht im Web-Handler:
`nvs_flash_erase()` entzieht jedem offenen `Preferences`-Handle die Grundlage,
also darf danach nichts mehr auf dem alten Inhalt weiterlaufen.

> *Profil im Gerät löschen* im Profildialog ist etwas anderes und viel
> harmloser: Es leert nur den Namensraum `hwcfg`, WLAN und KNX bleiben. Es
> meldet jetzt auch, wenn das misslingt, statt einen Erfolg zu behaupten.

> **Warum es keine Taster-Rettung beim Einschalten gibt.** Naheliegend wäre,
> den Taster während des Starts gedrückt zu halten. Das kann auf keinem
> ESP32 funktionieren: Der Taster, den jedes DevKit hat, ist der
> **Boot-Strapping-Pin** – GPIO 0 bei ESP32, S2 und S3, **GPIO 9** bei C3 und
> C6. Der wird beim Verlassen des Resets abgetastet; liegt er auf Masse, geht
> der ROM in den seriellen Download-Modus und die Anwendung startet gar nicht
> erst. Der Rettungsweg wäre also genau dann tot, wenn man ihn braucht.
> Deshalb erledigen das die Crash-Loop-Erkennung und der Laufzeit-Taster.

Auf S3-Modulen mit **Octal**-Flash/PSRAM sind zusätzlich GPIO 35–37 belegt.
Bei aktiviertem PSRAM (`memory_type = qio_opi`, siehe unten) erkennt die
Validierung das über `CONFIG_SPIRAM_MODE_OCT` und weist diese Pins ab.

---

## SB-Interface programmieren

Der LPC1115 des SB-Interface trägt einen **ROM-Bootlader**, der ein
zeilenweises ASCII-Protokoll über UART0 spricht (NXP UM10398, Kapitel 26).
Genau die UART, an der ohnehin der KNX-Stack hängt. Zwei zusätzliche
Steuerleitungen genügen daher, um den LPC ohne Debug-Adapter zu erkennen, zu
löschen und neu zu programmieren.

### Welche GPIO

Vorgeschlagen und in [platformio.ini](platformio.ini) für den S3 eingetragen:

| Signal | GPIO | LPC-Pin |
| --- | --- | --- |
| `lpc_reset` | **4** | `/RESET` (PIO0_0) |
| `lpc_isp` | **5** | `PIO0_1` |

Auf dem S3-DevKitC-1 bleiben nach KNX-UART, I2C, SPI, Taster und RGB-LED nur
wenige Pins ohne Zweitaufgabe übrig. GPIO 4 und 5 sind zwei davon: kein
Strapping-Pin, nicht Flash oder PSRAM (26–37), nicht USB (19/20), nicht JTAG
(39–42), nicht UART0 (43/44) und nicht die RGB-LED (38/48). Sie liegen
nebeneinander und direkt neben dem I2C-Paar, was die Verdrahtung kurz hält.

Beide sind für andere Boards frei wählbar; die Validierung des Profils prüft
sie wie jeden anderen Pin.

### Ruhezustand heißt loslassen

Die Leitungen werden **open drain** betrieben: Zum Auslösen zieht der ESP32
sie auf Masse, sonst schaltet er den Pin auf Eingang. `/RESET` und `PIO0_1`
haben Pull-ups am LPC, also bleibt der LPC in seinem Anwendungsprogramm,
solange der ESP32 nichts tut.

Das ist keine Kosmetik. Ein ESP32 kommt mit hochohmigen Eingängen aus dem
Reset, und bis `hwConfig.begin()` gelaufen ist, ist jeder Pin unbestimmt.
Würde der Ruhezustand ein getriebener Pegel sein, hinge der LPC während des
gesamten ESP32-Starts in einem undefinierten Zustand – im ungünstigen Fall im
Reset.

Für Platinen mit **Invertern** in beiden Leitungen – etwa der vorhandene
Raspberry-Pi-Aufsatz – gibt es `lpc_invert`. Dann wird in beide Richtungen
getrieben, und die Platine braucht Pull-downs an den Invertereingängen.

### Was erkannt wird

Der Dialog *SB-Interface programmieren* (Karte **KNX TP1**) beantwortet drei
verschiedene Fragen:

**Welcher Chip hängt dran?** Der Bootlader liefert auf `J` seine Part-ID. Die
Tabelle in [src/lpc_isp.cpp](src/lpc_isp.cpp) übersetzt sie in einen Typnamen
samt Flash- und RAM-Größe und deckt die ganze LPC11xx-Familie ab. Dazu
kommen Bootlader-Version (`K`) und die 128-Bit-Seriennummer (`N`).

**Ist er schon programmiert?** Zwei unabhängige Aussagen. Der *Blank Check*
(`I 0 0`) sagt, ob Sektor 0 je beschrieben wurde. Ergiebiger ist die
Vektortabelle: Der Bootlader startet ein Anwendungsprogramm nur, wenn die
ersten acht Wörter in Summe null ergeben (UM10398, 26.3.3). Genau das rechnet
das Gerät nach, plus die Plausibilität des ersten Wortes – des initialen
Stapelzeigers, der ins RAM zeigen muss. Ergebnis: *leer*, *startfähiges
Programm* oder *Inhalt ohne gültige Prüfsumme*.

**Ist es die richtige Firmware?** Das kann der Bootlader nicht sagen. Die
TP-UART-2-Emulation kennt keinen Versions- oder Produktbefehl – ein echter
TP-UART hat keinen, und die Emulation bildet einen echten nach. Was das Gerät
stattdessen zeigt, ist die **funktionale** Antwort: ob die Emulation auf den
`U_Reset.req` des KNX-Stacks antwortet. Das ist dieselbe Information wie
*Verbindung* auf der Hauptseite, hier nur neben den Bootlader-Angaben. Zusammen
ergibt das eine brauchbare Diagnose: gültiges Programm **und** keine Antwort
heißt „da läuft etwas, aber nicht die TPUART-Emulation".

### Programmieren

Angenommen werden **Intel-Hex** und **rohe Binärdateien**, jeweils ab Adresse
0; das erste Byte entscheidet, welches von beidem es ist. Die Datei landet
zunächst nur in einem Zwischenpuffer – geschrieben wird erst nach einem
zweiten, ausdrücklichen Klick.

Der Puffer **wächst mit der Datei** in 4-KiB-Schritten und liegt im PSRAM,
wenn welches da ist. Auf Boards ohne PSRAM kommt er aus dem internen Heap:
Deshalb wird nur die tatsächliche Dateigröße belegt – eine TPUART-Emulation
ist ein Bruchteil der 64 KiB, die ein LPC1115 fassen könnte –, deshalb prüft
das Gerät vorher, ob danach noch 48 KiB frei bleiben, und deshalb wird der
Puffer nach erfolgreichem Schreiben wieder freigegeben. Ein
fehlgeschlagener Versuch behält ihn, damit ein zweiter Anlauf ohne erneutes
Hochladen geht.

Beim Ablegen wird die **Prüfsumme der Vektortabelle** nachgerechnet und, wenn
sie nicht stimmt, gesetzt. Ob ein Werkzeug das schon getan hat, ist von der
Toolchain abhängig; fehlt sie, bleibt der LPC nach dem Reset im Bootlader und
sieht aus wie tot.

Der Ablauf danach folgt dem Protokoll: entsperren (`U`), Sektoren vorbereiten
(`P`), löschen (`E`), dann blockweise 1 KiB ins RAM des LPC (`W`,
UU-kodiert), noch einmal vorbereiten und ins Flash kopieren (`C`). Die
RAM-Kopie liegt danach noch da, also verifiziert ein `M` jeden Block, ohne
etwas ein zweites Mal zu übertragen.

Warum 1 KiB und nicht 4: Der kleinste Vertreter der Familie hat 4 KiB RAM,
und der Bootlader belegt davon die ersten 0x300 Bytes.

### Während ein Auftrag läuft

Die UART kann nicht zwei Herren dienen. Jeder Auftrag bittet deshalb zuerst
`KnxLink::suspend()` um sie. Das setzt nur ein Flag – anhalten darf sich der
Stack ausschließlich selbst, in `loop()` auf dem Haupt-Task, wo er nicht
mitten in einem Telegramm steht. Erst wenn das quittiert ist, öffnet der
Auftrag den Port mit 115200 Baud 8N1.

Danach läuft der umgekehrte Weg: Der LPC wird in sein Anwendungsprogramm
zurückgesetzt, der Port geschlossen, und `resume()` lässt den Haupt-Task den
TP-UART-Reset-Handshake neu fahren – was zugleich die Uhr des Stacks mit dem
gerade neu gestarteten SB-Interface synchronisiert.

Der Auftrag selbst läuft auf einer eigenen Task, damit die Oberfläche
bedienbar bleibt. Sie fragt `/api/lpc` ab, solange `busy` gesetzt ist.

Autobaud: Der Bootlader misst die Bitzeit eines einzelnen `?`. Ein zweites,
das während der Messung eintrifft, ist der häufigste Grund für sporadisches
Scheitern – deshalb genau eines pro Versuch. Drei Versuche, der letzte mit
57600 Baud, falls die Leitung lang oder verrauscht ist.

---

### PSRAM

Die Board-Definition `esp32-s3-devkitc-1` geht von der Variante **N8** ohne
PSRAM aus. Ein **N8R8** hat 8 MB Octal-PSRAM, das sonst unsichtbar bleibt –
`ESP.getPsramSize()` meldet dann 0 und das Dashboard zeigt *nicht aktiviert*.

Eingeschaltet wird es in [platformio.ini](platformio.ini):

```ini
board_build.arduino.memory_type = qio_opi   ; QIO-Flash + OPI-PSRAM
build_flags = -DBOARD_HAS_PSRAM
```

Der Preis sind **GPIO 35, 36 und 37**: Über die spricht das Modul mit dem
PSRAM, für alles andere sind sie damit weg.

Die Firmware **braucht** kein PSRAM – sie belegt rund 56 KB der 320 KB
internen RAMs. Allokationen bis 4 KB bleiben ohnehin intern
(`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL`). Der RMT-Puffer der LED-Kette wird
zusätzlich ausdrücklich intern angefordert: Der Treiber liest ihn aus einem
Interrupt, und PSRAM ist nicht erreichbar, solange der Flash-Cache aus ist.

Genutzt wird es für den **Protokollpuffer** und den **Busmonitor**, siehe
unten.

### PSRAM aufteilen

Beide Ringe sind die einzigen großen PSRAM-Mieter, und wie viel jeder
bekommt, ist Geschmackssache: Wer einen sporadischen Absturz sucht, will
Protokoll; wer einem Aktor beim Schwatzen zusehen will, Busmonitor. Deshalb
steht die Aufteilung im **Hardware-Profil** (*Hardware → Profil bearbeiten →
Speicheraufteilung*), mit Schiebereglern und einem gestapelten Balken.

| Feld | JSON | Vorgabe |
| --- | --- | --- |
| Protokoll | `log_kib` | 512 KiB |
| Busmonitor | `monitor_kib` | 384 KiB |

Die Summe wird auf `PSRAM − 256 KiB` **gekappt** – die Reserve
(`HW_PSRAM_RESERVE_KIB` in [src/hw_config.h](src/hw_config.h)) geht an
TLS-Handshakes und den Zwischenspeicher des Firmware-Updates. Ein Heap, der
erst beim Start des Updates voll ist, ist ein schlechter Ort für diese
Erkenntnis. Das Formular weicht schon beim Tippen aus, statt beim Speichern zu
meckern.

> Gekappt, **nicht** abgewiesen: Ein Profil, das vor diesen Feldern angelegt
> wurde, lädt sie aus den Vorgaben. Würde das zu einem Fehler in
> `validate()` führen, flöge das **ganze** Profil weg – Pins inbegriffen, und
> plötzlich findet das Gerät weder SB-Interface noch W5500. Genau das ist
> einmal passiert; `clampMemory()` ist die Lehre daraus.

`0` schaltet ab: ohne Protokollpuffer bleibt der 4-KiB-Notring im internen
RAM, ohne Monitorring entfällt der Busmonitor. Beides wirkt erst nach einem
Neustart.

Ohne PSRAM bleiben die Werte folgenlos – sie werden trotzdem angenommen und
gespeichert, damit ein Profil zwischen einem WROOM und einem WROVER hin- und
hergeschoben werden kann.

> `LogBuffer::begin()` muss laufen, bevor irgendetwas loggen kann – also
> bevor das Hardware-Profil gelesen ist. Die gewählte Größe zieht deshalb
> erst `sysLog.resize()` direkt nach `hwConfig.begin()` nach. Dabei wandern
> nur die neuesten 16 KiB mit: Mehr zu kopieren brächte nichts und hält die
> Sperre unnötig lange.

### Protokollpuffer

Ein Gerät im Verteiler hat keine serielle Konsole angeschlossen. Alles, was
die Firmware ausgibt, landet deshalb zusätzlich in einem Ringpuffer und ist
im Dashboard unter *System → Protokoll* abrufbar.

| | |
| --- | --- |
| mit PSRAM | 512 KiB, im Hardware-Profil einstellbar |
| ohne PSRAM | 4 KiB im internen RAM |

Zwei Quellen speisen ihn: alles, was über `sysLog` geschrieben wird – das ist
der gesamte Firmware-Code und, über `ArduinoPlatform::SerialDebug`, auch der
KNX-Stack – und der ESP-IDF-Log (die `[E][Preferences.cpp:47]`-Zeilen), der
über `esp_log_set_vprintf()` mitgeschnitten wird. Die serielle Ausgabe bleibt
unverändert: `sysLog` schreibt durch.

Der Puffer liegt im RAM und überlebt keinen Neustart. `GET /api/log` liefertdie neuesten 8 KiB als Text, `?bytes=` mehr, `?download=1` setzt zusätzlich
einen Dateinamen. `POST /api/log/clear` leert ihn.

Die Antwort wird **gestückelt** direkt aus dem Ring gesendet: Ein voller
Auszug ist 64 KiB, und als `String` gebaut würde er kurzzeitig das Doppelte an
Heap kosten. Läuft während eines Downloads viel Neues auf, sieht man die Naht
– das ist der Preis dafür, den Puffer nicht doppelt zu halten.

Jede Zeile trägt einen Zeitstempel: `12:34:56` sobald die Uhr steht, davor
`+00:01:23` seit dem Start. Das Pluszeichen unterscheidet beides.

> Im Code steht deshalb `sysLog.printf(...)` statt `Serial.printf(...)`.
> `Serial` selbst bleibt für `begin()`, `flush()` und `setTxTimeoutMs()`.

Kopieren in die Zwischenablage nimmt die Markierung, sonst das ganze
angezeigte Protokoll. `navigator.clipboard` gibt es dabei nur im sicheren
Kontext, den es über `http` nicht gibt – genau wie bei `crypto.subtle` beim
Firmware-Upload. Deshalb der Umweg über ein kurzzeitiges `textarea`.

### Was einen Neustart überdauert

PSRAM ist DRAM: Es braucht ständiges Refresh, und der Startcode initialisiert
den Controller neu. Der große Ringpuffer ist nach jedem Reset leer – daran
lässt sich nichts ändern.

Ein **Reset** überlebt dagegen der **RTC-Slow-Speicher**, den der Startcode
bewusst unangetastet lässt (`RTC_NOINIT_ATTR`). Dort liegt ein zweiter, 3 KiB
großer Ring mit denselben Zeilen. Nach einem Watchdog oder einer Panik steht
darin, was unmittelbar davor passiert ist – im Dashboard über *Vor dem
Neustart*.

Ein **Stromausfall** löscht auch diesen. Ein gültiges Kennzeichen im
RTC-Speicher unterscheidet beide Fälle; fehlt es, wird der Ring verworfen
statt Zufallsdaten anzuzeigen. Geprüft wird das **vor** allem anderen in
`LogBuffer::begin()` – sonst würde die erste Logzeile den Ring mit einem
zufälligen Offset indizieren.

Die erste Zeile nach dem Start nennt den **Neustartgrund** im Klartext
(`power-on`, `software`, `panic`, `task watchdog`, `brownout` …). Damit ist
auch ohne überlebten Ring erkennbar, ob ein Neustart gewollt war.

> Nach einem `upload` über esptool steht dort `power-on` und der Ring ist
> leer: Der Reset über die EN-Leitung löscht auch den RTC-Bereich. Überdauert
> wird nur ein **Software**-Neustart – also Watchdog, Panik oder der Neustart
> aus dem Dashboard.

Was überdauert hat, wird beim Start **vorne in den großen Ring kopiert**,
getrennt durch `----- restart -----`. Das Fenster liest sich dadurch als ein
durchgehendes Protokoll über den Neustart hinweg. Einen eigenen Knopf braucht
der RTC-Teil deshalb nicht; für Werkzeuge bleibt er unter
`GET /api/log/reset` für sich abrufbar.

### Der Ring ist ein Ring

Ist der Puffer voll, überschreibt Neues das Älteste – etwas anderes kann ein
Ringpuffer nicht. Der Füllstand steht unter dem Fenster und weist darauf hin,
sobald überschrieben wird.

Wie schnell das geht, hängt an der Redseligkeit der Firmware. Mit
`-DKNX_LOG_TUNNELING` protokolliert der KNX-Stack **jedes** Telegramm; für
alles außer der Fehlersuche am Tunneling gehört das Flag weggelassen.

### Blättern statt alles laden

Ein halbes Megabyte in einem `<pre>` macht das Blättern zäh. Das Dashboard
hält deshalb nur einen **Ausschnitt von 48 KiB** – ungefähr doppelt so viel
wie sichtbar, je ein Viertel als Reserve davor und dahinter. Wer an einen
Rand blättert, bekommt den nächsten Abschnitt nachgeladen, während am anderen
Ende einer wegfällt.

Dafür kennt der Puffer **absolute Positionen**: `written()` zählt alle je
geschriebenen Bytes, gehalten wird `[oldest(), written())`. `GET /api/log`
nimmt deshalb ein `from=` und liefert die aktuelle Lage in den Kopfzeilen
`X-Log-From`, `X-Log-Oldest` und `X-Log-Written` zurück. Ohne `from` antwortet
es wie bisher mit dem Ende.

Hat der Schreiber den Leser überholt, setzt `copyFrom()` die Position auf den
ältesten noch vorhandenen Eintrag – der Ausschnitt springt dann, statt Bytes
auszugeben, die inzwischen etwas anderes bedeuten.

Nur der **Download** holt den ganzen Puffer; er geht gestückelt und kennt
diese Grenze nicht.

Zu den Kosten: Geschrieben wird ein Byte pro Zeichen über den RTC-Bus,
zusätzlich zum Hauptring. Bei einem Protokoll, das ein paar Zeilen pro Sekunde
produziert, sind das einige Mikrosekunden – gemessen an dem, was ein
unerklärter Neustart sonst an Suche kostet, ist das nichts. Abschaltbar ist es
trotzdem.

### Speicherung

Die Einzelfelder liegen als **einzelne NVS-Schlüssel**, nicht als JSON-Blob.
JSON ist nur das Transportformat für das Dashboard: Geparst wird einmal beim
Annehmen, nie im Startpfad. Ein abgeschnittener oder beschädigter Blob kann den
Start daher nicht verhindern.

Die vier Listen sind die Ausnahme – sie liegen als rohe Struktur-Arrays. Damit
eine geänderte Struktur nicht als Pin-Nummern fehlgedeutet wird, trägt der
Satz eine Layout-Kennung (`IO_VERSION`), und jede Blob-Länge wird gegen die
erwartete Größe geprüft. Passt etwas nicht, fällt der **komplette** Satz auf
die Image-Vorgaben zurück. Eine halb gelesene Liste wäre schlimmer als gar
keine: Zähler und Array widersprächen sich, und `pinMode()` bekäme, was
zufällig im Flash stand.

### Startreihenfolge

`HwConfig::begin()` läuft ganz vorn in `setup()` – vor Improv, vor Ethernet,
vor KNX; nur der Protokollpuffer und die Zeitzone stehen davor. Es entscheidet
über die Pins, kostet rund 20 ms, und das Improv-Fenster bleibt damit
innerhalb der zwei Sekunden, die ESP Web Tools abwartet.

---

## Busmonitor

Zeichnet die Telegramme beider Seiten auf – TP1, IP oder beide – und zeigt sie
in einem eigenen Fenster mit Filtern. Gedacht für die Frage, die kein Zähler
beantwortet: *Was genau läuft da über den Bus, und wer fängt an?*

### Warum der Stack dafür gepatcht werden muss

Der Stack hat keinen Weg, ein Telegramm nach außen zu geben. Am nächsten kommt
`KNX_ACTIVITYCALLBACK`, aber dessen Rückruf trägt nur **Richtung und
Netz-Index** – genug, um die Buslast zu zählen, nicht, um sie zu zeigen.

[scripts/patch_knx.py](scripts/patch_knx.py) setzt deshalb einen sechsten
Patch in `data_link_layer.cpp`, an drei Stellen:

| Stelle | Was dort vorbeikommt |
|---|---|
| `frameReceived()` | jedes empfangene Telegramm, auf der Schicht, die es empfing |
| `sendTelegram()` | jedes gesendete, unmittelbar vor dem Medium |
| `dataRequestFromTunnel()` | was über einen Tunnel hereinkommt |

Die dritte Stelle braucht eine Unterdrückungsflagge: Sie ruft ihrerseits
`frameReceived()` für die lokale Zustellung auf, was dasselbe Telegramm ein
zweites Mal in die Liste schriebe.

**Sie bekommt eine eigene Seite: `Tunnel`.** Weder *IP* noch *TP* wäre dort
ehrlich – das Telegramm kommt über IP herein, wird aber auf der Schicht
zugestellt, an der der cEMI-Server hängt, und das ist laut `bau091A.cpp` die
**TP**-Schicht (`_cemiServer.dataLinkLayer(_dlLayerSecondary)`, Kommentar
dort: *„Secondary I/F is the important one!“*). Ursprünglich stand hier eine
fest verdrahtete `0` (= IP). Die Folge war eine Aufzeichnung, in der auf der
TP-Seite nie ein Empfang auftauchte und ein Telegramm scheinbar auf derselben
Seite wieder hinausging, auf der es hereinkam – was ein Koppler nie tut. Das
hat beim Suchen der Telegrammschleife oben **zweimal** in die falsche Richtung
geführt.

> Wer eine Aufzeichnung deutet, sollte das im Kopf haben: `Tunnel` ist die
> Übergabe durch einen Tunnel-Client, `IP` der Routing-Multicast, `TP` die
> Busleitung.

Beide Symbole sind in [src/bus_monitor.cpp](src/bus_monitor.cpp) definiert,
nicht im Stack. Findet der Patch seine Anker nicht mehr, kostet das den
Monitor seine Eingabe, nicht den Build. Welcher der beiden Fälle vorliegt,
sagt das Define `SBIP_MONITOR_HOOK`, das derselbe Patch setzt – das Dashboard
schreibt dann „Stack-Hook fehlt“ statt eine leere Liste zu zeigen.

### Der Aufzeichnungspfad darf nichts tun

`capture()` läuft im Haupttask, mitten in `knx.loop()`, direkt neben der
TP-UART-Zeitbedingung. Dort passiert deshalb nur: Zustand prüfen, Seite
prüfen, `memcpy` der rohen cEMI-Bytes, Zähler hochzählen. **Kein**
Formatieren, keine Speicheranforderung, kein `String`.

Das Zerlegen in Adressen, Priorität, Dienst und Nutzdaten macht der
Web-Handler, wenn jemand hinsieht. Ein Telegramm kostet im Ring 48 Byte: vier
Byte Zeitstempel, vier Byte Kopf und 40 Byte rohes cEMI. Ein Standard-Telegramm
mit voller APDU ist 25 Byte lang, wird also nie abgeschnitten; nur erweiterte
Rahmen sind es, und die tragen ihre wahre Länge mit.

### PSRAM oder gar nicht

Der Ring liegt **ausschließlich im PSRAM**. Der interne Heap hat an dieser
Stelle etwa 200 KiB frei, und den gegen ein Diagnosewerkzeug einzutauschen
wäre die falsche Reihenfolge: Er hält die Tunnelverbindungen und den Webserver
am Leben.

Wie groß er ist, steht im Hardware-Profil (siehe *PSRAM aufteilen*). Die
Vorgabe sind **384 KiB**, also 8000 Telegramme – bei 30 Telegrammen pro
Sekunde, einer gut ausgelasteten TP1-Linie, rund vier Minuten Dauerverkehr.
Unter 200 Telegrammen lohnt der Block nicht, dort bleibt der Monitor aus.

Ohne PSRAM bleibt er ebenfalls aus. Das ist kein Verlust an Diagnose, sondern
ein Verweis: Der **Gruppenmonitor der ETS** kann dasselbe, und dieses Gerät
ist als Schnittstelle dafür ohnehin eingetragen.

### Der Busmonitor der ETS bleibt leer
Der **Gruppenmonitor** der ETS funktioniert über dieses Gerät, der
**Busmonitor** nicht. Das ist keine Fehlfunktion, sondern eine fehlende
Funktion des Stacks: Ein Busmonitor-Tunnel ist eine eigene Verbindungsart,
und `IpDataLinkLayer` nimmt ausschließlich `TUNNEL_LINKLAYER` (0x02) an:

```cpp
if (connRequest.cri().type() == TUNNEL_CONNECTION && connRequest.cri().layer() != 0x02)
{
    //We only support 0x02!
    KnxIpConnectResponse connRes(0x00, E_TUNNELING_LAYER);
```

Die Absage ging bislang wortlos hinaus – die Erklärung lag hinter
`KNX_LOG_TUNNELING`, das hier nicht gesetzt ist. Deshalb sah es so aus, als
käme einfach nichts an. Jetzt steht es im Protokoll:

```
sbip: tunnel refused - only LinkLayer is supported, the ETS bus monitor
cannot work over this device
```

Für das Mitlesen des Busverkehrs ist der eingebaute Busmonitor da, der
denselben Zweck erfüllt und zusätzlich die IP- und Tunnelseite zeigt.

### Sofort oder auf Trigger

| Start | Verhalten |
|---|---|
| **sofort** | zeichnet ab dem Klick auf |
| **bei Gruppenadresse** | wartet auf ein Telegramm an diese Adresse |
| **bei Wiederholung** | wartet auf einen wiederholten Rahmen (CTRL1 Bit 5 gelöscht) |
| **bei KNX Data Secure** | wartet auf einen gesicherten Rahmen (APCI 0x3F1) |

Dazu **Telegramme davor** und **Telegramme danach**. Der Vorlauf ist der
eigentliche Wert eines Triggers: Was zu einem Ereignis geführt hat, kann
niemand mehr aufzeichnen, wenn es passiert ist. Der Ring läuft deshalb schon
im Zustand *wartet*, begrenzt auf die eingestellte Anzahl; erst der Auslöser
hebt die Begrenzung auf. Der Nachlauf begrenzt, wie viele Telegramme nach dem
Auslöser noch aufgenommen werden – 0 heißt „bis zum Anhalten".

**Anhalten und wieder starten setzt fort.** Geleert wird nur auf Knopfdruck.
Die Folgenummern bleiben über die Pause hinweg monoton, es geht also nichts
durcheinander. Nur wenn *Bei vollem Puffer anhalten* gesetzt und der Ring voll
ist, wird ein Start abgewiesen – er würde beim nächsten Telegramm sofort wieder
enden.

### Was die ETS mehr kann

Der ETS-Busmonitor bietet als Auslöser zusätzlich *Acknowledged*, *Not
acknowledged*, *Negatively acknowledged*, *Invalid*, *Unknown source address*,
*Unknown destination address*. Davon ist hier nichts nachrüstbar, und der Grund
liegt eine Ebene tiefer:

* **Quittungen** erzeugt das SB-Interface **autonom**. Das ACK-Zeitfenster von
  rund 1,4 ms lässt keinen UART-Roundtrip zum ESP32 zu – der Host erfährt nie,
  wie quittiert wurde. Siehe *Grenzen gegenüber einem NCN5130-Gateway*.
* **Ungültige Telegramme** verwirft der Stack, bevor `frameReceived()` läuft.
  Am Haken kommt nur an, was die Prüfsumme bestanden hat; die Zahl der
  verworfenen steht als Zähler auf der Startseite.
* **Unbekannte Adressen** setzen ein Projekt voraus. Das Gerät kennt nur seine
  Filtertabelle, und die enthält Adressen ohne Namen.

### Anzeige

Wie beim Protokollfenster hält der Browser nur einen **Ausschnitt**, hier 400
Telegramme, adressiert über absolute Folgenummern. `GET /api/monitor/frames`
nimmt `from=` und `max=` und gibt die Lage in `X-Mon-Oldest` und
`X-Mon-Written` zurück. Die Antwort ist gestückelt und wird Eintrag für
Eintrag formatiert – 200 Telegramme als ein `String` wären 20 KiB auf einem
Heap, der etwas Besseres zu tun hat.

Der Filter über Seite, Richtung, Adressart, Quelle, Ziel und Dienst wirkt
**auf das Geladene**, nicht auf die Aufzeichnung. Was aufgezeichnet wird,
entscheidet allein die Seitenauswahl im Gerät. Der CSV-Export gibt genau das
aus, was der Browser hält – sonst passten Anzeige und Datei nicht zusammen.

Farbe trägt zwei Aussagen, ohne zu wiederholen, was danebensteht: Die **linke
Kante** nennt die Seite – grün TP, blau IP – und eine **getönte Zeile** heißt
gesendet. Ein Klick auf eine Zeile hebt alles zur selben Gruppenadresse hervor,
ein zweiter hebt es wieder auf.

### Die Spalte „Wert" ist ein Vorschlag

Ein Koppler kennt den **Datenpunkttyp** einer Gruppenadresse nicht. Der steht
im ETS-Projekt; die Filtertabelle, die das Gerät bei einem Download bekommt,
enthält ausschließlich Adressen. Gedeutet wird deshalb nach der Länge der
Nutzdaten:

| Länge | Deutung |
|---|---|
| ≤ 6 Bit | Aus / Ein (DPT 1.x), sonst der 4-Bit-Wert (DPT 3.x) |
| 1 Byte | Rohwert (DPT 5.010) und Prozent aus 0…255 (DPT 5.001) |
| 2 Byte | KNX-Gleitkomma (DPT 9.x) und Rohwert (DPT 7.001) |
| 3 Byte | Uhrzeit (DPT 10.001) **und** Datum (DPT 11.001), wenn beides passt |
| 4 Byte | IEEE-Gleitkomma (DPT 14.x) und vorzeichenlos (DPT 12.001) |
| 14 Byte | Zeichenkette (DPT 16.000) |

Jede Angabe nennt den Typ, aus dem sie stammt, damit sichtbar bleibt, dass es
eine Annahme ist. Die Spalte **Bits** daneben sagt, wie breit die Nutzdaten
sind; bei bis zu sechs Bit steht `≤ 6`, weil die tatsächliche Breite – ein Bit
bei DPT 1, vier bei DPT 3 – nirgends im Telegramm steht.

### Die Spalte „Dienst" nennt zuerst die Transportschicht

Vor der APCI steht die **TPCI**, und die zu überspringen war ein Fehler mit
Folgen: Ein `T_Connect` besteht aus einem einzigen Byte. Wer trotzdem ein
zweites liest, bekommt APCI 0x000 – also `GroupValueRead`. Jeder
Verbindungsaufbau der ETS stand damit als Gruppentelegramm in der Liste,
obwohl es reiner Punkt-zu-Punkt-Verkehr ist:

```
Tunnel;TX;1.1.5;1.1.0;GroupValueRead      <- in Wirklichkeit T_Connect
```

Erkannt werden jetzt `T_Connect`, `T_Disconnect`, `T_ACK` und `T_NAK`.

Dazu kommt: `GroupValueRead`, `-Response` und `-Write` gibt es **nur** zu
einer Gruppenadresse. Steht im Ziel eine physikalische Adresse, bedeutet
dieselbe APCI etwas anderes, und die Spalte zeigt dann `APCI 0x0xx` statt
eines Namens, den das Telegramm nicht trägt.

> Praktisch heißt das: Wer im Monitor nach echtem Gruppenverkehr sucht, achtet
> auf ein Ziel in der Form `1/2/3`. Ziele wie `1.1.0` sind Punkt-zu-Punkt,
> `0/0/0` ist ein Broadcast – beides erscheint im **Gruppenmonitor der ETS**
> grundsätzlich nicht.

Bei 1 Bit und 2 Byte trifft die Deutung fast immer, bei 1 Byte ist sie
mehrdeutig, und bei 3 Byte lassen sich Uhrzeit und Datum grundsätzlich nicht
unterscheiden – dann stehen beide da. Die Rohbytes bleiben deshalb in der
Spalte daneben stehen. `GroupValueRead` wird nicht gedeutet: Die Null darin ist
Füllung, kein Wert.

Gestempelt wird mit der Betriebszeit, nicht mit der Uhrzeit: Eine Zeitzone hat
im Aufzeichnungspfad nichts zu suchen. Steht die Uhr, rechnet der Browser aus
`now_ms` und `epoch_ms` die Tageszeit; sonst bleibt `+hh:mm:ss.mmm` stehen.
Beide Werte kommen **in Millisekunden und direkt nacheinander gelesen** aus
demselben Statusabruf – mit einer Epoche in ganzen Sekunden schwankte die
Differenz zwischen zwei Abrufen um bis zu eine Sekunde, was sich als
springende Sekundenbruchteile beim Neuladen zeigte.

Die Spalte daneben zeigt den Abstand zum vorigen **angezeigten** Telegramm –
für die Frage nach Wiederholungen und Stürmen die eigentlich interessante
Zahl.

Direkt danach kommen `statusLed.begin()` und `buttonService.begin()`: Beide
brauchen das Profil für ihre Pins, und die Anzeige soll stehen, solange der
laute Teil des Starts läuft. `ethInterface.begin()` läuft vor
`netManager.begin()` – nur deshalb kann dort geprüft werden, ob WLAN
überhaupt abgeschaltet bleiben darf.

Ein Detail: Die UART-Nummer ist ein **Konstruktor-Argument** von
`HardwareSerial`. Statische Objekte entstehen aber vor `setup()` – also lange
bevor das Profil bekannt ist. Der Port wird deshalb in `KnxLink::begin()`
dynamisch angelegt und dem Platform-Objekt per `knxUart()` untergeschoben.

> **Zum Präfix:** In `-DSBIP_KNX_RX_PIN=16` ist `-D` der Compiler-Schalter
> „Define“ und `SBIP_` das Projekt-Präfix (SelfBus IP). Frühere Fassungen
> nutzten `GW_`, was zusammen mit dem Schalter wie ein Token „DGW“ aussah.

---

## Build

### Entwicklungsumgebung einrichten

Getestete Kombination:

| Komponente | Version |
| --- | --- |
| VS Code + Extension **PlatformIO IDE** (`platformio.platformio-ide`) | PlatformIO Core 6.1.19 |
| Plattform | pioarduino 54.03.21 |
| Arduino-ESP32 | 3.2.1 (ESP-IDF 5.4.2) |
| Git | beliebig, wird für den Build-Hash gebraucht |

1. **VS Code** installieren, darin die Extension *PlatformIO IDE*. Sie bringt
   eine eigene Python-Umgebung unter `~/.platformio/penv` mit; eine separate
   Python-Installation ist nicht nötig.
2. **Repository klonen.** Ohne Git-Historie funktioniert der Build zwar, aber
   [scripts/version_bump.py](scripts/version_bump.py) trägt dann keinen
   Commit-Hash in `src/build_info.h` ein.
3. **Proxy** – nur in Netzen ohne direkten Internetzugang. PlatformIO liest im
   Terminal ausschließlich die Umgebungsvariablen, die VS-Code-Einstellung
   `http.proxy` wirkt dort *nicht*:

   ```powershell
   # Windows (PowerShell), dauerhaft für den Benutzer
   [Environment]::SetEnvironmentVariable("HTTP_PROXY","http://proxy:8080","User")
   [Environment]::SetEnvironmentVariable("HTTPS_PROXY","http://proxy:8080","User")
   ```

   ```bash
   # Linux/macOS, dauerhaft in ~/.profile bzw. ~/.bashrc
   export HTTP_PROXY=http://proxy:8080
   export HTTPS_PROXY=http://proxy:8080
   ```

4. **Ersten Build starten.** Plattform, Toolchains und Framework (zusammen gut
   1,5 GB) werden dabei automatisch geholt – die Plattform-URL steht in
   [platformio.ini](platformio.ini) und ist auf eine feste Release-Version
   gepinnt. Rechne beim ersten Mal mit einigen Minuten pro Ziel.

Die Registry-Plattform `espressif32` wird **nicht** verwendet: Sie liefert
weiterhin Arduino-Core 2.0.x, und dort fehlt die halbe API dieses Projekts –
`ETH.begin(ETH_PHY_W5500, …, SPI, …)`, `ETH.hasIP()`, `ETH.setDefault()` sowie
die mbedTLS-3-Signaturen in [src/fw_hash.cpp](src/fw_hash.cpp). Core 2.0.x
kennt zudem nur RMII-PHYs und kann einen W5500 gar nicht ansprechen.

### Bekannte Stolpersteine

**`TypeError: ParamType.get_metavar() missing 1 required positional argument`**
beim Erzeugen von `bootloader.bin`. Das mitgelieferte `tool-esptoolpy` 5.0.0
ist mit Click ≥ 8.2 inkompatibel. Einmalig in der PlatformIO-Umgebung:

```powershell
# Windows
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" -m pip install "click==8.1.8"
```

```bash
# Linux/macOS
~/.platformio/penv/bin/python -m pip install "click==8.1.8"
```

**`error: unrecognized arguments: --ng`** bei `pio run -t metrics`. Umgekehrter
Fall derselben Art: Die Plattform ruft `esp_idf_size --ng` auf, ein Flag aus
der 1.x-Reihe, das in `esp-idf-size` 2.x entfernt wurde – dort ist das Format
Standard. Auf die passende Version zurück:

```powershell
# Windows
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" -m pip install "esp-idf-size==1.6.1"
```

```bash
# Linux/macOS
~/.platformio/penv/bin/python -m pip install "esp-idf-size==1.6.1"
```

**`fatal error: Network.h: No such file or directory`** darf nicht auftreten –
dagegen wirkt [scripts/framework_includes.py](scripts/framework_includes.py),
das über `extra_scripts` eingebunden ist. Die Arduino-3.x-Bibliotheken
inkludieren sich gegenseitig, deklarieren das aber nicht in ihren
`library.properties`. Sobald der Library Dependency Finder sie als eigene
Libraries einzieht – hier über `knx` und `ESPAsyncWebServer`, die `WiFi.h`
einbinden –, fehlen ihnen die Pfade zueinander.

**Änderungen an `platformio.ini`** verwerfen `.pio/build` vollständig. Danach
bauen alle Ziele neu, nicht nur das geänderte.

### Bauen

```
pio run                 # alle Ziele
pio run -e esp32dev     # nur eines
```

Der Pre-Script [scripts/version_bump.py](scripts/version_bump.py) erzeugt
`src/build_info.h` mit Buildnummer und Git-Hash.

### Alle Varianten bauen und ablegen

[scripts/release.py](scripts/release.py) baut die Umgebungen, benennt die
Images nach Umgebung und Version und schreibt das passende Manifest daneben:

```powershell
python scripts\release.py                        # alle Umgebungen
python scripts\release.py -e esp32dev -e esp32s3 # nur diese
python scripts\release.py --no-build             # nur einsammeln
```

```bash
python scripts/release.py
python scripts/release.py -e esp32dev -e esp32s3
python scripts/release.py --no-build
```

Ergebnis in `release/`:

```
firmware_esp32dev_0.1.0.bin
firmware_esp32s3_0.1.0.bin
…
manifest.json
```

Die Version kommt aus `FIRMWARE_VERSION` in
[include/interface_config.h](include/interface_config.h) und wird **nicht**
automatisch erhöht – ein Release ist eine Entscheidung, kein Nebenprodukt.
Liegt eine Datei desselben Namens bereits im Git, sagt das Skript das: Dann
gehört die Versionsnummer erhöht, bevor gebaut wird.

Das Manifest ist nach **Chipfamilie** geschlüsselt, genau wie
`UPDATE_CHIP_KEY` in [src/ota_service.cpp](src/ota_service.cpp) – ein Gerät
sucht immer nur seinen eigenen Eintrag. Die 8-MB-Umgebungen bekommen deshalb
eine Datei, aber **keinen** Manifest-Eintrag: Sie unterscheiden sich von ihren
4-MB-Gegenstücken allein in der Partitionstabelle, und die kann ein Update
ohnehin nicht ändern.

Die Buildnummer steht als `build` im Manifest. Das Gerät liest sie nicht, aber
sie beantwortet später die Frage, welcher Stand das eigentlich war. Sie steigt
pro Umgebung, ein Lauf über fünf Ziele verbraucht also fünf Nummern.

> Binärdateien im Repository lassen es wachsen. Wer das vermeiden will, hängt
> dieselben Dateien an ein **GitHub-Release** und trägt dessen Download-URL als
> `update_url` ein – das Manifest bleibt unverändert, nur die Adresse ändert
> sich.

### Flash-Größe und Partitionslayout

Vorgabe ist [partitions_4mb_ota.csv](partitions_4mb_ota.csv): zwei App-Slots zu
je 1,94 MB, dazu `nvs`, `otadata`, `knxcfg` und `coredump`. Für Module mit 8 MB
gibt es [partitions_8mb_ota.csv](partitions_8mb_ota.csv) mit doppelt großen
Slots und dazu fertige Umgebungen:

```
pio run -e esp32dev_8mb
pio run -e esp32s3_8mb
pio run -e esp32c6_8mb
```

Sie erben alles vom jeweiligen 4-MB-Ziel und ändern nur die Tabelle und
`board_upload.flash_size`. Dieselben drei Zeilen machen jede andere Umgebung
zur 8-MB-Variante.

> **Der Wechsel geht nicht über OTA.** Die Partitionstabelle liegt außerhalb
> jeder Partition und wird nur von esptool geschrieben; ein Gerät im 4-MB-Layout
> muss dafür über USB geflasht werden. `nvs` bleibt bei `0x9000` – WLAN-Zugang
> und Hardware-Profil überstehen das. `knxcfg` wandert von `0x3F0000` nach
> `0x7F0000` und ist danach leer: **das Gerät muss in der ETS neu programmiert
> werden.**

#### Warum 2 MB nicht geht

Es gibt keinen Trick. OTA braucht zwei Slots, die jeder ein *vollständiges*
Image aufnehmen; diese Firmware ist rund 1,6 MB groß. In 2 MB bleiben nach
Bootloader, Partitionstabelle, `nvs` und `otadata` etwa 1,87 MB für alles
Weitere – genug für **ein** Image, nicht für zwei.

Möglich wäre also nur eine einzelne App-Partition ohne OTA. Damit entfällt
zugleich der Rollback, der ein fehlerhaftes Update abfängt – die
Anti-Brick-Eigenschaft aus der Übersicht oben ist genau dieses zweite Slot.
Über USB bleibt das Gerät natürlich flashbar. Der Tausch lautet also „kein
Fernupdate und kein Rollback" gegen „halber Flash", und deshalb ist dafür
keine Umgebung vorbereitet.

Die Größe selbst lässt sich nicht nennenswert drücken: Arduino-Core, WLAN,
lwIP, Mbed TLS, der asynchrone Webserver und der KNX-Stack machen den
Löwenanteil aus, und keiner davon ist optional.

### Bewusst *nicht* gesetzte Build-Flags

| Flag | Warum nicht |
| --- | --- |
| `-DNCN5120` | Der LPC1115 emuliert einen TP-UART 2. Mit diesem Flag erwartet der Stack `U_Configure`, Marker-Mode, Baudratenwechsel auf 38400 und die NCN-Analogregister – nichts davon existiert. |
| `-DKNX_BAUDRATE` | Bleibt beim TP-UART-2-Standard 19200. |

### Tunnelanzahl

`-DKNX_TUNNELING=10` entspricht ip4knx. Jede Tunnelverbindung braucht eine
eigene physikalische Adresse; solange das Gerät unprogrammiert ist, leitet der
Stack sie aus dem eigenen Subnetz ab (`15.15.1` … `15.15.10`). Der Aufwand liegt
bei ~24 Byte RAM und 7 Byte Property-Daten je Tunnel – die praktische Grenze ist
der Adressbereich, nicht der ESP32.

---

## Firmware auf den ESP32 bringen

### Über USB (Erstinbetriebnahme)

Board anschließen, Port prüfen, flashen:

```
pio device list
pio run -e esp32dev -t upload
pio device monitor
```

PlatformIO wählt den Port selbst, wenn nur ein Gerät angeschlossen ist; sonst
`--upload-port COM5` bzw. `/dev/ttyUSB0` anhängen oder `upload_port` in
[platformio.ini](platformio.ini) setzen.

Bleibt der Chip stumm (`Failed to connect`), BOOT-Taste gedrückt halten,
EN/RST kurz drücken, BOOT loslassen. Boards mit nativem USB (C3, C6, S2, S3)
melden sich nach dem Flashen mit einer neuen Port-Nummer.

### Manuell mit esptool

Nötig etwa zum Flashen ohne Toolchain oder für vorgefertigte Images. Es sind
**vier** Dateien – `boot_app0.bin` gehört dazu, sonst startet das Gerät nach
dem ersten OTA-Update in die falsche Partition:

```bash
# Linux/macOS
esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 460800 \
  --before default-reset --after hard-reset \
  write-flash -z --flash-mode dio --flash-freq 40m --flash-size detect \
  0x1000  .pio/build/esp32dev/bootloader.bin \
  0x8000  .pio/build/esp32dev/partitions.bin \
  0xe000  ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
  0x10000 .pio/build/esp32dev/firmware.bin
```

```powershell
# Windows (PowerShell) - Backtick als Zeilenfortsetzung
$fw = "$env:USERPROFILE\.platformio\packages\framework-arduinoespressif32"
esptool.py --chip esp32 --port COM5 --baud 460800 `
  --before default-reset --after hard-reset `
  write-flash -z --flash-mode dio --flash-freq 40m --flash-size detect `
  0x1000  .pio\build\esp32dev\bootloader.bin `
  0x8000  .pio\build\esp32dev\partitions.bin `
  0xe000  "$fw\tools\partitions\boot_app0.bin" `
  0x10000 .pio\build\esp32dev\firmware.bin
```

Liegt `esptool.py` nicht im Pfad, steckt es in der PlatformIO-Umgebung:
`~/.platformio/packages/tool-esptoolpy/esptool.py` – dann mit dem Python aus
`~/.platformio/penv` aufrufen.

Der Bootloader-Offset ist chipabhängig, alles andere ist identisch:

| Ziel | `--chip` | bootloader.bin | partitions.bin | boot_app0.bin | firmware.bin |
| --- | --- | --- | --- | --- | --- |
| `esp32dev` | `esp32` | **0x1000** | 0x8000 | 0xe000 | 0x10000 |
| `esp32s2` | `esp32s2` | **0x1000** | 0x8000 | 0xe000 | 0x10000 |
| `esp32c3` | `esp32c3` | **0x0** | 0x8000 | 0xe000 | 0x10000 |
| `esp32c6` | `esp32c6` | **0x0** | 0x8000 | 0xe000 | 0x10000 |
| `esp32s3` | `esp32s3` | **0x0** | 0x8000 | 0xe000 | 0x10000 |

Den exakten Aufruf für ein Ziel zeigt `pio run -e <ziel> -t upload -v`.

### Danach: nur noch `firmware.bin`

Ist das Gerät im Netz, läuft jedes weitere Update über das Dashboard oder die
REST-API – dann wird ausschließlich `firmware.bin` gebraucht, und der
Bootloader-Rollback fängt ein defektes Image auf. Siehe
[Firmware-Update und Integrität](#firmware-update-und-integrität).

---

## Inbetriebnahme

1. Flashen, ESP32 mit Strom versorgen.
2. Ohne gespeicherte Zugangsdaten öffnet das Gerät sofort einen **offenen
   Access Point** `SB-IP AP xxxx`. Verbinden → Captive Portal erscheint
   (sonst `http://192.168.4.1`).
3. Netzwerk auswählen, Passwort eingeben, verbinden. Das Gerät startet neu.
4. Dashboard unter `http://sbip.local` oder der DHCP-Adresse.
5. In der ETS als KNXnet/IP-Schnittstelle programmieren.

**AP erzwingen:** Taster halten, der auf *WLAN Grundeinstellung* zugeordnet
ist – in der Vorgabe der Boot-Taster, ab 2 s. Siehe *Taster und LEDs*.

---

## Improv-WLAN-Provisionierung

[Improv Wi-Fi](https://www.improv-wifi.com/) ist ein **offener Standard** von
ESPHome und Home Assistant, entwickelt und finanziert von Nabu Casa – keine
Eigenentwicklung des ip4knx-Autors. Übernommen unter anderem von WLED, Tasmota,
ESPHome und ESP Web Tools.

Prinzip: Der Browser überträgt die WLAN-Zugangsdaten direkt über die serielle
Verbindung (Web Serial API) oder über Bluetooth LE an das Gerät. Das umgeht die
bekannten Schwächen des Soft-AP-Verfahrens – Smartphones mögen Access Points
ohne Internetzugang nicht und wechseln oft eigenmächtig ins Mobilfunknetz
zurück.

Diese Firmware nutzt die **serielle** Variante. In den ersten 120 s nach dem
Start nimmt sie Zugangsdaten von [improv-wifi.com](https://www.improv-wifi.com/)
oder aus ESP Web Tools entgegen. Der Captive-Portal-Weg bleibt parallel nutzbar.

Verwendet wird die Upstream-Bibliothek
[jnthas/Improv-WiFi-Library](https://github.com/jnthas/Improv-WiFi-Library).
ip4knx greift hier auf einen eigenen Fork zurück, weil dessen
`ImprovTypes::ChipFamily` um `CF_ESP32_C6` erweitert wurde. Da die Chip-Familie
nur der Anzeige im Browser dient, meldet sich der C6 hier schlicht als `ESP32` –
das spart die Fork-Abhängigkeit ohne funktionalen Nachteil.

Abschaltbar mit `-DDISABLE_IMPROV` (spart rund 9 KB Flash und 2 s Bootzeit).

### Gerätename

Enthält eine Anlage mehrere dieser Router, unterscheidet sie der Gerätename.
Er ist zugleich der **mDNS-Hostname** (`<name>.local`) und steht als zweite
Zeile im Protokoll. Erlaubt sind 1 bis 31 Zeichen aus `A-Z a-z 0-9 -`, nicht
mit Bindestrich beginnend oder endend – die Beschränkung kommt daher, dass
der Name als Hostname endet. Ohne eigene Angabe gilt `SBIP_MDNS_HOSTNAME`.

> Der Name im **KNXnet/IP-Discovery** ist ein anderer: Den schreibt die ETS
> beim Download in das Gerät (bei der ABB-Vorlage etwa
> `IPR/S3.1.1 IP-Router,REG`). Solange nicht programmiert wurde, ist dort der
> Produktname der Firmware zu sehen. Der Gerätename hier ändert daran nichts.
> Angezeigt wird er als *Name in der ETS* und in der Kopfzeile.

### Zugangsschutz

Optional, standardmäßig aus. Ist ein Benutzername mit Passwort gesetzt,
verlangt **jede** Seite und jeder Endpunkt eine Anmeldung nach
**Digest**-Verfahren (`AsyncAuthenticationMiddleware`).

Was das leistet und was nicht: Das Passwort geht **nicht im Klartext** über
die Leitung – anders als bei Basic-Auth. Alles andere schon. Ohne TLS, und
HTTPS ist hier bewusst nicht umgesetzt, schützt das vor unbefugtem Zugriff
durch andere im selben Netz, **nicht** vor jemandem, der den Verkehr
mitschneidet. Für ein Gerät, das eine KNX-Anlage bedient, ist das der
Unterschied zwischen offen und nicht offen.

Gespeichert wird nur der Digest-Hash über Benutzername, Realm und Passwort,
nicht das Passwort selbst. Der Realm ist Teil des Hashes – wird er geändert,
sind alle Passwörter ungültig.

Zwei Dinge sind ausgenommen:

* Der **Provisioning-Accesspoint**. Dort ist noch nichts eingerichtet, und
  eine Passwortabfrage vor der Einrichtungsseite ist der sicherste Weg, sich
  auszusperren.
* Ein **vergessenes Passwort** lässt sich nur über *Werkeinstellungen*
  zurücksetzen.

### Ohne Rückweg kein Passwort

Ein Passwort lässt sich deshalb **nur setzen, wenn ein Taster auf
*Werkeinstellungen* liegt** – siehe *Taster und LEDs*. Ohne einen solchen
Taster wäre die einzige Antwort auf ein vergessenes Passwort ein USB-Kabel und
`erase_flash`, und das ist kein Rettungsweg für ein Gerät im Verteiler.

Umgekehrt gilt dasselbe: Solange ein Passwort gesetzt ist, weist das
Hardware-Profil eine Änderung ab, die diesen Taster entfernen würde. Geprüft
wird das in `applyJson()`, nicht in `validate()` – ein bereits gespeichertes
Profil aus der Zeit vor dieser Regel muss weiterhin starten können.

Die Vorgabe enthält **keinen** solchen Taster. Wer ein Passwort setzen will,
legt ihn zuerst an – ein sehr langer Druck auf den vorhandenen Taster ist
dafür die naheliegende Wahl.

Ein separates API-Token gibt es bewusst nicht: Es wäre genauso mitlesbar wie
das Passwort und ein zweiter Ort für Fehler.

---

## REST-API

| Methode | Pfad | Wirkung |
| --- | --- | --- |
| GET | `/api/status` | vollständiges Statusdokument |
| GET | `/api/wifi/scan[?start=1]` | asynchroner Netzwerk-Scan |
| POST | `/api/wifi/connect` | `ssid`, `password` → speichern + Neustart |
| POST | `/api/wifi/ap_mode` | Zugangsdaten löschen, AP starten |
| POST | `/api/wifi/fallback` | `enabled=1\|0` → WLAN-Ersatzverbindung |
| POST | `/api/name` | `name=` → Geräte- und mDNS-Name |
| POST | `/api/progmode` | `state=on\|off\|toggle` |
| GET | `/api/hwconfig` | aktives, gespeichertes und Image-Profil |
| POST | `/api/hwconfig` | JSON-Profil speichern (Teilfelder erlaubt) |
| POST | `/api/hwconfig/reset` | gespeichertes Profil verwerfen |
| POST | `/api/peaks/reset` | `scope=bus\|cpu\|all` → Spitzenwertmarker löschen |
| POST | `/api/hours/reset` | Betriebsstunden und Startzähler auf null |
| POST | `/api/led/heartbeat` | `enabled=1\|0` → Herzschlag schalten |
| POST | `/api/led/brightness` | `percent=1..100` → Helligkeit aller LEDs |
| GET | `/api/partitions` | Partitionstabelle des Flash |
| GET | `/api/log` | Protokollpuffer als Text, `?bytes=` |
| POST | `/api/log/clear` | Protokollpuffer leeren |
| GET | `/api/log/reset` | was den letzten Neustart überdauert hat |
| POST | `/api/log/keep` | `enabled=1\|0` → RTC-Mitschrift |
| GET | `/api/monitor` | Zustand des Busmonitors |
| POST | `/api/monitor/start` | `sides=tp,ip`, `trigger_mode=now\|ga\|repeat\|secure`, `trigger=`, `pre=`, `post=`, `stop_full=1\|0` |
| POST | `/api/monitor/stop` | Aufzeichnung anhalten |
| POST | `/api/monitor/clear` | Ring leeren |
| GET | `/api/monitor/frames` | Ausschnitt, `?from=` und `?max=` |
| POST | `/api/auth` | `user=`, `password=` → Zugangsschutz |
| POST | `/api/reboot` | Neustart auslösen |
| POST | `/api/factory` | **gesamte NVS-Partition löschen** und neu starten |
| GET | `/api/time` | Zustand und Konfiguration des Zeitservers |
| POST | `/api/time/config` | Konfiguration schreiben (Teilfelder erlaubt) |
| POST | `/api/time/set` | `epoch` (UTC-Sekunden) → Uhr stellen |
| POST | `/api/time/send` | Zeittelegramme sofort senden |
| POST | `/api/ota` | Multipart `firmware`, optional Header `X-SHA256` |
| GET | `/api/update/check` | Manifest prüfen (asynchron) |
| POST | `/api/update/install` | Online-Update installieren |
| GET | `/api/update/status` | Fortschritt des Updates |
| GET | `/api/lpc` | Zustand des SB-Interface und des laufenden Auftrags |
| POST | `/api/lpc/probe` | LPC erkennen (asynchron) |
| POST | `/api/lpc/upload` | Multipart `firmware`, Intel-Hex oder Binär |
| POST | `/api/lpc/fetch` | Datei aus dem Manifest holen und prüfen |
| POST | `/api/lpc/write` | die hochgeladene Datei schreiben (asynchron) |
| POST | `/api/lpc/run` | LPC ins Anwendungsprogramm zurücksetzen |

Alle `POST`-Endpunkte sind Origin-geprüft und im AP-Modus gesperrt (Ausnahme:
`/api/wifi/connect`, sonst wäre keine Einrichtung möglich).

### Was die Endpunkte annehmen

Jeder Wert, der in einen Puffer fester Größe, in eine Umgebungsvariable oder
ins Protokoll wandert, wird vorher geprüft – nicht nur abgeschnitten:

| Feld | Regel | Bei Verstoß |
|---|---|---|
| `name` (Gerätename) | 1–31 Zeichen `A-Z a-z 0-9 -` | 400 |
| `user` (Zugangsschutz) | ≤ 31 Zeichen, druckbares ASCII, kein `:` | 400 |
| `password` | 8…128 Zeichen | 400 |
| `tz` | druckbares ASCII, 1…`sizeof(tz)-1` | 400 |
| `ntp_server` | druckbares ASCII, 1…`sizeof(ntpServer)-1` | 400 |
| `interval_min` | auf 1…10080 geklemmt | stillschweigend geklemmt |
| `epoch` | Plausibilitätsfenster im `TimeService` | 400 |
| `percent` (LED) | 1…100 | 400 |
| `bytes` (`/api/log`) | auf die Ringgröße geklemmt | geklemmt |
| Profilnamen (JSON) | `A-Z a-z 0-9 _ -`, Länge `HW_NAME_MAX` | Profil abgewiesen |
| Dateinamen aus Multipart | nur fürs Protokoll, dort auf druckbares ASCII gefiltert | – |

**Warum druckbares ASCII und nicht nur Kürzen:** `tz` landet in
`setenv()`/`tzset()`, `ntp_server` beim Resolver, und beide stehen zugleich im
Protokoll. Ein CR oder LF darin ließe einen Aufrufer eigene Protokollzeilen
fälschen – der billigste Weg, eine Spur zu verwischen.

---

## Zeitserver

Verteilt Datum und Uhrzeit auf dem Bus. Konfiguration vollständig über das
Dashboard, kein ETS nötig.

### Datenpunkttypen

| DPT | Länge | Inhalt |
|---|---|---|
| 19.001 | 8 Byte | Datum + Uhrzeit + Statusflags |
| 10.001 | 3 Byte | Uhrzeit + Wochentag |
| 11.001 | 3 Byte | Datum |

Alle drei sind unabhängig konfigurierbar (leere Gruppenadresse = aus). Viele
ältere Geräte verstehen nur 10.001/11.001 – deshalb parallel betreibbar.

### Zeitquellen

| Quelle | Genauigkeit | Priorität |
|---|---|---|
| NTP | ~ms, driftfrei | höchste, sobald erreichbar |
| RV-3028-C7 | ±1 ppm ≈ ±30 s/Jahr | zweite, läuft ohne Netz |
| Browser | ~1 s | manuell |
| Manuelle Eingabe | Eingabegenauigkeit | manuell |
| Übernommen | Drift seit dem letzten echten Abgleich | nur als Zwischenzustand |

Ohne Internetzugang – der Regelfall in einem abgeschotteten KNX-IP-Netz – NTP
abschalten und die Zeit per Browser oder manuell setzen. Mit bestücktem
RV-3028 überlebt sie den nächsten Stromausfall; ohne RTC muss sie nach jedem
Neustart neu gesetzt werden.

### „Übernommen“ – warum nach einem Neustart eine Zeit dasteht

Ein **Software**-Neustart löscht die Systemuhr nicht. `esp_timer` hält seinen
Startversatz im RTC-Slow-Speicher, also läuft `time()` nach einem Watchdog,
einer Panik oder `ESP.restart()` einfach weiter – ohne bestückte RTC und vor
der ersten NTP-Antwort. Das Protokoll stempelt seine Zeilen dann mit einer
Uhrzeit statt mit `+HH:MM:SS`, weil die Uhr eben nicht ungültig ist.

Das Dashboard weist diesen Zustand als eigene Quelle mit **gelbem** Punkt aus.
Die Alternativen wären beide schlechter: „nicht gesetzt“ neben einer
plausiblen Uhrzeit anzuzeigen, oder den Wert wegzuwerfen und damit eine
Information zu vernichten, die im Zweifel auf Sekunden stimmt. Nach einem
**Kaltstart** oder nach dem Flashen über esptool ist der Versatz weg und die
Uhr steht wieder auf „nicht gesetzt“.

### Die Zeitzone muss vor der ersten Logzeile stehen

`LogBuffer::makeStamp()` stempelt mit `localtime_r()`, also mit **Ortszeit**.
Solange `setenv("TZ", …)` und `tzset()` nicht gelaufen sind, gilt UTC – und in
dem Moment, in dem sie laufen, springt das Protokoll um den vollen Versatz
nach vorn. In Mitteleuropa sind das zwei Stunden mitten in der Startsequenz.

Deshalb ist `applyTimezone()` von `begin()` getrennt und steht in `setup()`
direkt hinter `hwConfig.begin()`: NVS ist dann oben, aber noch nichts hat eine
gestempelte Zeile geschrieben. `begin()` ruft dieselbe Funktion später noch
einmal auf, wenn die übrige Konfiguration an der Reihe ist.

### Zeitzone einstellen

Die Firmware speichert eine **POSIX-TZ-Regel**, weil die C-Bibliothek des
ESP32 nur diese versteht – eine Zonendatenbank wie `Europe/Berlin` gibt es
dort nicht. Das Dashboard bietet dazu eine Auswahlliste; wer eine Zone
braucht, die nicht darin steht, wählt *eigene Angabe* und trägt die Regel
selbst ein.

Die Regel hat vier Teile:

```
CET-1CEST,M3.5.0,M10.5.0/3
│   ││   │ │     │
│   ││   │ │     └─ Ende:    letzter Sonntag im Oktober, 3 Uhr
│   ││   │ └─────── Beginn:  letzter Sonntag im März
│   │└───┴───────── Kürzel der Sommerzeit
│   └────────────── Versatz der Normalzeit
└────────────────── Kürzel der Normalzeit
```

**Das Vorzeichen ist umgekehrt.** `CET-1` bedeutet UTC+1, nicht UTC−1: der
Wert nennt, was zur Ortszeit addiert UTC ergibt. `M3.5.0` liest sich als
Monat 3, Woche 5, Tag 0 – Woche 5 heißt „die letzte“, Tag 0 heißt Sonntag.
Ohne `/3` gilt 2 Uhr Ortszeit.

### Warum die RTC auch im laufenden Betrieb gebraucht wird

Die ESP32-Systemuhr hängt im Normalbetrieb am APB-Takt, spezifiziert mit
**±10 ppm** – rund **26 s pro Monat**. Für einen Zeitserver ist das zu viel.
Der RV-3028-C7 ist temperaturkompensiert und liegt bei ±1 ppm, also etwa
30 s pro Jahr.

Daraus ergibt sich eine wechselseitige Nachführung:

| Situation | Richtung | Intervall |
|---|---|---|
| NTP synchron | System → RTC | 1 h und bei jedem Sync |
| Kein NTP, RTC vorhanden | RTC → System | 15 min |
| Zeit manuell gesetzt | System → RTC | sofort |

Beim Zurückholen wird nur gestellt, wenn die Abweichung über einer Sekunde
liegt – sonst käme sich das ständige `settimeofday()` mit dem SNTP-Client ins
Gehege.

**Erkennung:** Die RTC wird beim Start gesucht und, falls nicht gefunden, alle
30 s erneut – ein nachträglich gestecktes Modul wird also ohne Neustart
übernommen.

### NTP-Server: manuell oder per DHCP

Beide Wege sind im Dashboard wählbar. Bei aktiviertem DHCP-Bezug landet der
vom Server gelieferte Eintrag in Slot 0, der konfigurierte bleibt als Fallback
in Slot 1.

> **Einschränkung:** DHCP-Option 42 erreicht die Firmware nur, wenn der
> lwIP-Build mit `CONFIG_LWIP_DHCP_GET_NTP_SRV` übersetzt wurde. Die
> vorkompilierten Arduino-ESP32-Bibliotheken haben das **abgeschaltet**.
> Der Code prüft das zur Übersetzungszeit (`#if LWIP_DHCP_GET_NTP_SRV`),
> gibt sonst einen Hinweis auf der seriellen Konsole aus und nutzt den
> konfigurierten Server. Welcher Server tatsächlich aktiv ist, zeigt das
> Dashboard – mit dem Zusatz `(DHCP)`, wenn der Bezug wirklich greift.
>
> Wer die Funktion braucht, muss gegen ESP-IDF mit gesetzter Option bauen.

### RV-3028-C7 (optional)

I2C, feste Adresse `0x52`. Pins und Ein/Aus über das Hardware-Profil im
Dashboard; `-DSBIP_I2C_SDA_PIN` / `-DSBIP_I2C_SCL_PIN` / `-DSBIP_I2C_ENABLED`
legen nur die Vorgabe fest.

Zwei Punkte, die in der Praxis Ärger machen:

* **Backup-Umschaltung ist ab Werk deaktiviert.** Ohne Aktivierung tut eine
  bestückte Batterie oder ein Goldcap gar nichts – der Baustein bleibt bei
  VDD-Verlust einfach stehen. [src/rv3028.cpp](src/rv3028.cpp) setzt bei jedem
  Start `BSM` im Konfigurationsregister. Bewusst nur im RAM-Spiegel, nicht im
  EEPROM: Der Spiegel hängt an der gepufferten Versorgung und überlebt genau
  den Fall, um den es geht – ohne EEPROM-Verschleiß.
* **Das PORF-Bit** meldet, ob die Zeit einen Spannungsverlust überstanden hat.
  Der Treiber liefert nur dann eine Zeit, wenn sie vertrauenswürdig ist –
  statt stillschweigend das Jahr 2000 auszugeben.

Der Trickle-Charger ist standardmäßig **aus**. Für einen Goldcap in
`Rv3028::begin()` z. B. `RV3028_TRICKLE_3K` setzen. Mit einer nicht
wiederaufladbaren Batterie muss er aus bleiben.

### Statusflags in DPT 19.001

Das Bit **CLQ** (Oktett 7, Bit 7) sagt aus, ob die Uhr extern synchronisiert
ist. Manche Geräte verwerfen Telegramme mit gelöschtem CLQ. Hier wird es
gesetzt, wenn NTP synchronisiert ist – und sonst eben nicht. Bei manuell
gesetzter Zeit bleibt es also bewusst auf 0.

Das Sommerzeit-Bit **SUTI** folgt der POSIX-TZ-Regel
(`CET-1CEST,M3.5.0,M10.5.0/3` für Mitteleuropa). Die Umstellung wird nicht
selbst berechnet.

### Gruppentelegramme ohne Gruppenobjekte

Ein Koppler der Maske 091A hat keine Kommunikationsobjekte – der übliche Weg
über die Applikationsschicht existiert nicht. `KnxLink::sendGroupValue()`
übergibt den Frame deshalb direkt an beide Data-Link-Layer:

* **TP-Layer** → TP1-Medium *und* alle offenen Tunnel
* **IP-Layer** → Routing-Multicast

Bewusst **nicht** über `dataRequestFromTunnel()`: Der Weg läuft durch
`frameReceived()`, das bei einer Quelladresse gleich der eigenen sofort
`individualAddressDuplication()` setzt – genau der Fall beim Eigensenden.

---

## Firmware-Update und Integrität

### SHA-256 statt MD5

MD5 ist seit 2004 nicht mehr kollisionsresistent und für Integritätszwecke
überholt. Verwendet wird deshalb **SHA-256**, berechnet über
[src/fw_hash.cpp](src/fw_hash.cpp) mit mbedtls.

Der ESP32 hat einen **SHA-Hardwarebeschleuniger** (SHA-1/224/256/384/512).
mbedtls nutzt ihn automatisch, sofern `CONFIG_MBEDTLS_HARDWARE_SHA` gesetzt ist
– in den Standard-Arduino-Builds der Fall. Das Hashen fällt neben dem
Flash-Schreiben nicht ins Gewicht.

Warum nicht `Update::setMD5()`? Arduinos `Update`-Klasse kennt ausschließlich
MD5. Der Digest wird daher parallel zum Schreiben selbst gebildet und
**vor** `Update.end()` geprüft – erst dieser Aufruf schaltet die
Boot-Partition um. Bei Abweichung folgt `Update.abort()`, die laufende
Firmware bleibt unangetastet.

| Weg | Header / Feld | Verhalten |
|---|---|---|
| Datei-Upload | `X-SHA256` | 64 Hex-Zeichen, bevorzugt |
| Datei-Upload | `X-MD5` | Legacy, nur falls kein SHA-256 |
| Online-Update | `sha256` im Manifest | **Pflicht**, sonst wird abgelehnt |

Prüfsumme erzeugen:

```
sha256sum .pio/build/esp32dev/firmware.bin      # Linux/macOS
Get-FileHash firmware.bin -Algorithm SHA256     # PowerShell
```

> **Browser-Einschränkung:** Das Dashboard berechnet den Hash automatisch,
> aber `crypto.subtle` steht nur im *secure context* zur Verfügung – also über
> HTTPS oder `localhost`. Beim üblichen Zugriff über `http://<IP>/` fehlt die
> API. Dann bleibt das Eingabefeld für den manuell ermittelten Hash; das
> Dashboard weist darauf hin.

### Was das leistet – und was nicht

Die Prüfsumme sichert gegen **Übertragungsfehler und abgeschnittene Uploads**.
Sie ist **kein Schutz gegen einen Angreifer**: Wer die Firmware-Datei
manipulieren kann, ändert die Prüfsumme gleich mit.

Echte Authentizität bietet nur eine Signatur. Zwei Wege:

* **ESP32 Secure Boot v2** (RSA-3072 oder ECDSA im Bootloader). Der stärkste
  Schutz, aber über eFuses **unwiderruflich** aktiviert – ein Fehler beim
  Einrichten macht das Gerät dauerhaft unbrauchbar.
* **`UPDATE_SIGN`** in Arduinos `Update`-Klasse. Prüft eine angehängte
  Signatur vor dem Aktivieren der Partition. Deutlich weniger invasiv, braucht
  aber eine eigene Schlüsselverwaltung.

Beides ist hier bewusst nicht umgesetzt: Das Interface ist für ein lokales
KNX-Netz gedacht, und die schreibenden Endpunkte sind bereits Origin-geprüft
und im AP-Modus gesperrt. Wer die Firmware über ein nicht vertrauenswürdiges
Netz verteilt, sollte Secure Boot v2 vorsehen.

### Zwischen den beiden Slots wechseln

Im Dashboard unter *System → Partitionstabelle* steht neben jedem Slot, welche
Firmware darin liegt und in welchem **OTA-Zustand** er ist. *Umschalten* setzt
die Startpartition und startet neu.

Der Bootloader hat den Rollback aktiviert (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`),
und das hat eine Falle, die man kennen muss:

| Zustand | Bedeutung |
| --- | --- |
| `geprüft` | Der Slot ist freigegeben. |
| `auf Bewährung` | Läuft gerade zur Probe. **Ein Reset jetzt verwirft ihn.** |
| `ungültig`, `abgebrochen` | Vom Bootloader dauerhaft gesperrt. |

Nach einem Wechsel startet der neue Slot in *auf Bewährung*. Kommt in dieser
Zeit ein Reset, bevor die Firmware sich freigegeben hat, schaltet der
Bootloader zurück und markiert den Slot als **ungültig** – ab dann lässt er
sich nicht mehr anwählen, obwohl das Abbild unversehrt im Flash liegt. Genau
das sieht wie „Umschalten bewirkt nichts" aus.

Deshalb gibt die Firmware sich nach einem **bewusst gewählten** Wechsel sofort
frei statt nach den 30 Sekunden, die für ein frisches OTA-Image gelten: Der
Slot lief hier schon einmal, es gibt nichts zu beweisen. Der Merker dafür
liegt in NVS und überlebt genau einen Neustart.

Ein bereits gesperrter Slot lässt sich nur durch einen neuen Upload
zurückholen; der Knopf ist dann abgeschaltet und nennt den Grund.

---

## Konfiguration über die ETS

**Kurz: nur eingeschränkt, und ein knxprod bringt hier wenig.**

Was **ohne** Produktdatenbank funktioniert, weil es über Standard-Properties
läuft:

* Physikalische Adresse des Interfaces vergeben
* Zusätzliche Tunnel-Adressen setzen (ETS: *Bus → Schnittstellen*, geht über
  `PID_ADDITIONAL_INDIVIDUAL_ADDRESSES`)
* Als Schnittstelle für Inbetriebnahme und Diagnose verwenden

### Adressen vergeben, ohne Gerät im Projekt

Zuständig ist der **Verbindungsmanager**, erreichbar über das Aufklappmenü in
der **Hauptsymbolleiste**. Nicht über ein Panel, nicht über die Statuszeile –
und die Menüleiste erscheint erst, wenn ein Projekt geöffnet ist.

1. Aufklappmenü öffnen. In der Mitte stehen die gefundenen Schnittstellen; das
   Interface meldet sich per Multicast selbst an.
2. Auf das **Zahnradsymbol** neben dem Eintrag klicken.
3. Rechts erscheinen die Eigenschaften.

Dort gibt es zwei Adressfelder:

| Feld | Bedeutung |
| --- | --- |
| *Host Individual Address* | Adresse des Geräts selbst – nur Anzeige |
| *Individual Address* | Adresse der genutzten Tunnelverbindung |

Das zweite Feld ist editierbar, weil dieses Gerät ein Plain-Device ohne KNX
Secure ist; bei secure-fähigen Geräten ginge es nur im Projekt. Genau dieses
Feld schreibt in `PID_ADDITIONAL_INDIVIDUAL_ADDRESSES`. Empfehlung der
KNX-Doku: Bereich und Linie an den Einbauort anpassen, als Gerätenummer eine
im Projekt unbenutzte wählen – typischerweise 255.

Ein roter Punkt neben dem Symbol zeigt an, dass die Schnittstelle im
Programmiermodus ist (Aktualisierung etwa alle 3 s). Praktisch, um den
Dashboard-Knopf gegenzuprüfen.

Zwei Dinge gehen dort **nicht**:

* Die Geräteadresse (*Host Individual Address*) ändern – das setzt einen
  Projekteintrag und damit eine Produktdatenbank voraus.
* Ein Dummy-Gerät als Ersatz nehmen – die ETS lädt in ein Dummy grundsätzlich
  nichts. Es taugt nur als Platzhalter in der Topologie, damit die Adresse
  nicht ein zweites Mal vergeben wird.

Der Diagnose-Dialog *Physikalische Adressen* vergibt ebenfalls keine Adressen.
Er findet nur Geräte im Programmiermodus, prüft die Erreichbarkeit einer
Adresse und scannt Linien.

Auf die Reihenfolge achten: Der Stack leitet die Tunnel-Adressen beim ersten
Verbindungsaufbau **einmalig** aus der Geräteadresse ab und legt sie in
`PID_ADDITIONAL_INDIVIDUAL_ADDRESSES` ab (`ip_data_link_layer.cpp`). War das
Gerät dabei noch unprogrammiert, bleiben dort dauerhaft `15.15.x` stehen; eine
spätere Programmierung zieht nicht nach. Die Adressen im Verbindungsmanager
explizit zu vergeben löst das unabhängig von der Geräteadresse und überlebt
jede Neuprogrammierung. Notfalls hilft *KNX zurücksetzen* im Dashboard.

Was ein knxprod bräuchte: Filtertabelle, Routing-Parameter des Kopplers und
sämtliche anwendungsspezifischen Einstellungen – hier also Zeitserver und
Gruppenadressen.

### Ohne knxprod kein Routing

Das ist keine Feinheit, sondern die Grenze zwischen Schnittstelle und Koppler.
`RouterObject::isGroupAddressInFilterTable()` beginnt mit

```cpp
if (loadState() != LS_LOADED)
    return false;
```

und der Ladezustand wird nur durch einen vollständigen ETS-Download gesetzt.
Ohne Produktdatenbank verwirft der Koppler daher **jedes** Gruppentelegramm
zwischen TP1 und IP-Multicast, obwohl `-DKNX_ROUTING` einkompiliert ist. Die
Kopplerparameter `PID_MAIN_LCCONFIG` und `PID_SUB_LCCONFIG` sind ebenfalls
reine Download-Inhalte.

Tunneling ist davon nicht betroffen und funktioniert unprogrammiert.

### Alle Gruppentelegramme weiterleiten

Ein Koppler ohne Filtertabelle sperrt jedes Gruppentelegramm – laut Norm
richtig, und für jemanden, der schlicht eine IP-Schnittstelle will, unbrauchbar.
Deshalb entscheidet `KnxLink::applyRouting()` das automatisch:

| Zustand | Was gilt |
|---|---|
| noch kein ETS-Download | alles wird weitergeleitet |
| ETS hat programmiert | die geladene Filtertabelle entscheidet |
| Schalter eingeschaltet | alles wird weitergeleitet, auch mit Tabelle |

Der Übergang passiert von selbst: `superviseRouting()` sieht sekundenweise nach,
ob sich `knx.configured()` geändert hat, und zieht nach. Das gilt in beide
Richtungen – wer die ETS-Programmierung löscht, bekommt die Weiterleitung
zurück. Der Schalter auf der Startseite (`POST /api/knx/routing`,
`unfiltered=1`) bleibt als **Übersteuerung** darüber erhalten und wird
gespeichert.

Technisch setzt das `sbipRouteUnfiltered` und lässt
`isGroupAddressInFilterTable()` **vor** der Prüfung des Ladezustands `true`
zurückgeben. Der Patch dazu steht in
[scripts/patch_knx.py](scripts/patch_knx.py).

Das Statusdokument nennt beides: `knx_route_all` ist der Schalter,
`knx_route_all_active` das, was der Stack tatsächlich tut.

Der Schalter ist im Dashboard mit einer Rückfrage versehen: Wer die Filterung
projektiert hat, will sie normalerweise auch. Nur einschalten, wenn dieses Gerät
die einzige Verbindung zwischen Linie und IP ist – zwei Koppler ohne Filter
erzeugen Telegrammschleifen.

### „Mehr als ein Gerät im Programmiermodus"

Beim Vergeben der physikalischen Adresse meldete die ETS das, obwohl nur
dieses eine Gerät im Programmiermodus war. Es war auch nur eines – die ETS
bekam die Antwort mehrfach.

**Die Messung.** Der Busmonitor zeigt es beim Programmieren:

| ms | Seite | Ri. | Quelle | Dienst | Hop |
| --- | --- | --- | --- | --- | --- |
| 443481 | Tunnel | TX | 1.1.5 | IndividualAddressRead | 6 |
| 443482 | IP | TX | **15.15.0** | IndividualAddressResponse | 6 |
| 443483 | TP | TX | **15.15.0** | IndividualAddressResponse | 6 |
| 443540 | **TP** | **RX** | **1.1.255** | **IndividualAddressResponse** | **4** |
| 443541 | Tunnel | TX | 1.1.255 | IndividualAddressResponse | 4 |

Zeile 2 und 3 sind die eigene Antwort, abgesendet als `15.15.0`. Zeile 4 ist
dieselbe Antwort, 58 ms später **von der Busleitung** zurück – aber unter der
Quelladresse `1.1.255`. Über Zeile 5 geht sie an die ETS.

Die ETS bekommt also zwei `IndividualAddressResponse` von zwei verschiedenen
Absendern und schließt daraus: **mehr als ein Gerät im Programmiermodus.**

**Wer ist `1.1.255`?** Eine ersetzte Quelladresse ist die Signatur eines
KNXnet/IP-Interfaces im Tunnelbetrieb – es trägt seine eigene Tunneladresse
ein. Auf derselben TP-Linie hängt demnach ein **zweites Gerät**, das die Linie
mit demselben IP-Netz verbindet, den Routing-Multicast aufnimmt und auf den
Bus legt. Der Zeitversatz von 30–60 ms ist die Übertragungsdauer auf TP1, die
fehlenden Hop-Zähler stammen von diesem Gerät.

**Das ist kein Fehler dieser Firmware, sondern der Anlage.** Zwei ungefilterte
Koppler zwischen einer Linie und einem IP-Netz erzeugen genau das; der
Hop-Zähler begrenzt die Schleife nur. Abhilfe: das zweite Interface während
der Programmierung vom Netz nehmen, oder so projektieren, dass nur eines von
beiden koppelt.

**Die Firmware sagt es jetzt selbst.** `BusMonitor::watchForLoop()` merkt sich
Ziel und Nutzlast jedes gesendeten Telegramms und vergleicht sie mit dem, was
kurz darauf von der Busleitung hereinkommt. Stimmt alles überein außer der
Quelladresse, steht das im Protokoll:

```
KNX: telegram loop - 7 frame(s) we sent came back from the bus as 1.1.255.
A second device bridges this line to the same IP network; ETS will see every
answer twice.
```

Der Hop-Zähler wird bewusst nicht verglichen – den hat das andere Gerät schon
verändert.

> **Warum kein Filter dagegen hilft.** Die zurückkommende Antwort trägt nicht
> mehr unsere Absenderadresse, sondern die des anderen Geräts. Sie ist von
> einem echten Fremdtelegramm nicht zu unterscheiden – wer sie verwirft,
> verwirft irgendwann berechtigten Busverkehr. Deshalb meldet die Firmware den
> Befund, statt ihn zu verstecken.

**Der Stack merkt es und tut nichts.** In `DataLinkLayer::frameReceived()`:

```cpp
if (source == ownAddr)
    _deviceObject.individualAddressDuplication(true);
```

Danach läuft die Verarbeitung ganz normal weiter, inklusive Weiterleitung.

Der siebte Patch in [scripts/patch_knx.py](scripts/patch_knx.py) verwirft
deshalb im **Multicast-Empfangspfad** alles, was von uns stammt – erkennbar an
zwei unabhängigen Merkmalen:

| Merkmal | Bedeutung |
| --- | --- |
| Absender-IP ist die eigene | echtes Socket-Loopback |
| Quelladresse ist die eigene PA | es war unterwegs und kam zurück |

Auf die Byte-Reihenfolge kommt es an: `readBytesMultiCast()` dreht den
Absender mit `htonl()`, `currentIpAddress()` reicht die Netzwerkreihenfolge
von `IPAddress` durch – ein roher Vergleich träfe nie zu.

**Wer die Schleife schließt, sagt das Protokoll.** Der Patch ruft
`sbipLoopHook` auf, definiert in [src/knx_link.cpp](src/knx_link.cpp), und
dort steht höchstens alle zehn Sekunden eine Zeile:

```
KNX: dropped 14 looped telegram(s), last from 192.168.179.7 source 15.15.0 (our address from elsewhere)
```

Sagt sie *our own IP*, spiegelt der eigene Socket. Nennt sie eine **fremde
IP**, gibt es ein zweites Gerät, das dieselbe TP-Linie mit demselben IP-Netz
verbindet – dann ist die Schleife im Anlagenaufbau begründet und nicht in
dieser Firmware. Zwei ungefilterte Koppler zwischen einer Linie und einem
IP-Netz erzeugen sie prinzipbedingt; der Hop-Count begrenzt sie nur.

> **Drei Sackgassen, nicht wiederholen.**
>
> 1. Die Tunnel-Spiegelung in `DataLinkLayer::sendTelegram()` für selbst
>    erzeugte Telegramme unterdrücken. Für einen Tunnel-Client ist sie der
>    **einzige** Weg, auf dem ihn eine Antwort erreicht – ein Tunnel empfängt
>    keinen Routing-Multicast. Die ETS sah das Gerät danach überhaupt nicht
>    mehr im Programmiermodus.
> 2. Nur auf Socket-Loopback setzen. Die gemessenen Hop-Counts widerlegen das:
>    Loopback dekrementiert nicht.
> 3. Dieselbe Prüfung nach `DataLinkLayer::frameReceived()` legen. Das trifft
>    **jeden** Empfangspfad, auch die Tunnel-Zustellung – und ein getunneltes
>    Telegramm muss lokal zugestellt werden, egal welche Adresse es trägt.
>    Danach war das Gerät in den Adaptereinstellungen der ETS nicht mehr
>    erreichbar. Deshalb sitzt die Prüfung jetzt ausschließlich im
>    Multicast-Empfang.

> Die unfilterte Weiterleitung oben ist **nicht** die Ursache – sie wirkt nur
> auf `isGroupAddressInFilterTable()`, und ein Broadcast mit Zieladresse 0
> durchläuft diese Prüfung ohnehin nicht.

### Die Adresse muss auf `.0` enden

**Das Gerät ist ein Koppler, und die ETS darf ihm keine Teilnehmeradresse
geben.** Wird ihm etwa `1.1.5` statt `1.1.0` zugewiesen, findet die ETS es
zwar noch über Broadcasts – der Programmiermodus wird angezeigt –, aber kein
physikalisch adressiertes Telegramm kommt mehr an oder heraus.

Der Grund steht in `NetworkLayerCoupler::evaluateCouplerType()`:

```cpp
if ((_deviceObj.individualAddress() & 0x00FF) == 0x00)
{
    // Device is a router: x.y.0 -> LineCoupler, x.0.0 -> BackboneCoupler
}
else
{
    // Device is not a router, check if TP1 bridge or TP1 repeater
    /*  ... vollständig auskommentiert ...  */
}
```

Der `else`-Zweig ist oben unfertig. `_couplerType` behält dann, was vorher
darin stand, und `routeDataIndividual()` steigt mit
*„unknown coupler type“* wortlos aus – jedes physikalisch adressierte
Telegramm wird verworfen. Broadcasts laufen weiter, weshalb das Gerät
auffindbar bleibt und der Fehler schwer zu sehen ist.

Die Firmware warnt deshalb im Protokoll, sobald die Adresse nicht auf `.0`
endet:

```
KNX: WARNING - address 1.1.5 does not end in .0, so the stack cannot tell
which coupler this is and drops every physically addressed telegram.
```

> Nicht zu verwechseln mit den **Tunnel-Adressen**: Die dürfen und sollen
> Teilnehmeradressen sein (`1.1.5` und so weiter). Nur die **Geräteadresse**
> des Kopplers selbst muss auf `.0` enden.

### Fremde Produktdatenbank verwenden

Statt eine eigene Produktdatenbank zu erstellen, kann sich die Firmware als
vorhandenes Gerät ausgeben, dessen Verhalten sie nachbildet.

Ausgewählt wird das Profil in `platformio.ini`, Abschnitt `[knx_product]` –
es ist genau eine Zeile zu tauschen. Die Werte selbst stehen in
`include/interface_config.h`.

| Profil | Gerät |
| --- | --- |
| `0` | Eigene Kennung, Hersteller `0x00FA`. Nur Tunneling, Basis für ein eigenes knxprod (Kaenx, OpenKNXproducer). |
| `1` | ABB i-bus KNX IP-Router **IPR/S 3.1.1**, Applikation *IP-Router/2.0a* |

Die ABB-Applikation passt, weil sie dieselbe Maskenversion `091A` verwendet,
die diese Firmware ohnehin baut – die Kopplerlogik bleibt unberührt, nur die
Identität wechselt. Sie kennt keine Kommunikationsobjekte, und ihre 25
Parameter sind Standard-Linienkopplereinstellungen, die `NetworkLayerCoupler`
bereits auswertet. `AdditionalAddressesCount="5"` gibt die fünf Tunnel vor,
deshalb setzt das Profil `KNX_TUNNELING=5`; ein `static_assert` bricht ab,
wenn beides auseinanderläuft.

**Die Unterlagen liegen bewusst nicht im Repository.** Produkthandbuch und
knxprod sind urheberrechtlich geschützt.
Beides gibt es kostenlos bei ABB unter der Bestellnummer `2CDG 110 175 R0011`
([Produktseite](https://new.abb.com/products/2CDG110175R0011/ipr-s3-1-1)) sowie
im KNX-Online-Katalog. Zum Nachvollziehen genügt die knxprod – sie ist ein
ZIP-Archiv, die Kennungen stehen in `M-0002/M-0002_A-A0A9-10-AA35.xml`.

Offen ist die Hardwarekennung `PID_HARDWARE_TYPE`: sechs Oktette, die weder im
Handbuch noch in der knxprod stehen. Sie bleibt vorerst null.

### Eine eigene Produktdatenbank

Drei Gründe, warum das bisher nicht beschritten wurde:

1. **Herstellerkennung.** Ein importierbares knxprod braucht eine bei der KNX
   Association registrierte Manufacturer-ID. Der Stack meldet sich mit `0xFA`
   (thelsing/knx-Default), was für den Eigengebrauch reicht, aber keine
   offizielle Kennung ist.
2. **Doppelte Konfigurationswege.** Zeitserver-Parameter gleichzeitig per ETS
   *und* Web-GUI pflegbar zu machen, erzeugt Zustandskonflikte. Die GUI ist
   für dieses Gerät der praktikablere Weg – sie funktioniert ohne ETS-Lizenz
   und ohne PC mit installierter ETS.
3. **Aufwand.** Das Erzeugen liefe über
   [OpenKNXproducer](https://github.com/OpenKNX/OpenKNXproducer) (XML → knxprod,
   ETS 5.7/6 muss installiert sein). Die Applikationsbeschreibung und der
   Parameterspeicher wären ein eigenes Teilprojekt.

Falls es später gewünscht ist: Der KNX-Stack unterstützt die dafür nötigen
`knx.paramByte()`/`paramWord()`-Zugriffe bereits, `Bau091A` enthält ein
`OT_APPLICATION_PROG`-Objekt. Der Weg ist offen, nur nicht beschritten.

---

## Ethernet (W5500, optional)

Ein W5500-Modul lässt sich per SPI anschließen und wird beim Start
**automatisch erkannt**. Dasselbe Firmware-Image läuft auf Geräten mit und
ohne Ethernet.

### PHY-Betrieb ohne Hardware-IP-Stack

Genau so wird der W5500 hier verwendet. Der ESP-IDF-Treiber schaltet ihn in
den **MACRAW-Modus**: die hartverdrahtete TCP/IP-Engine des Chips bleibt
ungenutzt, er arbeitet nur als MAC/PHY, und lwIP macht den kompletten
IP-Stack.

Das ist keine Geschmacksfrage, sondern Voraussetzung:

* Die vier Hardware-Sockets des W5500 reichen für 10 Tunnel plus Routing nicht.
* KNXnet/IP-Routing braucht **IGMP-Multicast** – der Hardware-Stack kann das
  nur eingeschränkt.
* Der KNX-Stack spricht BSD-Sockets. Er merkt vom Interfacewechsel nichts.

Angenehmer Nebeneffekt: `WiFiUDP` ist in Arduino-ESP32 3.x lediglich ein
`typedef` auf `NetworkUDP`, also ein reiner Socket-Wrapper ohne WLAN-Bezug.
Der KNX-Stack funktioniert deshalb **unverändert** über Ethernet.

### Verdrahtung

Vier Leitungen plus Versorgung. `SCS` ist der Chip-Select des W5500, `INT` und
`RST` bleiben in allen Vorgaben unbeschaltet (−1): der Treiber pollt, und der
Reset des Moduls hängt am eigenen RC-Glied.

Je nach Entwicklungsboard sind unterschiedliche GPIOs vorbelegt, weil die
freien Pins woanders liegen:

| W5500 | `esp32dev` | `esp32s3` | `esp32s2` | `esp32c3` | `esp32c6` |
| --- | --- | --- | --- | --- | --- |
| SCK | 18 | 12 | 12 | – | – |
| MISO | 19 | 13 | 13 | – | – |
| MOSI | 23 | 11 | 11 | – | – |
| SCS | 5 | 10 | 10 | – | – |
| INT | −1 | −1 | −1 | – | – |
| RST | −1 | −1 | −1 | – | – |
| Vorgabe | aktiv | aktiv | aktiv | aus | aus |

`3V3` und `GND` gehen in allen Fällen an 3,3 V und Masse. Das Modul zieht beim
Link-Aufbau kurzzeitig deutlich über 100 mA – ein USB-Port am Entwicklungsboard
reicht dafür meist, ein schwacher LDO nicht.

**Wird der Chip nur sporadisch erkannt?** Ohne verdrahtete `RST`-Leitung
verlässt sich der W5500 auf seinen eigenen Power-on-Reset, und dessen PLL
braucht rund 50 ms. Beim Kaltstart kann das nach dem Punkt liegen, an dem der
ESP32 nachsieht. Die Firmware fragt das Versionsregister deshalb **sechsmal im
Abstand von 25 ms** ab und schreibt den zuletzt gelesenen Wert ins Protokoll:
`0x00`/`0xFF` heißt, dass niemand MISO treibt (Verdrahtung, Versorgung, falsche
Pins), jeder andere Wert deutet auf einen fremden Chip oder auf zu lange
Leitungen für den Takt.

Die Vorgaben stehen als `SBIP_ETH_*_PIN` in
[platformio.ini](platformio.ini) und lassen sich zur Laufzeit über das
Hardware-Profil überschreiben.

**XIAO ESP32-C3** (`esp32c3`): Das Board führt nur elf GPIOs heraus, die
bereits durch KNX-UART, I2C, LED und Taster belegt sind. Ethernet ist dort in
der Vorgabe deaktiviert und lässt sich nur einschalten, wenn man dafür etwas
anderes aufgibt.

**ESP32-C6-DevKitC-1** (`esp32c6`): Freie Pins gibt es genug, aber keine
Vorgabe – die Belegung hängt davon ab, welche Stiftleiste man benutzt. SCK,
MISO, MOSI und SCS im Hardware-Profil eintragen, dann läuft es.

Der Treiber ist immer einkompiliert, damit genau das möglich bleibt. Mit
`-DSBIP_ETH_COMPILED=0` entfällt er vollständig (spart rund 40 KB), dann ist
Ethernet aber auch über die GUI nicht mehr aktivierbar.

### Wie die Erkennung funktioniert

[src/eth_interface.cpp](src/eth_interface.cpp) liest vor dem Treiberstart das
**Versionsregister** `VERSIONR` (0x0039) direkt über SPI. Es liefert auf jedem
W5500 konstant `0x04`.

Bewusst nicht einfach `ETH.begin()` aufrufen und den Rückgabewert auswerten:
Der Treiber schreibt bei fehlendem Chip eine Fehlerlawine ins Log und lässt
den SPI-Bus belegt. Ein einzelner Registerzugriff ist deterministisch, still
und kostet Mikrosekunden – erst das macht das Proben bei jedem Start auch auf
Geräten vertretbar, an denen nie Ethernet hängt.

### Ethernet hat Vorrang

Kommt der W5500 hoch, wird **WLAN gar nicht erst gestartet**. Zwei Gründe
jenseits der Stromersparnis:

1. `NetworkUDP::beginMulticast()` tritt der Gruppe mit
   `imr_interface = INADDR_ANY` bei – lwIP nimmt dafür das *Default-Interface*.
   Bei zwei aktiven Interfaces hinge es an Routing-Prioritäten, wo der
   KNX-Multicast landet. `ETH.setDefault()` macht die Sache eindeutig.
2. Bei funktionierender Kabelverbindung gibt es nichts einzurichten – der
   offene Provisionierungs-AP wäre nur zusätzliche Angriffsfläche.

**Die Wahl fällt einmal beim Start.** Ein später eingestecktes Kabel wird
nicht sofort übernommen; dafür sorgt die Ersatzverbindung unten.

### WLAN-Ersatzverbindung

Bis dahin galt: Fällt das Kabel aus, ist das Gerät bis zum nächsten Neustart
offline. `_ethMode` schaltete den WLAN-Watchdog dauerhaft ab, und das Funkteil
war ohnehin aus.

Der Haken **WLAN-Ersatzverbindung** auf der Startseite ändert das
(`POST /api/wifi/fallback`, Vorgabe **ein**). `NetManager::superviseFailover()`
vergleicht sekündlich Betriebsart und Wirklichkeit; weichen sie länger als
eine Minute voneinander ab, **startet das Gerät neu** – und `begin()` trifft
die Wahl dann anhand dessen, was wirklich da ist. Das gilt in beide
Richtungen: Kabel weg → WLAN, Kabel zurück → Ethernet.

**Warum ein Neustart und kein Umschalten im Betrieb?** Weil die
Entscheidungslogik in `begin()` die ist, die auf diesem Gerät erprobt ist, und
weil das Verschieben des Netif unter einem laufenden KNX-Multicast-Socket
genau das ist, woran diese Firmware zweimal gescheitert ist – einmal als
`could not join igmp: 125`, einmal als `InstructionFetchError` beim
Moduswechsel. Ein Gerät, dessen Netz gerade verschwunden ist, bedient
niemanden; fünf Sekunden Neustart kosten dagegen nichts.

Die Minute Karenz ist kein runder Wert: Sie liegt bewusst über den zwanzig
Sekunden, nach denen `HwConfig` das Profil für erprobt erklärt. Wäre sie
kürzer, würde ein flatterndes Kabel den Crash-Loop-Zähler hochzählen und
irgendwann das Hardware-Profil verwerfen.

Zwei Sperren:

* **Ohne gespeicherte Zugangsdaten** wird nicht umgeschaltet. Der Neustart
  landete sonst im Provisionierungs-AP – ein offener Access Point, weil jemand
  ein Kabel gezogen hat, ist keine Verbesserung.
* **Ohne erkannten W5500** lässt sich der Haken nicht abschalten; er ist dann
  gesetzt und gesperrt, mit entsprechendem Tooltip. Ohne Ethernet gäbe es
  nichts, wovon man ausweichen könnte – die Einstellung wäre nur ein Weg, sich
  auszusperren. `begin()` setzt sie zusätzlich zurück, falls die Platine später
  verschwindet.

**Was der Wechsel kostet:** eine neue IP-Adresse. Offene ETS-Tunnel reißen ab,
und wer die Schnittstelle im Projekt über die IP fest eingetragen hat, muss
sie neu zuweisen. Über die Entdeckung oder `<name>.local` gefunden, findet ETS
das Gerät von selbst wieder.

### Zwei Fallstricke, die dabei behoben wurden

**Reihenfolge.** `knxLink.begin()` aktiviert den Stack und legt dabei den
KNXnet/IP-Socket an. Der IGMP-Beitritt bindet sich an das Interface, das in
diesem Moment Default ist. Deshalb läuft `ethInterface.begin()` in
[src/main.cpp](src/main.cpp) **vor** dem KNX-Start.

**Mitgliedschaft erneuern.** Auch dann hat das Interface beim Stackstart noch
keine Adresse. `KnxLink::restartIpLayer()` schließt den Socket und öffnet ihn
neu, sobald die Verbindung wirklich steht. Das betrifft den WLAN-Pfad genauso
und war dort vorher nicht abgedeckt.

### IP-Meldung an ETS

`esp32_platform.cpp` des KNX-Stacks legt das Interface zur **Übersetzungszeit**
fest:

```cpp
#ifdef KNX_IP_LAN
    #define KNX_NETIF ETH
#else
    #define KNX_NETIF WiFi
#endif
```

Das erzwänge zwei Firmware-Varianten und liefe der automatischen Erkennung
zuwider. Die vier Zugriffsmethoden sind aber `virtual`, deshalb genügt eine
Ableitung in [src/knx_link.cpp](src/knx_link.cpp) – **kein Patch am Stack**.

Das ist nicht kosmetisch: Diese Werte landen in der HPAI von Search- und
Connect-Response. Würde die WLAN-Schnittstelle gemeldet, während das Gerät
über Ethernet läuft, bekäme ETS `0.0.0.0` – WLAN ist dann ja gar nicht
gestartet.

---

## Architektur

| Datei | Aufgabe |
| --- | --- |
| [src/main.cpp](src/main.cpp) | Startreihenfolge, Hauptschleife |
| [src/hw_config.cpp](src/hw_config.cpp) | Hardware-Profil, Validierung, Failsafe |
| [src/knx_link.cpp](src/knx_link.cpp) | KNX-Stack, TP-Link-Überwachung, Statistik |
| [src/net_manager.cpp](src/net_manager.cpp) | WLAN, AP, Captive Portal |
| [src/button_service.cpp](src/button_service.cpp) | Taster entprellen, Druckdauer, Funktionen |
| [src/status_led.cpp](src/status_led.cpp) | LEDs, einfach und adressierbar |
| [src/eth_interface.cpp](src/eth_interface.cpp) | W5500-Erkennung und Link-Überwachung |
| [src/time_service.cpp](src/time_service.cpp) | Zeitquellen, DPT-Kodierung, Sendeplan |
| [src/rv3028.cpp](src/rv3028.cpp) | Treiber für die optionale RTC |
| [src/web_server.cpp](src/web_server.cpp) | Dashboard und REST-API |
| [src/ota_service.cpp](src/ota_service.cpp) | Firmware-Update, Rollback-Freigabe |
| [src/fw_hash.cpp](src/fw_hash.cpp) | SHA-256 über den Firmware-Datenstrom |
| [src/improv_service.cpp](src/improv_service.cpp) | Improv-Provisionierung |
| [src/json_util.cpp](src/json_util.cpp) | JSON-Escaping und Mini-Parser |
| [src/log_buffer.cpp](src/log_buffer.cpp) | Ringpuffer für das serielle Protokoll |

### Nebenläufigkeit

Der `ESPAsyncWebServer` läuft im `async_tcp`-Task, der KNX-Stack im
`loop()`-Task. Beide Regeln sind nicht optional:

1. **Der KNX-Stack wird nie aus einem Web-Handler heraus verändert.**
   `/api/progmode` setzt nur ein Flag, `KnxLink::loop()` wendet es an. Ebenso
   `/api/time/set` und `/api/time/send` – sie schreiben sonst die RTC über I2C
   und senden Telegramme aus dem falschen Task.
2. **Kein blockierender Aufruf im Web-Handler.** WLAN-Scan und HTTPS-Download
   laufen asynchron bzw. in eigenen Tasks; ein blockierender Aufruf friert
   sämtliche HTTP- und TCP-Callbacks ein.

### Zur Frage nach dem zweiten Kern

Ein zweiter Kern hilft **nicht**, um ein zweites KNX-Gerät (etwa das OpenKNX-
Logikmodul) parallel laufen zu lassen:

* `MASK_VERSION` ist ein Compile-Zeit-Define. `bau091A.cpp` beginnt mit
  `#if MASK_VERSION == 0x091A` – pro Firmware-Image wird genau eine BAU-Klasse
  übersetzt. Zwei Masken in einem Binary gibt es nicht, unabhängig von der
  Kernanzahl.
* Der Stack greift an mehreren Stellen auf die globale `knx`-Instanz zu
  (z. B. `ip_data_link_layer.cpp` → `knx.progMode()`). Zwei Instanzen würden
  kollidieren.
* Beide Kerne teilen sich Flash und RAM. Ein zweiter Kern liefert Rechenzeit –
  und Rechenzeit war nie der Engpass.

Wofür ein zweiter Kern **sehr wohl** taugt: den KNX-Stack von dem Kern
wegzuziehen, auf dem WLAN und AsyncTCP laufen. Das reduziert Jitter im
UART-Empfangspfad. OpenKNX macht das über `OPENKNX_DUALCORE`. Hier ist es
nicht nötig, weil das SB-Interface das gesamte zeitkritische Bit-Timing selbst
erledigt.

### TP-Link-Überwachung

Der KNX-Stack pollt den TP-UART sekündlich mit `U_State.req` und `U_SetAddress`
und erklärt die Verbindung nach 5 s ohne Antwort für tot – ab dann verwirft er
jedes `L_Data.req` stillschweigend. Er erholt sich zwar von selbst, sobald
wieder Bytes eintreffen, aber nach einem Reset des SB-Interface ist der Zustand
beider Seiten inkonsistent. `KnxLink::superviseTpLink()` stößt deshalb alle
10 s einen erneuten Reset-Handshake an, solange `isConnected()` false ist.

### Buslast

Die Anzeige ist ein **Indikator, keine Messung**. Der Stack meldet Frames, nicht
Bytes; die Rechnung nimmt 50 Frames/s als 100 % an (TP1 mit 9600 bit/s und
minimalen L_Data-Frames). Lange Frames verfälschen den Wert nach unten.

### CPU-Last

FreeRTOS führt je Task einen Laufzeitzähler, gespeist aus `esp_timer`. Der
Idle-Task eines Kerns sammelt genau die Zeit, in der dieser Kern nichts zu tun
hatte – die Last ist also das, was vom Sekundenfenster übrig bleibt.

Der Zähler ist auf 32 Bit gekürzt und läuft alle ~71 Minuten über. Alle Werte
hier sind Differenzen zweier Messungen im Sekundenabstand, und vorzeichenlose
Arithmetik trägt das von allein über den Überlauf.

**Für Kern 1 gibt es diesen Wert nicht.** Die Arduino-Hauptschleife ist dorthin
gebunden und pollt, ohne je zu blockieren; der Idle-Task von Kern 1 kommt damit
nie an die Reihe, und die Rechnung oben liefert dauerhaft 100 %. Das ist keine
Messfehlfunktion – der Kern *ist* zu 100 % belegt –, sagt aber nichts darüber
aus, wie viel davon Arbeit war. Ein Bremsen der Schleife verbietet sich: der
TP-UART erwartet die Quittung eines adressierten Telegramms innerhalb weniger
Millisekunden, ein `delay(1)` je Durchlauf gefährdet dieses Fenster.

### Hauptschleifen-Last

Deshalb misst `CpuLoad::pass()` die Schleife selbst. Jeder Durchlauf von
`loop()` stempelt einen Zeitpunkt; über ein Sekundenfenster ergeben sich
Anzahl, Gesamtdauer und der kürzeste Durchlauf.

Der **kürzeste Durchlauf ist der Preis des Pollens** – das, was ein Durchlauf
kostet, der nichts zu tun findet. Nur was die übrigen Durchläufe darüber hinaus
brauchen, zählt als Arbeit:

```
Last = (Gesamtdauer − Durchläufe × kürzester Durchlauf) / Gesamtdauer
```

Ohne Telegramme und ohne Zugriffe geht der Wert damit gegen null, und er steigt,
je mehr echte Arbeit die Schleife bekommt. Preemption durch andere Tasks auf
demselben Kern fällt mit hinein – zu Recht, sie kostet die Schleife dieselbe
Zeit.

Daneben steht der **längste Durchlauf** seit dem letzten Zurücksetzen. Für ein
pollendes Gerät ist das die eigentliche Gesundheitszahl: er begrenzt, wie
schnell ein Telegramm bedient werden kann.

Grenze des Verfahrens: unter Volllast gibt es keinen leerlaufenden Durchlauf
mehr, der Bezugspunkt wandert nach oben und der Wert bleibt unter dem wahren.
Nach oben ist die Anzeige also konservativ, nie zu optimistisch.

Einkern-Varianten (C3, C6) zeigen nur einen Kernbalken; die Kernanzahl kommt aus
`CONFIG_FREERTOS_NUMBER_OF_CORES`, nicht aus dem Chiptyp – ein Dual-Core kann
per Konfiguration unicore laufen. Die Hauptschleifen-Last gibt es unabhängig
davon.

### Spitzenwertmarker

Jeder Auslastungsbalken trägt einen zweiten, schmalen Strich: den höchsten
Wert seit dem letzten Zurücksetzen. Die Zahl daneben nennt ihn ebenfalls, denn
ein 2-px-Strich auf einem 6-px-Balken ist zum Ablesen zu grob.

Buslast und CPU haben je einen eigenen Knopf – die eine Zahl beobachtet man beim
Suchen nach einem Telegrammsturm, die andere beim Nachladen der Weboberfläche,
und dafür will man nicht jedes Mal beide verlieren. Damit die Marker trotzdem
lesbar bleiben, steht hinter jedem Höchstwert die Zeit, für die er gilt.
`POST /api/peaks/reset` ohne `scope` löscht weiterhin beide. Der erste
Messzyklus nach dem Start wird verworfen; sonst stünde die Startlast für den
Rest der Laufzeit im Marker.

### Betriebsstunden

Die **Laufzeit** auf der Startseite ist die Zeit seit dem letzten Start und
sagt nichts darüber, wie lange das Gerät schon in Betrieb ist. Dafür zählt
[src/hour_meter.cpp](src/hour_meter.cpp) getrennt weiter, zusammen mit der
Anzahl der Starts. Beides setzt nur `POST /api/hours/reset` zurück.

Zwei Ablagen, und erst die Aufteilung macht beide billig:

| Ablage | Inhalt | Wie oft geschrieben |
|---|---|---|
| RV-3028 User-RAM, Byte 0 | Stunden seit dem letzten Abgleich | stündlich |
| NVS im Flash | die Gesamtsumme | alle 10 Tage und bei jedem Start |

Das User-RAM an Register 0x1F ist **echtes RAM** – von derselben Pufferquelle
gehalten wie die Uhr, ohne Schreibgrenze und ohne messbare Schreibdauer. Ein
Byte reicht, weil dort nur die Stunden seit dem letzten Abgleich stehen; **Byte
1 bleibt frei** für spätere Zwecke.

Der Abgleich alle 240 Stunden ergibt über dreißig Jahre rund **1100
Schreibvorgänge** im Flash. Das ist der Punkt: Nicht das Medium war das
Problem, sondern der Takt. Ein Viertelstundentakt kostete 35 000 Zugriffe im
Jahr, ein Zehntagestakt kostet 36.

Fällt die Pufferquelle aus, gehen höchstens die zehn Tage seit dem letzten
Abgleich verloren – nicht der ganze Zählerstand. Beim Start wird zusätzlich
sofort abgeglichen: Was das RAM über den Neustart getragen hat, steht damit
dauerhaft, und ab da kann eine leere Stützzelle nur noch die Stunden danach
kosten.

#### Neue RTC, leere Stützzelle

Der Inhalt des User-RAM ist nach einem vollständigen Spannungsverlust **nicht
definiert**. Läse das Gerät dort 0xFF und rechnete es dazu, käme bei jedem
Neustart ein Zuschlag von 255 Stunden heraus. Zwei Prüfungen schließen das aus:

* Das **Power-on-Reset-Flag** des Chips. Es wird gesetzt, sobald die Versorgung
  einschließlich Puffer unter die Betriebsschwelle fiel, und erst durch
  Schreiben der Uhr gelöscht – dasselbe Flag, an dem `timeValid()` die
  Glaubwürdigkeit der Uhrzeit festmacht.
* Ein Wert **über dem Abgleichsintervall**. Mehr als 240 kann dort im
  Normalbetrieb nicht stehen, weil genau bei 240 abgeglichen wird.

Trifft eines davon zu, wird der RAM-Inhalt verworfen und das Byte auf null
gesetzt; das Protokoll nennt den Grund. Der im NVS gespeicherte Stand bleibt
dabei **unangetastet** – `flush()` schreibt nur, wenn es tatsächlich etwas zu
übertragen gibt. Eine neue RTC oder eine leere Zelle kostet also die Stunden
seit dem letzten Abgleich, nie den Zählerstand selbst.

Das **EEPROM des RV-3028** (43 Byte neben dem RAM) wäre die dritte
Möglichkeit, bringt hier aber nichts: Bei rund 1100 Schreibvorgängen liegen
Flash und EEPROM beide weit innerhalb ihrer etwa 100 000 Zyklen, und das
EEPROM kostet zusätzlich eine blockierende I2C-Sequenz von etwa 10 ms je
Schreibvorgang. Es hätte genau einen Vorteil – der Zählerstand bliebe an der
RTC hängen statt am ESP32, überlebte also einen Tausch des Rechners statt
eines Tauschs der Uhr.

**Ohne RTC steht dort „nicht verfügbar"**, mit dem Hinweis *Keine RTC
vorhanden.* als Tooltip. Die angebrochene Stunde bräuchte sonst einen Platz,
den ein Neustart nicht löscht, und den gibt es ohne gepufferte Uhr nicht.

---

## Grenzen gegenüber einem NCN5130-Gateway

Diese Punkte folgen aus der Emulation, nicht aus dieser Firmware:

* **Keine Transceiver-Telemetrie.** V20V, VDD2, VBUS, VFILT, XTAL und
  Thermal-Warning existieren beim LPC1115 nicht. Das Dashboard zeigt
  stattdessen den Verbindungszustand und die Frame-Zähler.
* **Keine Extended Frames.** Die `Bus`-Zustandsmaschine von sblib implementiert
  nur Standard-Frames.
* **`L_Data.con` ist immer positiv.** sblib gibt das Sendeergebnis nicht heraus
  und wiederholt intern bereits bei NACK und BUSY.
* **ACK erfolgt autonom.** Das SB-Interface quittiert jedes empfangene
  Telegramm selbst, weil das ACK-Zeitfenster (~1,4 ms) keinen UART-Roundtrip
  zulässt. Folge: ETS erkennt „Gerät nicht vorhanden" nicht mehr zuverlässig,
  wenn nur dieses Interface an der Linie hängt.

Details siehe [../TPUART2-Emu/README.md](../TPUART2-Emu/README.md).

---

## Online-Update aktivieren

Die Manifest-URL steht im **Hardware-Profil** (`update_url`) und ist im
Bearbeiten-Dialog änderbar – ohne Neubau und ohne Neustart. `UPDATE_MANIFEST_URL`
in [include/interface_config.h](include/interface_config.h) ist nur noch die
Vorgabe für ein Gerät, das nie konfiguriert wurde; sie ist leer, das
Online-Update also ab Werk deaktiviert. Der Datei-Upload funktioniert
unabhängig davon.

Dass die URL im Profil liegt und nicht in einem eigenen Namensraum, hat einen
praktischen Grund: Das Profil ist das eine Dokument, das sich exportieren und
auf das nächste Gerät übertragen lässt. Geprüft wird sie wie jedes andere
Feld – leer oder mit `https://` beginnend, druckbares ASCII ohne Leerzeichen.
HTTPS ist keine Vorliebe, sondern Bedingung: der Update-Client ist ein
`WiFiClientSecure`.

Zum Aktivieren eine HTTPS-URL auf ein Manifest dieser Form eintragen:

```json
{
  "version": "0.2.0",
  "ota": {
    "ESP32":    { "path": "firmware_esp32.bin",   "sha256": "…" },
    "ESP32-S3": { "path": "firmware_esp32s3.bin", "sha256": "…" },
    "ESP32-C3": { "path": "firmware_esp32c3.bin", "sha256": "…" },
    "ESP32-C6": { "path": "firmware_esp32c6.bin", "sha256": "…" }
  }
}
```

Der Schlüssel ergibt sich aus `UPDATE_CHIP_KEY` in
[src/ota_service.cpp](src/ota_service.cpp) und ist für jede Chip-Variante
eigenständig – ein Image für das falsche Ziel würde sonst heruntergeladen und
erst beim Booten scheitern.

Fehlt `sha256` oder ist es kein gültiger 64-stelliger Hex-Wert, wird das
Update **abgelehnt**. Ein ungeprüfter Download wäre schlechter als keiner.

Die Binärdateien werden relativ zum Manifest aufgelöst. Das Zertifikat wird
**nicht** geprüft (`setInsecure()`) – die Integrität sichert die SHA-256-Summe
aus dem Manifest. Zur Reichweite dieser Zusicherung siehe *Firmware-Update und
Integrität*.

### Firmware für das SB-Interface

Daneben steht `lpc_url` (Define `LPC_MANIFEST_URL`) mit einem eigenen Block im
selben Format:

```json
{
  "version": "1.0.3",
  "lpc": {
    "LPC1115": { "path": "tpuart2emu.hex", "sha256": "…" }
  }
}
```

Der Schlüssel kommt aus `LPC_MANIFEST_KEY`. Beide URLs dürfen auf **dieselbe
Datei** zeigen – `ota` und `lpc` sind verschiedene Blöcke. Eigene URLs sind
trotzdem die Vorgabe: Der Emulator liegt in einem anderen Repository und
veröffentlicht in eigenem Takt.

**Es ist bewusst kein Update-*Check*.** `POST /api/lpc/fetch` holt die Datei,
prüft ihre SHA-256-Summe und legt sie in denselben Zwischenpuffer, den ein
Upload füllt; geschrieben wird sie erst mit `POST /api/lpc/write`. Ein
Versionsvergleich wäre gelogen: Der TP-UART-2-Emulator kennt **keinen
Versionsbefehl**, der Bootlader kann nur sagen, ob überhaupt ein startfähiges
Programm im Flash steht. Das Dashboard nennt deshalb nur, was das Manifest
anbietet, und überlässt die Entscheidung dem Menschen.

Der Download läuft auf einer eigenen Task mit 10 KiB Stack, nicht auf der
ISP-Task: TLS braucht den Platz, und diese hier blockiert an Sockets statt an
UART-Timing. Der Strom geht Byte für Byte durch `uploadData()`, also durch
denselben Parser wie eine hochgeladene Datei – Intel-Hex und Binär
funktionieren hier wie dort.

### Firmware für das SB-Interface

Daneben steht `lpc_url` (Define `LPC_MANIFEST_URL`) mit einem eigenen Block im
selben Format:

```json
{
  "version": "1.0.3",
  "lpc": {
    "LPC1115": { "path": "tpuart2emu.hex", "sha256": "…" }
  }
}
```

Der Schlüssel kommt aus `LPC_MANIFEST_KEY`. Beide URLs dürfen auf **dieselbe
Datei** zeigen – `ota` und `lpc` sind verschiedene Blöcke. Eigene URLs sind
trotzdem die Vorgabe: Der Emulator liegt in einem anderen Repository und
veröffentlicht in eigenem Takt.

**Es ist bewusst kein Update-*Check*.** `POST /api/lpc/fetch` holt die Datei,
prüft ihre SHA-256-Summe und legt sie in denselben Zwischenpuffer, den ein
Upload füllt; geschrieben wird sie erst mit `POST /api/lpc/write`. Ein
Versionsvergleich wäre gelogen: Der TP-UART-2-Emulator kennt **keinen
Versionsbefehl**, der Bootlader kann nur sagen, ob überhaupt ein startfähiges
Programm im Flash steht. Das Dashboard nennt deshalb nur, was das Manifest
anbietet, und überlässt die Entscheidung dem Menschen.

Der Download läuft auf einer eigenen Task mit 10 KiB Stack, nicht auf der
ISP-Task: TLS braucht den Platz, und diese hier blockiert an Sockets statt an
UART-Timing. Der Strom geht Byte für Byte durch `uploadData()`, also durch
denselben Parser wie eine hochgeladene Datei – Intel-Hex und Binär
funktionieren hier wie dort.

### Dieselbe Datei für die LPC-Firmware?

Dasselbe Manifest, ja – aber ein eigener Zweig darin, nicht der `ota`-Block.
Die beiden Downloads haben nichts gemeinsam außer dem Transport:

| | ESP32 | LPC1115 |
|---|---|---|
| Ziel | eigene App-Partition | Flash über ISP-UART |
| Auswahl | `UPDATE_CHIP_KEY` je ESP32-Variante | eine Datei für alle |
| Version | `FIRMWARE_VERSION` vergleichbar | **nicht auslesbar** |
| Größe | ~1,6 MB | ~30 KB |
| Format | rohes Image | Intel-Hex oder binär |

Der entscheidende Punkt steht in der dritten Zeile: Der TP-UART-2-Emulator hat
**keinen Versionsbefehl**, und der Bootlader kann nur sagen, ob überhaupt ein
startfähiges Programm im Flash steht. Ein automatischer Vergleich „ist die
angebotene Version neuer als die geflashte“ ist damit unmöglich – anders als
beim ESP32, wo genau dieser Vergleich das ganze Verfahren trägt.

Ein LPC-Zweig wäre also kein Update-Check, sondern ein bequemerer Dateiwahl-
Dialog: Datei holen, SHA-256 prüfen, schreiben, verifizieren – auf Ansage,
nicht automatisch. Sinnvoll strukturiert:

```json
{
  "version": "0.2.0",
  "ota": { "ESP32": { "path": "firmware_esp32.bin", "sha256": "…" } },
  "lpc": { "version": "2.0",
           "path": "tpuart2_emu.hex",
           "sha256": "…" }
}
```

Getrennte Dateien bräuchte es nur, wenn die LPC-Firmware aus einem anderen
Repository käme und dort ihren eigenen Veröffentlichungstakt hätte – was beim
Selfbus-Baukasten durchaus der Fall ist. Dann eine zweite URL neben
`update_url`, gleiches Format, gleicher Prüfweg.

---

## Lizenz und verwendete Komponenten

Diese Firmware steht unter der **GPL-3.0**
([Lizenztext](https://github.com/jelli123/IP-Interface-ESP32?tab=GPL-3.0-1-ov-file)).
Das ist keine freie Wahl: der KNX-Stack ist selbst GPL-3.0, und ein damit
gelinktes Werk kann nur unter derselben Lizenz weitergegeben werden.

| Komponente | Lizenz | Nachweis |
|---|---|---|
| [thelsing/knx](https://github.com/thelsing/knx) | GPL-3.0 | `.pio/libdeps/*/knx/LICENSE` |
| [ESPAsyncWebServer](https://github.com/ESP32Async/ESPAsyncWebServer) | LGPL-3.0 | `library.properties` → `license=LGPL-3.0` |
| [AsyncTCP](https://github.com/ESP32Async/AsyncTCP) | LGPL-3.0 | `library.properties` → `license=LGPL-3.0` |
| [Improv-WiFi-Library](https://github.com/jnthas/Improv-WiFi-Library) | MIT | `LICENSE` |
| [Arduino-ESP32](https://github.com/espressif/arduino-esp32) | LGPL-2.1-or-later | `framework-arduinoespressif32/package.json` |
| [ESP-IDF](https://github.com/espressif/esp-idf) | Apache-2.0 | `docs/en/COPYRIGHT.rst` |
| [FreeRTOS-Kernel](https://github.com/FreeRTOS/FreeRTOS-Kernel) | MIT | Quelltextköpfe in `components/freertos` |
| [lwIP](https://savannah.nongnu.org/projects/lwip/) | BSD-3-Clause | ESP-IDF `COPYRIGHT.rst` |
| [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) | Apache-2.0 | ESP-IDF `COPYRIGHT.rst` |

Alle sind mit der GPL-3.0 vereinbar:

* **Apache-2.0 → GPL-3.0** ist einseitig verträglich. Die FSF führt die
  Apache-2.0 ausdrücklich als GPLv3-kompatibel; die Unverträglichkeit betrifft
  nur die **GPLv2**, weil deren Text die Patentklauseln der Apache-2.0 als
  zusätzliche Einschränkung liest. Für ESP-IDF und Mbed TLS ist das die
  einzige relevante Frage.
* **LGPL-2.1-or-later** (Arduino-ESP32) darf über die „or later“-Klausel als
  LGPL-3.0 verwendet und damit in ein GPL-3.0-Werk übernommen werden.
* **LGPL-3.0** (ESPAsyncWebServer, AsyncTCP) geht ohne Umweg in GPL-3.0 auf.
* **MIT** und **BSD-3-Clause** sind permissiv und verlangen nur die
  Weitergabe ihres Lizenzhinweises.

Die Namensnennung leisten der Info-Dialog des Dashboards und diese Tabelle.
Den vollständigen Lizenztext liefert jede Komponente in ihrem eigenen
Quelltextarchiv mit; er wird hier nicht dupliziert.
