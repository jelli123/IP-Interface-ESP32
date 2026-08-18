/*
 *  index_html.h - Dashboard, served from flash.
 *
 *  Kept as a single self-contained page: no external assets, no framework,
 *  so the interface works without internet access and without a filesystem
 *  partition.
 */
#pragma once

#include <pgmspace.h>

static const char index_html[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="de">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Selfbus KNX/IP Interface</title>
<style>
:root{--bg:#12161c;--card:#1b212b;--line:#2b3444;--fg:#e6ebf2;--dim:#8b97a8;
--ok:#3fb950;--warn:#d29922;--err:#f85149;--acc:#3b82f6}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);
font:14px/1.5 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
header{display:flex;align-items:center;gap:12px;padding:16px 20px;
border-bottom:1px solid var(--line);flex-wrap:wrap}
h1{font-size:17px;margin:0;font-weight:600}
h2{font-size:13px;margin:0 0 12px;color:var(--dim);text-transform:uppercase;
letter-spacing:.06em;font-weight:600}
.badge{margin-left:auto;padding:5px 12px;border-radius:20px;font-size:12px;
cursor:pointer;border:1px solid var(--line);background:var(--card);
transition:transform .07s ease,filter .12s ease}
.badge:active{transform:translateY(1px) scale(.97);filter:brightness(.88)}
.badge.ok{color:var(--ok)} .badge.warn{color:var(--warn)}
main{display:grid;gap:16px;padding:20px;
grid-template-columns:repeat(auto-fit,minmax(310px,1fr))}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:16px}
.row{display:flex;justify-content:space-between;gap:12px;padding:5px 0;
border-bottom:1px solid rgba(255,255,255,.04)}
.row:last-child{border:0}
.row span:first-child{color:var(--dim)}
.row span:last-child{font-variant-numeric:tabular-nums;text-align:right}
button{background:var(--acc);color:#fff;border:0;border-radius:7px;
padding:9px 16px;font-size:13px;cursor:pointer;font-family:inherit;
transition:transform .07s ease,filter .12s ease,box-shadow .12s ease}
button.sec{background:transparent;border:1px solid var(--line);color:var(--fg)}
button.on{background:var(--warn);color:#12161c;font-weight:600}
button:hover:not(:disabled){filter:brightness(1.15)}
/* Press feedback: the button sinks in, so a click is felt and not just seen. */
button:active:not(:disabled){transform:translateY(1px) scale(.97);
filter:brightness(.88);box-shadow:inset 0 2px 5px rgba(0,0,0,.35)}
button:focus-visible{outline:2px solid var(--acc);outline-offset:2px}
button:disabled{opacity:.5;cursor:not-allowed}
@media (prefers-reduced-motion:reduce){
button,.badge{transition:none}
button:active:not(:disabled),.badge:active{transform:none}}
#langBtn{padding:5px 11px;font-size:12px;border-radius:20px}
.bar{height:6px;background:var(--line);border-radius:3px;overflow:hidden;margin-top:8px}
.bar>i{display:block;height:100%;background:var(--acc);width:0;transition:width .3s}
.actions{display:flex;gap:8px;flex-wrap:wrap;margin-top:12px}
dialog{background:var(--card);color:var(--fg);border:1px solid var(--line);
border-radius:10px;padding:20px;min-width:300px;max-width:92vw}
dialog::backdrop{background:rgba(0,0,0,.6)}
input,select{width:100%;padding:9px;margin:6px 0 12px;background:var(--bg);
border:1px solid var(--line);border-radius:7px;color:var(--fg);font-family:inherit}
.dot{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:7px}
.dot.ok{background:var(--ok)} .dot.err{background:var(--err)}
.dot.warn{background:var(--warn)} .dot.off{background:var(--line)}
.dim{color:var(--dim)}
dialog label{display:block;margin-top:10px;font-size:12px;color:var(--dim)}
/* Option groups: the checkbox heads the box it switches on, so it no longer
 * reads as just another field caption floating above unrelated inputs. */
.grp{border:1px solid var(--line);border-radius:8px;padding:12px;margin:14px 0}
dialog .grp>label:not(.chk):first-child{margin-top:0}
.grp.off>*:not(:first-child){opacity:.45}
dialog label.chk{display:flex;align-items:center;gap:9px;margin:0;
font-size:13px;color:var(--fg)}
input[type=checkbox]{width:auto;margin:0;flex:0 0 auto;accent-color:var(--acc)}
.trio{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px}
.trio input{margin:6px 0}
/* Row editors. The selected row is what the minus button acts on, so it has
 * to be obvious which one that is - a border alone is too quiet here. */
table.rows{width:100%;border-collapse:collapse;margin:6px 0 4px}
table.rows td{padding:2px 3px}
table.rows tr{cursor:pointer}
table.rows tr.sel td{background:rgba(94,168,255,.14)}
table.rows tr.sel td:first-child{box-shadow:inset 3px 0 0 var(--acc)}
table.rows input,table.rows select{margin:2px 0;padding:6px;font-size:13px}
table.rows input[type=checkbox]{margin:0 0 0 6px}
.rowbtns{display:flex;gap:8px}
button.ico{width:38px;padding:7px 0;font-size:16px;line-height:1;flex:0 0 auto}
small{color:var(--dim)}
</style>
</head>
<body>
<header>
  <h1>Selfbus KNX/IP Interface</h1>
  <span class="badge" id="netBadge" onclick="openWifi()">...</span>
  <button class="sec" id="langBtn" onclick="toggleLang()"
          title="Sprache / Language">EN</button>
</header>

<main>
  <section class="card">
    <h2>Status</h2>
    <div class="row"><span>Laufzeit</span><span id="uptime">-</span></div>
    <div class="row"><span>Physikalische Adresse</span><span id="pa">-</span></div>
    <div class="row"><span>ETS-Konfiguration</span><span id="cfg">-</span></div>
    <div class="row"><span>Tunnel (max.)</span><span id="tun">-</span></div>
    <div class="row"><span>Tunnel-Adressen</span><span id="tunPa">-</span></div>
    <div class="row"><span>Programmiermodus</span><span id="pm">-</span></div>
    <div class="actions">
      <button id="pmBtn" onclick="toggleProg()">Programmiermodus</button>
      <button class="sec" onclick="showFilter()">Filtertabelle</button>
      <button class="sec" onclick="resetKnx()">ETS-Programmierung l&ouml;schen</button>
    </div>
    <label class="chk" style="margin-top:14px">
      <input type="checkbox" id="rtAll" onchange="setRouting()">
      Ohne Filtertabelle alles weiterleiten</label>
    <p><small>Ein unprogrammierter Koppler sperrt jedes Gruppentelegramm.
    Nur einschalten, wenn dieses Ger&auml;t die einzige Verbindung zwischen
    Linie und IP ist &ndash; sonst drohen Telegrammschleifen.</small></p>
  </section>

  <section class="card">
    <h2>KNX TP1 &ndash; SB-Interface</h2>
    <div class="row"><span>Verbindung</span><span id="tpConn">-</span></div>
    <div class="row"><span>Schnittstelle</span><span id="tpType">-</span></div>
    <div class="row"><span>Baudrate</span><span id="tpBaud">-</span></div>
    <div class="row"><span>Selbsttest</span><span id="tpTest">-</span></div>
    <div class="row"><span>Buslast</span><span id="tpLoad">-</span></div>
    <div class="bar"><i id="tpLoadBar"></i></div>
  </section>

  <section class="card">
    <h2>Telegramme</h2>
    <div class="row"><span>TP empfangen</span><span id="tpRx">-</span></div>
    <div class="row"><span>TP verworfen (fremd)</span><span id="tpIgn">-</span></div>
    <div class="row"><span>TP ungueltig</span><span id="tpInv">-</span></div>
    <div class="row"><span>TP gesendet</span><span id="tpTx">-</span></div>
    <div class="row"><span>IP empfangen</span><span id="ipRx">-</span></div>
    <div class="row"><span>IP gesendet</span><span id="ipTx">-</span></div>
  </section>

  <section class="card">
    <h2>Zeitserver</h2>
    <div class="row"><span>Aktuelle Zeit</span><span id="tsNow">-</span></div>
    <div class="row"><span>Zeitquelle</span><span id="tsSrc">-</span></div>
    <div class="row"><span>NTP-Server</span><span id="tsNtpAct">-</span></div>
    <div class="row"><span>RTC (RV-3028)</span><span id="tsRtc">-</span></div>
    <div class="row"><span>Naechstes Senden</span><span id="tsNext">-</span></div>
    <div class="actions">
      <button class="sec" onclick="openTime()">Einstellungen</button>
      <button class="sec" onclick="syncBrowser()">Zeit vom Browser</button>
      <button class="sec" onclick="sendTime()">Jetzt senden</button>
    </div>
  </section>

  <section class="card">
    <h2>Netzwerk</h2>
    <div class="row"><span>Schnittstelle</span><span id="ifc">-</span></div>
    <div class="row"><span>SSID</span><span id="ssid">-</span></div>
    <div class="row"><span>IP-Adresse</span><span id="ip">-</span></div>
    <div class="row"><span>IP-Bezug</span><span id="ipSrc">-</span></div>
    <div class="row"><span>Netzmaske</span><span id="mask">-</span></div>
    <div class="row"><span>Gateway</span><span id="gw">-</span></div>
    <div class="row"><span>DNS</span><span id="dns">-</span></div>
    <div class="row"><span>MAC</span><span id="mac">-</span></div>
    <div class="row"><span>Signal</span><span id="rssi">-</span></div>
    <div class="row"><span>Ethernet (W5500)</span><span id="ethSt">-</span></div>
    <div class="actions">
      <button class="sec" onclick="openWifi()">WLAN einrichten</button>
    </div>
  </section>

  <section class="card">
    <h2>System</h2>
    <div class="row"><span>Chip</span><span id="chip">-</span></div>
    <div class="row"><span>Takt</span><span id="cpu">-</span></div>
    <div class="row"><span>Freier Speicher</span><span id="heap">-</span></div>
  </section>

  <section class="card">
    <h2>Hardware-Profil</h2>
    <div class="row"><span>Quelle</span><span id="hwSrc">-</span></div>
    <div class="row"><span>KNX-UART</span><span id="hwKnx">-</span></div>
    <div class="row"><span>Taster</span><span id="hwBtns">-</span></div>
    <div class="row"><span>LEDs</span><span id="hwLeds">-</span></div>
    <div class="row"><span>I2C (RTC)</span><span id="hwI2c">-</span></div>
    <div class="row"><span>SPI (W5500)</span><span id="hwEth">-</span></div>
    <div class="actions">
      <button class="sec" onclick="openHw()">Bearbeiten</button>
      <button class="sec" onclick="hwFile.click()">JSON laden</button>
      <button class="sec" onclick="hwDownload()">JSON speichern</button>
      <input type="file" id="hwFile" accept=".json" style="display:none" onchange="hwUpload()">
    </div>
    <label class="chk" id="beatRow" style="margin-top:14px;display:none">
      <input type="checkbox" id="beat" onchange="setBeat()">
      Herzschlag &ndash; alle 2 Sekunden ein weisser Blitz</label>
    <p><small>Aenderungen am Profil werden erst nach einem Neustart aktiv.</small></p>
  </section>

  <section class="card">
    <h2>Firmware</h2>
    <div class="row"><span>Version</span><span id="ver">-</span></div>
    <div class="row"><span>Build</span><span id="build">-</span></div>
    <div class="row"><span>Aktiver Speicherplatz</span><span id="partRun">-</span></div>
    <div class="row"><span>Zweiter Speicherplatz</span><span id="partAlt">-</span></div>
    <div class="row"><span>Update</span><span id="updState">-</span></div>
    <div class="bar" id="updBar" style="display:none"><i id="updFill"></i></div>
    <div class="actions">
      <button class="sec" onclick="checkUpdate()">Online pruefen</button>
      <button class="sec" id="instBtn" onclick="installUpdate()" disabled>Installieren</button>
      <button class="sec" onclick="fw.click()">Datei hochladen</button>
      <input type="file" id="fw" accept=".bin" style="display:none" onchange="upload()">
      <button class="sec" id="swBtn" onclick="switchPart()" disabled>Partition wechseln</button>
    </div>
    <label style="display:block;margin-top:10px;font-size:12px;color:var(--dim)">
      SHA-256 der Datei (optional, aus <code>sha256sum</code>)</label>
    <input id="fwHash" placeholder="64 Hex-Zeichen &ndash; leer = ungeprueft">
    <p><small id="hashNote" data-dyn></small></p>
  </section>
</main>

<dialog id="wifiDlg">
  <h2>WLAN einrichten</h2>
  <select id="ssidSel"><option>Netzwerke suchen...</option></select>
  <input type="password" id="pw" placeholder="Passwort" autocomplete="off">
  <div class="actions">
    <button onclick="connect()">Verbinden</button>
    <button class="sec" onclick="scan()">Neu suchen</button>
    <button class="sec" onclick="wifiDlg.close()">Abbrechen</button>
  </div>
  <p><small>Nach dem Verbinden startet das Geraet neu.</small></p>
</dialog>

<dialog id="filterDlg">
  <h2>Filtertabelle</h2>
  <p><small id="filterInfo"></small></p>
  <div id="filterList" style="max-height:50vh;overflow:auto;font-size:13px;
       font-variant-numeric:tabular-nums;line-height:1.7"></div>
  <div class="actions">
    <button class="sec" onclick="filterDlg.close()">Schliessen</button>
  </div>
</dialog>

<dialog id="timeDlg">  <h2>Zeitserver</h2>
  <div class="grp">
    <label class="chk"><input type="checkbox" id="tsEn"> Zeit auf den KNX-Bus senden</label>
    <label>Gruppenadresse Datum+Zeit (DPT 19.001)</label>
    <input id="tsGaDt" placeholder="z.B. 0/0/1 &ndash; leer = aus">
    <label>Gruppenadresse Uhrzeit (DPT 10.001)</label>
    <input id="tsGaT" placeholder="leer = aus">
    <label>Gruppenadresse Datum (DPT 11.001)</label>
    <input id="tsGaD" placeholder="leer = aus">
    <label>Sendeintervall (Minuten)</label>
    <input id="tsIvl" type="number" min="1" max="1440">
  </div>
  <div class="grp">
    <label class="chk"><input type="checkbox" id="tsNtpEn"> Zeit per NTP holen</label>
    <label class="chk" style="margin-top:10px">
      <input type="checkbox" id="tsNtpDhcp"> NTP-Server vom DHCP beziehen</label>
    <label>NTP-Server (Fallback, wenn DHCP keinen liefert)</label>
    <input id="tsNtpSrv" placeholder="pool.ntp.org">
  </div>
  <label>Zeitzone (POSIX TZ)</label>
  <input id="tsTz" placeholder="CET-1CEST,M3.5.0,M10.5.0/3">
  <label>Zeit manuell setzen</label>
  <input id="tsManual" type="datetime-local" step="1">
  <div class="actions">
    <button onclick="saveTime()">Speichern</button>
    <button class="sec" onclick="setManual()">Zeit uebernehmen</button>
    <button class="sec" onclick="timeDlg.close()">Schliessen</button>
  </div>
  <p><small>Ohne Internetzugang NTP abschalten und die Zeit per Browser oder
  manuell setzen. Mit RV-3028 bleibt sie ueber einen Stromausfall erhalten.</small></p>
</dialog>

<dialog id="hwDlg">
  <h2>Hardware-Profil</h2>
  <p><small id="hwChip" data-dyn></small></p>

  <label>KNX &ndash; UART-Nummer / RX / TX</label>
  <div class="trio">
    <input id="hwUart" type="number" min="0">
    <input id="hwRx"   type="number" min="-1">
    <input id="hwTx"   type="number" min="-1">
  </div>

  <div class="grp">
    <label>Taster</label>
    <table class="rows" id="hwBtnTbl"></table>
    <div class="rowbtns">
      <button class="sec ico" onclick="rowAdd('btn')" title="Zeile hinzufuegen">+</button>
      <button class="sec ico" onclick="rowDel('btn')" title="Markierte Zeile entfernen">&minus;</button>
    </div>
    <p><small>Kurz ist ein Druck unter einer Sekunde, lang ab zwei Sekunden,
    sehr lang ab sechs. Derselbe GPIO darf mehrfach vorkommen, solange sich
    die Auesloesung unterscheidet.</small></p>
  </div>

  <div class="grp">
    <label>LEDs</label>
    <table class="rows" id="hwLedTbl"></table>
    <div class="rowbtns">
      <button class="sec ico" onclick="rowAdd('led')" title="Zeile hinzufuegen">+</button>
      <button class="sec ico" onclick="rowDel('led')" title="Markierte Zeile entfernen">&minus;</button>
    </div>
    <p><small>Eine adressierbare LED ist eine Position in einer Kette. Zeilen
    mit demselben GPIO gehoeren zur selben Kette und brauchen denselben Chiptyp,
    aber verschiedene Positionen.</small></p>
  </div>

  <div class="grp">
    <label>Zuordnung Taster</label>
    <table class="rows" id="hwBaTbl"></table>
    <div class="rowbtns">
      <button class="sec ico" onclick="rowAdd('ba')" title="Zeile hinzufuegen">+</button>
      <button class="sec ico" onclick="rowDel('ba')" title="Markierte Zeile entfernen">&minus;</button>
    </div>
    <p><small>Werkeinstellungen loescht alles, auch die WLAN-Zugangsdaten.
    WLAN ein/aus laesst sich nur abschalten, wenn ein W5500 erkannt wurde,
    und wirkt nach einem Neustart.</small></p>
  </div>

  <div class="grp">
    <label>Zuordnung LEDs</label>
    <table class="rows" id="hwLaTbl"></table>
    <div class="rowbtns">
      <button class="sec ico" onclick="rowAdd('la')" title="Zeile hinzufuegen">+</button>
      <button class="sec ico" onclick="rowDel('la')" title="Markierte Zeile entfernen">&minus;</button>
      <button class="sec ico" onclick="rowMove('la',-1)" title="Nach oben">&uarr;</button>
      <button class="sec ico" onclick="rowMove('la',1)" title="Nach unten">&darr;</button>
    </div>
    <p><small>Die Reihenfolge entscheidet: Fuer jede LED gilt die
    oberste Zeile, deren Zustand gerade zutrifft. Dieselbe LED darf mehrfach
    vorkommen &ndash; so zeigt eine einzelne LED nacheinander
    Programmiermodus, Netzzustand und Herzschlag. Die Farbe wirkt nur bei
    adressierbaren LEDs; eine einfache LED unterscheidet die Zustaende
    allein am Muster.</small></p>
  </div>

  <div class="grp">
    <label class="chk"><input type="checkbox" id="hwI2cEn"> RTC ueber I2C anschliessen</label>
    <label>SDA / SCL</label>
    <div class="trio">
      <input id="hwSda" type="number" min="-1">
      <input id="hwScl" type="number" min="-1">
      <span></span>
    </div>
  </div>

  <div class="grp">
    <label class="chk"><input type="checkbox" id="hwEthEn"> Ethernet W5500 anschliessen</label>
    <label>SCK / MISO / MOSI</label>
    <div class="trio">
      <input id="hwSck"  type="number" min="-1">
      <input id="hwMiso" type="number" min="-1">
      <input id="hwMosi" type="number" min="-1">
    </div>
    <label>CS / IRQ / RST (&minus;1 = ungenutzt)</label>
    <div class="trio">
      <input id="hwCs"  type="number" min="-1">
      <input id="hwIrq" type="number" min="-1">
      <input id="hwRst" type="number" min="-1">
    </div>
  </div>

  <p id="hwErr" style="color:var(--err);font-size:12px"></p>
  <div class="actions">
    <button onclick="hwSave()">Speichern</button>
    <button class="sec" onclick="hwDefaults()">Werte des Images</button>
    <button class="sec" onclick="hwReset()">Auf Standard zuruecksetzen</button>
    <button class="sec" onclick="hwDlg.close()">Abbrechen</button>
  </div>
  <p><small>Ungueltige Pins werden abgewiesen. Startet das Geraet mit einem
  neuen Profil zweimal nicht durch, faellt es automatisch auf die Werte des
  Images zurueck. Taster beim Einschalten gedrueckt halten erzwingt das
  ebenfalls.</small></p>
</dialog>

<script>
const $ = id => document.getElementById(id);

/*
 * Zweisprachigkeit.
 *
 * Deutsch steht im Markup und ist die Quelle. Uebersetzt wird ueber eine
 * einzige Tabelle, die vom deutschen Text auf den englischen abbildet - so
 * gibt es kein zweites Woerterbuch zu pflegen, und was fehlt, bleibt einfach
 * deutsch. Beim Umschalten zurueck wird der urspruengliche Textknoten aus
 * dem Cache wiederhergestellt.
 */
let LANG = localStorage.getItem('sbip-lang') === 'en' ? 'en' : 'de';

const EN = {
'Telegramme':'Telegrams', 'Zeitserver':'Time server', 'Netzwerk':'Network',
'Hardware-Profil':'Hardware profile', 'WLAN einrichten':'Set up Wi-Fi',
'Laufzeit':'Uptime', 'Physikalische Adresse':'Individual address',
'ETS-Konfiguration':'ETS configuration', 'Tunnel (max.)':'Tunnels (max.)', 'Tunnel-Adressen':'Tunnel addresses',
'Programmiermodus':'Programming mode', 'Verbindung':'Connection',
'Schnittstelle':'Interface', 'Baudrate':'Baud rate', 'Selbsttest':'Self test',
'Buslast':'Bus load', 'TP empfangen':'TP received',
'TP verworfen (fremd)':'TP discarded (foreign)', 'TP ungueltig':'TP invalid',
'TP gesendet':'TP sent', 'IP empfangen':'IP received', 'IP gesendet':'IP sent',
'Aktuelle Zeit':'Current time', 'Zeitquelle':'Time source',
'NTP-Server':'NTP server', 'Naechstes Senden':'Next transmission',
'IP-Adresse':'IP address', 'Netzmaske':'Subnet mask',
'IP-Bezug':'Address source', 'fest, aus der ETS':'fixed, from the ETS',
'automatisch (DHCP)':'automatic (DHCP)',
'Takt':'Clock', 'Freier Speicher':'Free memory',
'Quelle':'Source', 'LED / Taster':'LED / button', 'Status-LED':'Status LED',
'Herzschlag \u2013 alle 2 Sekunden ein weisser Blitz':
  'Heartbeat \u2013 a white flash every 2 seconds',

'KNX zur\u00fccksetzen':'Reset KNX', 'Einstellungen':'Settings',
'ETS-Programmierung l\u00f6schen':'Erase the ETS programming',
'Filtertabelle':'Filter table', 'Partition wechseln':'Switch partition',
'L\u00e4uft aus':'Running from', 'Zweiter Speicherplatz':'Second slot',
'Aktiver Speicherplatz':'Active slot',
'geprueft':'verified', 'auf Bewaehrung':'on probation', 'neu':'new',
'ungueltig':'invalid', 'abgebrochen':'aborted',
'ohne OTA-Vermerk':'no OTA record', 'unbekannt':'unknown',
'leer':'empty',
'lese Filtertabelle...':'reading the filter table...',
'Lesen fehlgeschlagen.':'Could not read it.',
['Keine Filtertabelle geladen \u2013 das Geraet ist nicht programmiert und '
+ 'sperrt jedes Gruppentelegramm.']:
  'No filter table loaded \u2013 the device is unprogrammed and blocks every '
+ 'group telegram.',
'%s Gruppenadressen werden weitergeleitet.':'%s group addresses are forwarded.',
'Angezeigt: die ersten %s.':'Showing the first %s.',
'Die Tabelle ist geladen, laesst aber nichts durch.':
  'The table is loaded but lets nothing through.',
['Beim naechsten Start die andere Partition verwenden? Das Geraet startet '
+ 'neu.']:
  'Boot from the other slot next time? The device restarts.',
['Achtung: Der Inhalt ist unbekannt. Dieser Speicherplatz hat unter der '
+ 'aktuellen Firmware noch nie gelaufen, dort liegt vermutlich ein aelterer '
+ 'Stand.']:
  'Careful: the contents are unknown. This slot has never run under the '
+ 'current firmware, so it most likely holds an older build.',
'Umgeschaltet. Das Geraet startet neu.':'Switched. The device restarts.',
'Umschalten nicht moeglich.':'Cannot switch.',
'Zeit vom Browser':'Time from browser', 'Jetzt senden':'Send now',
'Bearbeiten':'Edit', 'JSON laden':'Load JSON', 'JSON speichern':'Save JSON',
'Online pruefen':'Check online', 'Installieren':'Install',
'Datei hochladen':'Upload file', 'Verbinden':'Connect',
'Neu suchen':'Scan again', 'Abbrechen':'Cancel', 'Speichern':'Save',
'Zeit uebernehmen':'Apply time', 'Schliessen':'Close',
'Werte des Images':'Image defaults',
'Auf Standard zuruecksetzen':'Reset to defaults',

'Zeit auf den KNX-Bus senden':'Send time to the KNX bus',
'Ohne Filtertabelle alles weiterleiten':'Forward everything without a filter table',
['Ein unprogrammierter Koppler sperrt jedes Gruppentelegramm. Nur einschalten, '
+ 'wenn dieses Ger\u00e4t die einzige Verbindung zwischen Linie und IP ist '
+ '\u2013 sonst drohen Telegrammschleifen.']:
  'An unprogrammed coupler blocks every group telegram. Only turn this on when '
+ 'this device is the sole path between the line and IP \u2013 otherwise you '
+ 'invite telegram loops.',
'Gruppenadresse Datum+Zeit (DPT 19.001)':'Group address date+time (DPT 19.001)',
'Gruppenadresse Uhrzeit (DPT 10.001)':'Group address time (DPT 10.001)',
'Gruppenadresse Datum (DPT 11.001)':'Group address date (DPT 11.001)',
'Sendeintervall (Minuten)':'Transmission interval (minutes)',
'Zeit per NTP holen':'Get time via NTP',
'NTP-Server vom DHCP beziehen':'Obtain NTP server from DHCP',
'NTP-Server (Fallback, wenn DHCP keinen liefert)':
  'NTP server (fallback when DHCP supplies none)',
'Zeitzone (POSIX TZ)':'Time zone (POSIX TZ)',
'Zeit manuell setzen':'Set time manually',
'KNX \u2013 UART-Nummer / RX / TX':'KNX \u2013 UART number / RX / TX',
'Programmier-LED / Taster (\u22121 = nicht bestueckt)':
  'Programming LED / button (\u22121 = not fitted)',
'Taster':'Buttons', 'LEDs':'LEDs',
'Zuordnung Taster':'Button assignment', 'Zuordnung LEDs':'LED assignment',
'kurzer Druck':'short press', 'langer Druck':'long press',
'sehr langer Druck':'very long press',
'RGB-LED':'RGB LED',
'Programmiermodus':'Programming mode', 'Werkeinstellungen':'Factory reset',
'WLAN Grundeinstellung':'WiFi setup', 'Geraet neu starten':'Restart the device',
'WLAN ein/aus':'WiFi on/off',
'Programmier-LED':'Programming LED', 'Heartbeat-LED':'Heartbeat LED',
'Netzwerkanzeige':'Network indicator',
'Programmiermodus aktiv':'Programming mode active',
'AP-Modus offen':'Access point open',
'keine TP-Verbindung':'No TP connection',
'online':'online', 'offline':'offline', 'Herzschlag':'Heartbeat',
'rot':'red', 'gruen':'green', 'blau':'blue', 'gelb':'yellow',
'cyan':'cyan', 'magenta':'magenta', 'weiss':'white',
'Dauerlicht':'steady', 'langsam blinken':'slow blink',
'schnell blinken':'fast blink', 'Doppelblitz':'double flash',
'kurzer Blitz':'short flash',
'Nach oben':'Move up', 'Nach unten':'Move down',
'mehr Zeilen sind nicht moeglich':'no more rows are possible',
'keine':'none',
'hoechstens 8 Zeilen':'at most 8 rows',
'Bitte zuerst eine Zeile anklicken.':'Select a row first.',['Kurz ist ein Druck unter einer Sekunde, lang ab zwei Sekunden, sehr lang '
+ 'ab sechs. Derselbe GPIO darf mehrfach vorkommen, solange sich die '
+ 'Auesloesung unterscheidet.']:
  'Short means under one second, long from two seconds, very long from six. '
+ 'The same GPIO may appear more than once as long as the trigger differs.',
['Eine adressierbare LED ist eine Position in einer Kette. Zeilen mit '
+ 'demselben GPIO gehoeren zur selben Kette und brauchen denselben Chiptyp, '
+ 'aber verschiedene Positionen.']:
  'An addressable LED is one position in a chain. Rows sharing a GPIO belong '
+ 'to the same chain and need the same chip type but different positions.',
['Werkeinstellungen loescht alles, auch die WLAN-Zugangsdaten. WLAN ein/aus '
+ 'laesst sich nur abschalten, wenn ein W5500 erkannt wurde, und wirkt nach '
+ 'einem Neustart.']:
  'Factory reset erases everything, including the WiFi credentials. WiFi '
+ 'on/off can only be switched off once a W5500 has been found, and takes '
+ 'effect after a restart.',
['Die Reihenfolge entscheidet: Fuer jede LED gilt die oberste Zeile, deren '
+ 'Zustand gerade zutrifft. Dieselbe LED darf mehrfach vorkommen \u2013 so '
+ 'zeigt eine einzelne LED nacheinander Programmiermodus, Netzzustand und '
+ 'Herzschlag. Die Farbe wirkt nur bei adressierbaren LEDs; eine einfache '
+ 'LED unterscheidet die Zustaende allein am Muster.']:
  'The order decides: for each LED the topmost row whose state currently '
+ 'holds is the one that shows. The same LED may appear more than once - '
+ 'that is how a single LED shows programming mode, network state and a '
+ 'heartbeat in turn. Colour only applies to addressable LEDs; a plain one '
+ 'tells the states apart by pattern alone.',
'LED low-aktiv':'LED active low',
'RTC ueber I2C anschliessen':'Connect an RTC via I2C',
'Ethernet W5500 anschliessen':'Connect an Ethernet W5500',
'CS / IRQ / RST (\u22121 = ungenutzt)':'CS / IRQ / RST (\u22121 = unused)',
'SHA-256 der Datei (optional, aus':'SHA-256 of the file (optional, from',

'Aenderungen am Profil werden erst nach einem Neustart aktiv.':
  'Profile changes take effect after a restart.',
'Zeile hinzufuegen':'Add a row',
'Markierte Zeile entfernen':'Remove the selected row',
'low-aktiv':'active low',
'Nach dem Verbinden startet das Geraet neu.':
  'The device restarts after connecting.',
['Ohne Internetzugang NTP abschalten und die Zeit per Browser oder manuell '
+ 'setzen. Mit RV-3028 bleibt sie ueber einen Stromausfall erhalten.']:
  'Without internet access, switch NTP off and set the time from the browser '
+ 'or by hand. With an RV-3028 it survives a power failure.',
['Ungueltige Pins werden abgewiesen. Startet das Geraet mit einem neuen Profil '
+ 'zweimal nicht durch, faellt es automatisch auf die Werte des Images '
+ 'zurueck. Taster beim Einschalten gedrueckt halten erzwingt das ebenfalls.']:
  'Invalid pins are rejected. If the device fails to boot twice with a new '
+ 'profile, it falls back to the image defaults on its own. Holding the '
+ 'button while powering up forces the same.',

'Passwort':'Password', 'leer = aus':'empty = off',
'z.B. 0/0/1 \u2013 leer = aus':'e.g. 0/0/1 \u2013 empty = off',
'64 Hex-Zeichen \u2013 leer = ungeprueft':
  '64 hex characters \u2013 empty = unverified',
'Netzwerke suchen...':'Searching for networks...',

'Fehler':'Error', 'nicht programmiert':'not programmed', 'aktiv':'active',
'aus':'off', 'Programmiermodus starten':'Start programming mode',
'Programmiermodus beenden':'Stop programming mode',
'verbunden':'connected', 'kein Link':'no link',
'keine IP-Adresse':'no IP address', 'nicht gefunden':'not found',
'nicht konfiguriert':'not configured', 'WLAN':'Wi-Fi',
'AP-Modus aktiv':'AP mode active', 'getrennt':'disconnected',
'manuell':'manual', 'keine':'none', 'nicht gesetzt':'not set',
'keiner':'none', 'nicht bestueckt':'not fitted', 'deaktiviert':'disabled',
'noch keine vergeben':'none assigned yet',
'Image-Standard':'image defaults',
'ungueltig &rarr; Standard':'invalid &rarr; defaults',
'Startfehler &rarr; Standard':'boot failure &rarr; defaults',
'Taster &rarr; Standard':'button &rarr; defaults',
'gespeichertes Profil':'stored profile',
'(Neustart noetig)':'(restart required)', 'kein':'none',
'pruefe...':'checking...', 'berechne Pruefsumme...':'computing checksum...',
'lade hoch...':'uploading...',
'erfolgreich, Neustart...':'successful, restarting...',
'fehlgeschlagen':'failed', 'Uebertragung abgebrochen':'transfer aborted',
'abgelehnt':'rejected', 'Suche laeuft...':'Scanning...',
'Kein Netz gefunden':'No network found',
'Zeitueberschreitung':'Timed out', 'Version %s verfuegbar':'version %s available',

'Zeit konnte nicht gesetzt werden.':'Could not set the time.',
'Bitte Datum und Uhrzeit eingeben.':'Please enter a date and a time.',
'Senden nicht moeglich - Uhr nicht gesetzt?':
  'Cannot send - is the clock set?',
'Profil gespeichert. Jetzt neu starten?':'Profile saved. Restart now?',
'Gespeichertes Profil verwerfen und die Werte des Images verwenden?':
  'Discard the stored profile and use the image defaults?',
'Zurueckgesetzt. Jetzt neu starten?':'Reset done. Restart now?',
'Keine gueltige JSON-Datei.':'Not a valid JSON file.',
'Neustart laeuft. Die Seite in einigen Sekunden neu laden.':
  'Restarting. Reload the page in a few seconds.',
'Das Geraet laeuft ueber Ethernet. WLAN ist nicht aktiv.':
  'The device runs over Ethernet. Wi-Fi is not active.',
'Zugangsdaten gespeichert. Das Geraet startet neu.':
  'Credentials saved. The device restarts.',
'Firmware jetzt installieren? Das Geraet startet danach neu.':
  'Install the firmware now? The device restarts afterwards.',
'Kein SHA-256 angegeben. Firmware ungeprueft uebertragen?':
  'No SHA-256 given. Transfer the firmware unverified?',
'Zur\u00fccksetzen fehlgeschlagen.':'Reset failed.',
'KNX-Konfiguration gel\u00f6scht. Das Ger\u00e4t startet neu.':
  'KNX configuration cleared. The device restarts.',
['KNX-Konfiguration zur\u00fccksetzen?\n\nPhysikalische Adresse und '
+ 'Tunnel-Adressen gehen verloren, das Ger\u00e4t startet neu und muss in der '
+ 'ETS neu programmiert werden.\n\nWLAN und Hardware-Profil bleiben erhalten.']:
  'Reset the KNX configuration?\n\nThe individual address and the tunnel '
+ 'addresses are lost, the device restarts and has to be programmed again in '
+ 'the ETS.\n\nWi-Fi and the hardware profile are kept.',
'Wird beim Hochladen automatisch berechnet.':
  'Computed automatically during upload.',
['Automatische Berechnung braucht HTTPS und entfaellt hier. Selbst ermitteln '
+ 'mit "Get-FileHash firmware.bin" bzw. "sha256sum firmware.bin", oder leer '
+ 'lassen.']:
  'Automatic computation needs HTTPS and is unavailable here. Obtain it with '
+ '"Get-FileHash firmware.bin" or "sha256sum firmware.bin", or leave empty.'
};

/** Translate a single string. Unknown text stays German. */
const t = s => (LANG === 'en' && EN[s]) || s;

/** Escape for insertion into markup. Names are whitelisted by the firmware,
 *  but the editor also shows what the user is still typing. */
const esc = s => String(s).replace(/[&<>"]/g,
  c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));

// Everything static lives in one of these; dynamic values sit in the second
// span of a row or carry data-dyn, and are never touched here.
const I18N_SEL = 'h2,.row>span:first-child,button,label,small:not([data-dyn]),option';

function applyLang(){
  document.documentElement.lang = LANG;

  document.querySelectorAll(I18N_SEL).forEach(el => {
    el.childNodes.forEach(n => {
      if(n.nodeType !== 3) return;                    // text nodes only, so
      if(n.de === undefined) n.de = n.nodeValue;      // nested tags survive
      const m = n.de.match(/^(\s*)([\s\S]*?)(\s*)$/);
      if(!m[2]) return;
      const en = EN[m[2].replace(/\s+/g, ' ')];
      n.nodeValue = (LANG === 'en' && en) ? m[1] + en + m[3] : n.de;
    });
  });

  document.querySelectorAll('[placeholder]').forEach(el => {
    if(el.dePh === undefined) el.dePh = el.placeholder;
    el.placeholder = (LANG === 'en' && EN[el.dePh]) || el.dePh;
  });

  document.querySelectorAll('[title]').forEach(el => {
    if(el.deTitle === undefined) el.deTitle = el.title;
    el.title = (LANG === 'en' && EN[el.deTitle]) || el.deTitle;
  });

  $('langBtn').textContent = LANG === 'de' ? 'EN' : 'DE';
  $('hashNote').textContent = t(CAN_HASH
    ? 'Wird beim Hochladen automatisch berechnet.'
    : 'Automatische Berechnung braucht HTTPS und entfaellt hier. Selbst '
    + 'ermitteln mit "Get-FileHash firmware.bin" bzw. "sha256sum '
    + 'firmware.bin", oder leer lassen.');

  refresh(); refreshTime(); refreshHw();
}

function toggleLang(){
  LANG = LANG === 'de' ? 'en' : 'de';
  localStorage.setItem('sbip-lang', LANG);
  applyLang();
}

const dot = ok => `<span class="dot ${ok?'ok':'err'}"></span>${t(ok?'OK':'Fehler')}`;

// An unset address reads better as a dash than as 0.0.0.0.
const orDash = v => (!v || v === '0.0.0.0') ? '-' : v;

/** Prefix length of a dotted netmask: 255.255.254.0 -> 23. */
function prefixLen(mask){
  return mask.split('.')
             .reduce((n, o) => n + ((+o).toString(2).match(/1/g) || []).length, 0);
}

// Dim a group while its leading checkbox is off, so the fields read as
// belonging to the switch above them rather than standing on their own.
// Only a checkbox that IS the group's first element counts - the row editors
// contain checkboxes of their own, and those govern a single LED, not a box.
function syncGroups(){
  document.querySelectorAll('.grp').forEach(g => {
    const head = g.firstElementChild;
    const cb = (head && head.matches('label.chk')) ? head.querySelector('input') : null;
    if(cb) g.classList.toggle('off', !cb.checked);
  });
}
document.addEventListener('change', e => {
  if(e.target.type === 'checkbox') syncGroups();
});

let last = null;

async function refresh(){
  let s;
  try { s = await (await fetch('/api/status')).json(); }
  catch(e){ $('netBadge').textContent = 'offline'; return; }
  last = s;

  $('uptime').textContent = s.uptime;
  $('pa').textContent     = s.knx_pa;
  $('cfg').innerHTML      = s.knx_configured ? dot(true)
                          : '<span class="dot err"></span>' + t('nicht programmiert');
  $('tun').textContent    = s.knx_max_tunnels;
  $('tunPa').textContent  = (s.knx_tunnel_pa && s.knx_tunnel_pa.length)
                          ? s.knx_tunnel_pa.join(', ')
                          : t('noch keine vergeben');

  // Label the action, not the state - "Programmiermodus EIN" could be read
  // either way. The state lives in its own row above.
  $('pm').innerHTML       = s.prog_mode
                          ? '<span class="dot warn"></span>' + t('aktiv')
                          : '<span class="dot off"></span>' + t('aus');
  $('pmBtn').textContent  = t(s.prog_mode ? 'Programmiermodus beenden'
                                          : 'Programmiermodus starten');
  $('pmBtn').className    = s.prog_mode ? 'on' : '';
  $('rtAll').checked      = s.knx_route_all;

  $('beatRow').style.display = s.led_beat_available ? '' : 'none';
  $('beat').checked          = s.led_heartbeat;

  $('tpConn').innerHTML = dot(s.tp.connected);
  $('tpType').textContent = s.tp.type;
  $('tpBaud').textContent = s.tp.baud + ' Bd, 8E1';
  $('tpTest').textContent = s.tp.self_test;
  $('tpLoad').textContent = (s.tp.bus_load/10).toFixed(1) + ' %';
  $('tpLoadBar').style.width = (s.tp.bus_load/10) + '%';

  $('tpRx').textContent = s.tp.rx_frames;
  $('tpIgn').textContent= s.tp.rx_ignored;
  $('tpInv').textContent= s.tp.rx_invalid;
  $('tpTx').textContent = s.tp.tx_processed + ' / ' + s.tp.tx_frames;
  $('ipRx').textContent = s.ip_stats.rx_frames;
  $('ipTx').textContent = s.ip_stats.tx_frames;

  $('ssid').textContent = s.ssid;
  $('ip').textContent   = s.ip;
  $('ipSrc').textContent = t(s.ip_from_ets ? 'fest, aus der ETS' : 'automatisch (DHCP)');
  $('mask').textContent = s.netmask ? s.netmask + ' /' + prefixLen(s.netmask) : '-';
  $('gw').textContent   = orDash(s.gateway);
  $('dns').textContent  = orDash(s.dns);
  $('mac').textContent  = s.mac;
  $('rssi').textContent = (s.is_ap_mode || s.iface === 'ethernet') ? '-' : s.rssi + ' dBm';

  const ES = {ready:'verbunden', no_link:'kein Link', no_ip:'keine IP-Adresse',
              absent:'nicht gefunden', disabled:'nicht konfiguriert'};
  const est = t(ES[s.eth.state] || s.eth.state);
  const eok = s.eth.state === 'ready';
  $('ethSt').innerHTML = (s.eth.state === 'disabled' || s.eth.state === 'absent')
      ? '<span class="dim">' + est + '</span>'
      : (eok ? dot(true) + ' ' + s.eth.speed + ' Mbit/s ' + s.eth.duplex
             : '<span class="dot err"></span>' + est);

  const ifn = {ethernet:'Ethernet', wifi:'WLAN'};
  $('ifc').textContent = t(ifn[s.iface] || s.iface);

  $('chip').textContent = s.hardware.chip_model + ' rev ' + s.hardware.chip_rev;
  $('cpu').textContent  = s.hardware.cpu_freq + ' MHz';
  $('heap').textContent = Math.round(s.hardware.heap_free/1024) + ' / '
                        + Math.round(s.hardware.heap_total/1024) + ' KiB';
  // Both application slots with what is stored in each, so "switch" is an
  // informed decision rather than a leap.
  const PST = {valid:'geprueft', pending_verify:'auf Bewaehrung',
               new:'neu', invalid:'ungueltig', aborted:'abgebrochen',
               undefined:'ohne OTA-Vermerk'};
  const slot = p => p.label + ' \u00b7 '
      + (p.firmware || t(p.valid ? 'unbekannt' : 'leer'))
      + ' \u00b7 ' + t(PST[p.state] || p.state);

  const parts = s.build.partitions || [];
  $('partRun').textContent = parts[0] ? slot(parts[0]) : '-';
  $('partAlt').textContent = parts[1] ? slot(parts[1]) : '-';
  $('swBtn').disabled = !(parts[1] && parts[1].valid);
  $('ver').textContent  = s.build.version;
  $('build').textContent= '#' + s.build.number + ' / ' + s.build.git;

  const b = $('netBadge');
  b.textContent = s.is_ap_mode ? t('AP-Modus aktiv')
                : (s.iface === 'ethernet' ? 'Ethernet'
                : (s.wifi_connected ? s.ssid : t('getrennt')));
  b.className   = 'badge ' + (s.is_ap_mode ? 'warn' : (s.wifi_connected ? 'ok' : ''));
  b.style.cursor = (s.iface === 'ethernet') ? 'default' : 'pointer';
}

async function toggleProg(){
  await fetch('/api/progmode', {method:'POST'});
  setTimeout(refresh, 300);
}

async function setRouting(){
  const body = new URLSearchParams({unfiltered: $('rtAll').checked ? '1' : '0'});
  await fetch('/api/knx/routing', {method:'POST', body});
  setTimeout(refresh, 300);
}

async function setBeat(){
  const body = new URLSearchParams({enabled: $('beat').checked ? '1' : '0'});
  await fetch('/api/led/heartbeat', {method:'POST', body});
  setTimeout(refresh, 300);
}

async function showFilter(){
  $('filterInfo').textContent = t('lese Filtertabelle...');
  $('filterList').textContent = '';
  filterDlg.showModal();

  let f;
  try { f = await (await fetch('/api/knx/filter')).json(); }
  catch(e){ $('filterInfo').textContent = t('Lesen fehlgeschlagen.'); return; }

  if(!f.loaded){
    $('filterInfo').textContent = t('Keine Filtertabelle geladen \u2013 das Geraet '
      + 'ist nicht programmiert und sperrt jedes Gruppentelegramm.');
    return;
  }

  $('filterInfo').textContent = f.total
    ? t('%s Gruppenadressen werden weitergeleitet.').replace('%s', f.total)
      + (f.total > f.addresses.length
         ? ' ' + t('Angezeigt: die ersten %s.').replace('%s', f.addresses.length)
         : '')
    : t('Die Tabelle ist geladen, laesst aber nichts durch.');

  $('filterList').textContent = f.addresses.join('   ');
}

async function switchPart(){
  const p = ((last && last.build.partitions) || [])[1];

  let msg = t('Beim naechsten Start die andere Partition verwenden? '
            + 'Das Geraet startet neu.');

  // A slot holding a bootable image we have never run is fair game, but the
  // user should know they are jumping to something unidentified.
  if(p && !p.firmware){
    msg += '\n\n' + t('Achtung: Der Inhalt ist unbekannt. Dieser Speicherplatz '
         + 'hat unter der aktuellen Firmware noch nie gelaufen, dort liegt '
         + 'vermutlich ein aelterer Stand.');
  }

  if(!confirm(msg)) return;
  const r = await fetch('/api/ota/switch', {method:'POST'});
  alert(t(r.ok ? 'Umgeschaltet. Das Geraet startet neu.'
              : 'Umschalten nicht moeglich.'));
}

async function resetKnx(){
  if(!confirm(t('KNX-Konfiguration zur\u00fccksetzen?\n\n'
            + 'Physikalische Adresse und Tunnel-Adressen gehen verloren, '
            + 'das Ger\u00e4t startet neu und muss in der ETS neu programmiert '
            + 'werden.\n\nWLAN und Hardware-Profil bleiben erhalten.'))) return;
  try {
    const r = await fetch('/api/knx/reset', {method:'POST'});
    if(!r.ok){ alert(t('Zur\u00fccksetzen fehlgeschlagen.')); return; }
    alert(t('KNX-Konfiguration gel\u00f6scht. Das Ger\u00e4t startet neu.'));
  } catch(e){ alert(t('Zur\u00fccksetzen fehlgeschlagen.')); }
}

// --- Zeitserver ---
const SRC = {ntp:'NTP', rtc:'RTC', manual:'manuell', none:'keine'};

async function refreshTime(){
  let ts;
  try { ts = await (await fetch('/api/time')).json(); } catch(e){ return; }
  $('tsNow').textContent  = ts.local_time;
  $('tsSrc').innerHTML    = ts.clock_valid ? dot(true) + ' ' + t(SRC[ts.source]||ts.source)
                                           : '<span class="dot err"></span>' + t('nicht gesetzt');
  $('tsNtpAct').innerHTML = ts.ntp_enabled
      ? (ts.ntp_active ? ts.ntp_active + (ts.ntp_dhcp_active ? ' <small>(DHCP)</small>' : '')
                       : '<span class="dim">' + t('keiner') + '</span>')
      : '<span class="dim">' + t('aus') + '</span>';
  $('tsRtc').innerHTML    = ts.rtc_present ? dot(true)
                          : '<span class="dim">' + t('nicht bestueckt') + '</span>';
  $('tsNext').textContent = ts.enabled ? (ts.next_send_s + ' s') : t('deaktiviert');
  syncGroups();
  return ts;
}

async function openTime(){
  const ts = await refreshTime();
  if(!ts) return;
  $('tsEn').checked     = ts.enabled;
  $('tsGaDt').value     = ts.ga_datetime;
  $('tsGaT').value      = ts.ga_time;
  $('tsGaD').value      = ts.ga_date;
  $('tsIvl').value      = ts.interval_min;
  $('tsNtpEn').checked  = ts.ntp_enabled;
  $('tsNtpDhcp').checked= ts.ntp_from_dhcp;
  $('tsNtpSrv').value   = ts.ntp_server;
  $('tsTz').value       = ts.tz;
  const d = new Date(Date.now() - new Date().getTimezoneOffset()*60000);
  $('tsManual').value   = d.toISOString().slice(0,19);
  timeDlg.showModal();
}

async function saveTime(){
  const body = new URLSearchParams({
    enabled:      $('tsEn').checked ? '1' : '0',
    ga_datetime:  $('tsGaDt').value,
    ga_time:      $('tsGaT').value,
    ga_date:      $('tsGaD').value,
    interval_min: $('tsIvl').value,
    ntp_enabled:  $('tsNtpEn').checked ? '1' : '0',
    ntp_from_dhcp:$('tsNtpDhcp').checked ? '1' : '0',
    ntp_server:   $('tsNtpSrv').value,
    tz:           $('tsTz').value
  });
  await fetch('/api/time/config', {method:'POST', body});
  refreshTime();
}

// Browser-Zeit uebernehmen: Date.now() ist bereits UTC-basiert.
async function syncBrowser(){
  const body = new URLSearchParams({epoch: Math.floor(Date.now()/1000)});
  const r = await fetch('/api/time/set', {method:'POST', body});
  if(!r.ok) alert(t('Zeit konnte nicht gesetzt werden.'));
  setTimeout(refreshTime, 600);
}

// Manuelle Eingabe: datetime-local ist Ortszeit, daher in UTC umrechnen.
async function setManual(){
  const v = $('tsManual').value;
  if(!v){ alert(t('Bitte Datum und Uhrzeit eingeben.')); return; }
  const epoch = Math.floor(new Date(v).getTime()/1000);
  const body = new URLSearchParams({epoch});
  const r = await fetch('/api/time/set', {method:'POST', body});
  if(!r.ok) alert(t('Zeit konnte nicht gesetzt werden.'));
  setTimeout(refreshTime, 600);
}

async function sendTime(){
  const r = await fetch('/api/time/send', {method:'POST'});
  if(!r.ok) alert(t('Senden nicht moeglich - Uhr nicht gesetzt?'));
  setTimeout(refreshTime, 600);
}

// --- Hardware-Profil ---
const HWF = ['knx_uart','knx_rx','knx_tx',
             'i2c_sda','i2c_scl','eth_sck','eth_miso','eth_mosi',
             'eth_cs','eth_irq','eth_rst','eth_spi_mhz'];
const HWID = {knx_uart:'hwUart', knx_rx:'hwRx', knx_tx:'hwTx',
              i2c_sda:'hwSda', i2c_scl:'hwScl',
              eth_sck:'hwSck', eth_miso:'hwMiso', eth_mosi:'hwMosi',
              eth_cs:'hwCs', eth_irq:'hwIrq', eth_rst:'hwRst'};
let hwState = null;

// Row editors. Order matches the enums in hw_config.h - the index is what
// travels over the API, the text is only ever shown.
const TRIG  = ['kurzer Druck','langer Druck','sehr langer Druck'];
const LKIND = ['LED','RGB-LED'];
const RGBT  = ['WS2812','SK6812'];
const BFUNC = ['Programmiermodus','Werkeinstellungen','WLAN Grundeinstellung',
               'Geraet neu starten','WLAN ein/aus'];
const LCOND = ['Programmiermodus aktiv','AP-Modus offen','keine TP-Verbindung',
               'online','offline','Herzschlag'];
const LCOL  = ['rot','gruen','blau','gelb','cyan','magenta','weiss'];
const LPAT  = ['Dauerlicht','langsam blinken','schnell blinken','Doppelblitz',
               'kurzer Blitz'];

const RTBL = {btn:'hwBtnTbl', led:'hwLedTbl', ba:'hwBaTbl', la:'hwLaTbl'};
const RKEY = {btn:'buttons', led:'leds', ba:'button_assign', la:'led_assign'};
const ROWS = {btn:[], led:[], ba:[], la:[]};
const RSEL = {btn:-1, led:-1, ba:-1, la:-1};

const FBR = {unconfigured:'Image-Standard', invalid:'ungueltig &rarr; Standard',
             crashloop:'Startfehler &rarr; Standard'};

async function refreshHw(){
  try { hwState = await (await fetch('/api/hwconfig')).json(); }
  catch(e){ return; }
  const a = hwState.active;

  $('hwSrc').innerHTML = hwState.using_defaults
      ? '<span class="dot err"></span>' + t(FBR[hwState.fallback]||hwState.fallback)
      : dot(true) + ' ' + t('gespeichertes Profil');
  if(hwState.reboot_pending)
    $('hwSrc').innerHTML += ' <small>' + t('(Neustart noetig)') + '</small>';

  $('hwKnx').textContent = 'UART' + a.knx_uart + ', RX ' + a.knx_rx + ', TX ' + a.knx_tx;

  const named = (list, fn) => list.length
      ? list.map(fn).join(', ') : t('keine');
  $('hwBtns').textContent = named(a.buttons || [],
      b => b.name + ' (GPIO ' + b.pin + ', ' + t(TRIG[b.trigger] || '?') + ')');
  $('hwLeds').textContent = named(a.leds || [],
      l => l.name + ' (GPIO ' + l.pin
         + (l.kind == 1 ? ', ' + RGBT[l.rgb_type] + ' #' + l.rgb_index : '') + ')');
  $('hwI2c').textContent = a.i2c_enabled ? ('SDA ' + a.i2c_sda + ', SCL ' + a.i2c_scl)
                                         : t('aus');
  $('hwEth').textContent = a.eth_enabled
      ? ('SCK ' + a.eth_sck + ', MISO ' + a.eth_miso + ', MOSI ' + a.eth_mosi + ', CS ' + a.eth_cs)
      : t('aus');
}

function hwFill(p){
  for(const k of HWF){ if(HWID[k]) $(HWID[k]).value = p[k]; }
  $('hwI2cEn').checked  = p.i2c_enabled;
  $('hwEthEn').checked  = p.eth_enabled;
  // Deep copy: editing a row must not change the document we compare against
  // when the user hits "Standard" again.
  for(const kind in RTBL){
    ROWS[kind] = (p[RKEY[kind]] || []).map(o => Object.assign({}, o));
    RSEL[kind] = -1;
    rowRender(kind);
  }
  syncGroups();
}

/* --- Zeileneditoren ------------------------------------------------------ *
 * Die Zeilen leben in ROWS, die Tabelle ist nur die Anzeige. Vor jedem
 * Neuzeichnen wird das DOM zurueckgeschrieben, sonst gingen Eingaben beim
 * Hinzufuegen einer Zeile verloren.
 * ------------------------------------------------------------------------ */

function gpioOptions(list, sel){
  return (list || []).map(p =>
    '<option value="' + p + '"' + (p == sel ? ' selected' : '') + '>' + p + '</option>'
  ).join('');
}

function pickOptions(texts, sel){
  return texts.map((s, i) =>
    '<option value="' + i + '"' + (i == sel ? ' selected' : '') + '>' + t(s) + '</option>'
  ).join('');
}

function nameCell(v){
  // maxlength und pattern spiegeln nur, was die Firmware ohnehin prueft.
  return '<input maxlength="16" pattern="[A-Za-z0-9_-]{1,16}" value="' +
         esc(v == null ? '' : v) + '">';
}

function rowRender(kind){
  const tbl = $(RTBL[kind]);
  const gin = hwState ? hwState.gpio_in : [];
  const gout = hwState ? hwState.gpio_out : [];
  let html = '';

  ROWS[kind].forEach((r, i) => {
    const sel = (RSEL[kind] === i) ? ' class="sel"' : '';
    html += '<tr' + sel + ' onclick="rowPick(\'' + kind + '\',' + i + ')">';

    if(kind === 'btn'){
      html += '<td>' + nameCell(r.name) + '</td>';
      html += '<td><select>' + gpioOptions(gin, r.pin) + '</select></td>';
      html += '<td><select>' + pickOptions(TRIG, r.trigger) + '</select></td>';
    }
    else if(kind === 'led'){
      html += '<td>' + nameCell(r.name) + '</td>';
      html += '<td><select>' + gpioOptions(gout, r.pin) + '</select></td>';
      html += '<td><select onchange="rowSync(\'led\');rowRender(\'led\')">'
            + pickOptions(LKIND, r.kind) + '</select></td>';
      if(r.kind == 1){
        html += '<td><select>' + pickOptions(RGBT, r.rgb_type) + '</select></td>';
        html += '<td><input type="number" min="0" max="63" value="'
              + (r.rgb_index|0) + '"></td>';
      } else {
        html += '<td colspan="2"><label class="chk"><input type="checkbox"'
              + (r.active_low ? ' checked' : '') + '> low-aktiv</label></td>';
      }
    }
    else if(kind === 'ba'){
      const names = ROWS.btn.map(x => x.name);
      html += '<td><select>' + names.map(n =>
                '<option' + (n === r.target ? ' selected' : '') + '>' + esc(n) + '</option>'
              ).join('') + '</select></td>';
      html += '<td><select>' + pickOptions(BFUNC, r.function) + '</select></td>';
    }
    else {
      html += '<td><select onchange="rowSync(\'la\');rowRender(\'la\')">'
            + ROWS.led.map(l =>
                '<option' + (l.name === r.target ? ' selected' : '') + '>'
                + esc(l.name) + '</option>'
              ).join('') + '</select></td>';
      html += '<td><select>' + pickOptions(LCOND, r.condition) + '</select></td>';
      // Farbe hat nur bei einer adressierbaren LED eine Wirkung. Das Feld
      // bleibt sichtbar, damit die Spalten nicht springen, wird aber
      // gesperrt - so ist ohne Erklaerung klar, dass es hier nichts tut.
      const target = ROWS.led.find(l => l.name === r.target);
      const rgb = target && target.kind == 1;
      html += '<td><select' + (rgb ? '' : ' disabled') + '>'
            + pickOptions(LCOL, r.colour) + '</select></td>';
      html += '<td><select>' + pickOptions(LPAT, r.pattern) + '</select></td>';
    }
    html += '</tr>';
  });

  tbl.innerHTML = html;
}

/** DOM zurueck in ROWS schreiben. */
function rowSync(kind){
  const rows = $(RTBL[kind]).querySelectorAll('tr');

  rows.forEach((tr, i) => {
    const r = ROWS[kind][i];
    if(!r) return;
    const f = tr.querySelectorAll('input,select');

    if(kind === 'btn'){
      r.name    = f[0].value.trim();
      r.pin     = parseInt(f[1].value, 10);
      r.trigger = parseInt(f[2].value, 10);
    }
    else if(kind === 'led'){
      r.name = f[0].value.trim();
      r.pin  = parseInt(f[1].value, 10);
      r.kind = parseInt(f[2].value, 10);
      if(r.kind == 1){
        r.rgb_type  = parseInt(f[3].value, 10);
        r.rgb_index = parseInt(f[4].value, 10);
      } else {
        r.active_low = f[3].checked;
      }
    }
    else if(kind === 'ba'){
      r.target   = f[0].value;
      r.function = parseInt(f[1].value, 10);
    }
    else {
      r.target    = f[0].value;
      r.condition = parseInt(f[1].value, 10);
      r.colour    = parseInt(f[2].value, 10);
      r.pattern   = parseInt(f[3].value, 10);
    }
  });
}

function rowPick(kind, i){
  // Kein Neuzeichnen: ein Klick ins Namensfeld wuerde sonst den Cursor
  // verlieren, weil die Tabelle unter der Eingabe neu gebaut wird.
  RSEL[kind] = i;
  $(RTBL[kind]).querySelectorAll('tr')
               .forEach((tr, n) => tr.classList.toggle('sel', n === i));
}

const RMAX = {btn:8, led:8, ba:8, la:12};

function rowAdd(kind){
  rowSync(kind);
  if(ROWS[kind].length >= RMAX[kind]){
    $('hwErr').textContent = t('mehr Zeilen sind nicht moeglich');
    return;
  }
  if(kind === 'btn')
    ROWS.btn.push({name:'', pin:(hwState.gpio_in||[0])[0], trigger:0});
  else if(kind === 'led')
    ROWS.led.push({name:'', pin:(hwState.gpio_out||[0])[0], kind:0,
                   active_low:false, rgb_type:0, rgb_index:0});
  else if(kind === 'ba')
    ROWS.ba.push({target: ROWS.btn.map(x=>x.name)[0] || '', function:0});
  else
    ROWS.la.push({target: ROWS.led.map(x=>x.name)[0] || '',
                  condition:3, colour:1, pattern:0});
  RSEL[kind] = ROWS[kind].length - 1;
  rowRender(kind);
}

/* Nur bei der LED-Zuordnung sichtbar - dort ist die Reihenfolge die
 * Rangfolge, sonst spielt sie keine Rolle. */
function rowMove(kind, dir){
  rowSync(kind);
  const i = RSEL[kind];
  const j = i + dir;
  if(i < 0 || j < 0 || j >= ROWS[kind].length){
    $('hwErr').textContent = t('Bitte zuerst eine Zeile anklicken.');
    return;
  }
  const tmp = ROWS[kind][i];
  ROWS[kind][i] = ROWS[kind][j];
  ROWS[kind][j] = tmp;
  RSEL[kind] = j;
  rowRender(kind);
}

function rowDel(kind){
  rowSync(kind);
  const i = RSEL[kind];
  if(i < 0 || i >= ROWS[kind].length){
    $('hwErr').textContent = t('Bitte zuerst eine Zeile anklicken.');
    return;
  }
  ROWS[kind].splice(i, 1);
  RSEL[kind] = -1;
  rowRender(kind);
  // Eine geloeschte LED oder Taste darf in keiner Zuordnung stehenbleiben.
  if(kind === 'btn' || kind === 'led'){
    const other = (kind === 'btn') ? 'ba' : 'la';
    const names = ROWS[kind].map(x => x.name);
    ROWS[other] = ROWS[other].filter(a => names.includes(a.target));
    RSEL[other] = -1;
    rowRender(other);
  }
}

async function openHw(){
  await refreshHw();
  if(!hwState) return;
  hwFill(hwState.has_stored ? hwState.stored : hwState.active);
  $('hwChip').textContent = hwState.chip + ' \u2013 GPIO 0..' + (hwState.gpio_count-1)
                          + ', UART 0..' + (hwState.uart_count-1);
  $('hwErr').textContent = '';
  hwDlg.showModal();
}

function hwDefaults(){ if(hwState) hwFill(hwState.defaults); }

function hwCollect(){
  const p = {};
  for(const k of HWF){ if(HWID[k]) p[k] = parseInt($(HWID[k]).value, 10); }
  p.i2c_enabled    = $('hwI2cEn').checked;
  p.eth_enabled    = $('hwEthEn').checked;
  for(const kind in RTBL){
    rowSync(kind);
    p[RKEY[kind]] = ROWS[kind];
  }
  return p;
}

async function hwPost(profile){
  const r = await fetch('/api/hwconfig', {
    method:'POST', headers:{'Content-Type':'application/json'},
    body: JSON.stringify(profile)
  });
  if(r.ok){
    hwDlg.close();
    refreshHw();
    if(confirm(t('Profil gespeichert. Jetzt neu starten?'))) doReboot();
    return true;
  }
  let msg = t('abgelehnt');
  try { const j = await r.json(); if(j.error) msg = j.error; } catch(e){}
  $('hwErr').textContent = msg;
  return false;
}

function hwSave(){ return hwPost(hwCollect()); }

async function hwReset(){
  if(!confirm(t('Gespeichertes Profil verwerfen und die Werte des Images verwenden?'))) return;
  await fetch('/api/hwconfig/reset', {method:'POST'});
  hwDlg.close();
  refreshHw();
  if(confirm(t('Zurueckgesetzt. Jetzt neu starten?'))) doReboot();
}

function hwDownload(){
  if(!hwState) return;
  const blob = new Blob([JSON.stringify(hwState.active, null, 2)],
                        {type:'application/json'});
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = 'sbip-hardware.json';
  a.click();
  URL.revokeObjectURL(a.href);
}

async function hwUpload(){
  const f = $('hwFile').files[0];
  if(!f) return;
  let p;
  try { p = JSON.parse(await f.text()); }
  catch(e){ alert(t('Keine gueltige JSON-Datei.')); return; }
  if(!await hwPost(p)) { hwDlg.showModal(); hwFill(p); }
}

async function doReboot(){
  await fetch('/api/reboot', {method:'POST'});
  alert(t('Neustart laeuft. Die Seite in einigen Sekunden neu laden.'));
}

function openWifi(){
  // Im Ethernet-Betrieb ist das WLAN gar nicht gestartet.
  if(last && last.iface === 'ethernet'){
    alert(t('Das Geraet laeuft ueber Ethernet. WLAN ist nicht aktiv.'));
    return;
  }
  wifiDlg.showModal(); scan();
}

async function scan(){
  const sel = $('ssidSel');
  sel.innerHTML = '<option>' + t('Suche laeuft...') + '</option>';
  await fetch('/api/wifi/scan?start=1');
  for(let i=0;i<20;i++){
    await new Promise(r=>setTimeout(r,900));
    const r = await (await fetch('/api/wifi/scan')).json();
    if(Array.isArray(r)){
      sel.innerHTML = r.length
        ? r.map(n=>`<option value="${n.ssid}">${n.ssid} (${n.rssi} dBm)</option>`).join('')
        : '<option>' + t('Kein Netz gefunden') + '</option>';
      return;
    }
  }
  sel.innerHTML = '<option>' + t('Zeitueberschreitung') + '</option>';
}

async function connect(){
  const body = new URLSearchParams({ssid:$('ssidSel').value, password:$('pw').value});
  await fetch('/api/wifi/connect', {method:'POST', body});
  wifiDlg.close();
  alert(t('Zugangsdaten gespeichert. Das Geraet startet neu.'));
}

async function checkUpdate(){
  $('updState').textContent = t('pruefe...');
  await fetch('/api/update/check');
  pollUpdate();
}

async function installUpdate(){
  if(!confirm(t('Firmware jetzt installieren? Das Geraet startet danach neu.'))) return;
  await fetch('/api/update/install', {method:'POST'});
  pollUpdate();
}

async function pollUpdate(){
  const u = await (await fetch('/api/update/status')).json();
  let text = u.state;
  if(u.error) text += ' - ' + u.error;
  else if(u.available) text = t('Version %s verfuegbar').replace('%s', u.latest);
  $('updState').textContent = text;
  $('instBtn').disabled = !u.available;

  if(u.state === 'installing'){
    $('updBar').style.display = 'block';
    $('updFill').style.width = (u.total ? (100*u.progress/u.total) : 0) + '%';
    setTimeout(pollUpdate, 700);
  } else if(u.state === 'checking'){
    setTimeout(pollUpdate, 700);
  } else {
    $('updBar').style.display = 'none';
  }
}

/*
 * SHA-256 im Browser.
 *
 * crypto.subtle gibt es nur im "secure context" - also HTTPS oder localhost.
 * Ueber http://<IP>/ ist die API schlicht nicht vorhanden. Dann bleibt nur
 * die manuelle Eingabe des Hashes.
 */
const CAN_HASH = !!(window.crypto && window.crypto.subtle);

async function sha256Hex(file){
  const buf = await file.arrayBuffer();
  const dig = await crypto.subtle.digest('SHA-256', buf);
  return [...new Uint8Array(dig)].map(b=>b.toString(16).padStart(2,'0')).join('');
}

async function upload(){
  const file = $('fw').files[0];
  if(!file) return;
  // Clear the picker, otherwise choosing the same file after a failed attempt
  // fires no change event and the button appears dead.
  $('fw').value = '';

  let hash = $('fwHash').value.trim().toLowerCase();

  if(!hash && CAN_HASH){
    $('updState').textContent = t('berechne Pruefsumme...');
    try { hash = await sha256Hex(file); $('fwHash').value = hash; }
    catch(e){ hash = ''; }
  }

  if(!hash && !confirm(t('Kein SHA-256 angegeben. Firmware ungeprueft uebertragen?'))) return;

  $('updState').textContent = t('lade hoch...');
  const fd = new FormData();
  fd.append('firmware', file);

  const headers = {};
  if(/^[0-9a-f]{64}$/.test(hash)) headers['X-SHA256'] = hash;

  let r;
  try { r = await fetch('/api/ota', {method:'POST', body:fd, headers}); }
  catch(e){ $('updState').textContent = t('Uebertragung abgebrochen'); return; }

  if(r.ok){
    $('updState').textContent = t('erfolgreich, Neustart...');
  } else {
    let msg = t('fehlgeschlagen');
    try { const j = await r.json(); if(j.error) msg += ' - ' + j.error; } catch(e){}
    $('updState').textContent = msg;
  }
}

applyLang();
setInterval(refresh, 2000);
setInterval(refreshTime, 5000);
</script>
</body>
</html>
)HTML";
