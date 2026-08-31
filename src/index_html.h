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
#infoBtn{padding:5px 0;width:28px;font-size:13px;font-style:italic;
font-weight:700;border-radius:50%;margin-left:6px}
/* Ein ausstehender Neustart ist kein Nebensatz - solange er offen ist,
 * laeuft das Geraet nicht auf dem, was in der Maske steht. */
.warnbadge{display:inline-block;padding:2px 8px;border-radius:20px;
font-size:11px;font-weight:600;background:var(--err);color:#fff}
.bar{height:6px;background:var(--line);border-radius:3px;overflow:hidden;
margin-top:8px;position:relative}
.bar>i{display:block;height:100%;background:var(--acc);width:0;transition:width .3s}
/* Hoechststand seit dem letzten Ruecksetzen. Innen liegend, damit er am
 * rechten Anschlag nicht vom overflow abgeschnitten wird. */
.bar>b{position:absolute;top:0;bottom:0;left:0;width:2px;margin-left:-2px;
background:var(--warn);opacity:.9;transition:left .3s}
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
/* Ohne eigene Regel nimmt der Browser sein Standardblau - auf dem dunklen
 * Untergrund kaum lesbar, und besuchte Links faerbten sich auch noch um. */
a{color:var(--acc);text-decoration:none}
a:hover{text-decoration:underline}
/* Quellenliste: Name links als Link, Lizenzkuerzel rechts als Nebenangabe. */
#infoDlg{max-width:min(92vw,460px)}
.cred .row span{color:var(--dim);white-space:nowrap}
dialog label{display:block;margin-top:10px;font-size:12px;color:var(--dim)}
/* Option groups: the checkbox heads the box it switches on, so it no longer
 * reads as just another field caption floating above unrelated inputs. */
.grp{border:1px solid var(--line);border-radius:8px;padding:12px;margin:14px 0}
dialog .grp>label:not(.chk):first-child{margin-top:0}
/* Ueberschrift der Gruppe. Bleibt sichtbar, wenn der Schalter darunter den
 * Rest abblendet - sonst waere nicht mehr zu lesen, was da abgeblendet ist. */
.grp>label.hd{color:var(--fg);font-weight:600}
.grp.off>*:not(.hd):not(.chk){opacity:.45}
dialog label.chk{display:flex;align-items:center;gap:9px;margin:0;
font-size:13px;color:var(--fg)}
input[type=checkbox]{width:auto;margin:0;flex:0 0 auto;accent-color:var(--acc)}
/* Feldraster: jedes Eingabefeld traegt seine eigene Beschriftung, sonst muss
 * man von "SCK / MISO / MOSI" auf drei Kaesten abzaehlen. Bricht um, wenn der
 * Platz nicht reicht. */
.fields{display:flex;gap:8px;flex-wrap:wrap;align-items:flex-end;margin:6px 0}
.fields>div{flex:1 1 96px}
.fields label{margin:0 0 3px;display:block}
.fields input,.fields select{margin:0}
/* Schieberegler tragen den Rahmen der Textfelder nicht, sonst sitzt der
 * Griff in einem Kasten. */
input[type=range]{width:100%;margin:8px 0 0;padding:0;border:0;height:18px;
background:none;accent-color:var(--acc)}
/* Gestapelter Balken fuer die PSRAM-Aufteilung: sehen ist schneller als
 * zwei Zahlen gegen eine dritte zu rechnen. */
.membar{display:flex;height:12px;border-radius:6px;overflow:hidden;
background:var(--bg);border:1px solid var(--line);margin:10px 0 4px}
.membar i{display:block;min-width:0}
#memLog{background:var(--acc)}
#memMon{background:var(--warn)}
#memRes{background:var(--dim);opacity:.45}
/* Am PC zieht sich der Profildialog sonst ueber den ganzen Schirm, und die
 * Eingabefelder werden absurd breit. */
#hwDlg{max-width:min(94vw,720px)}
#timeDlg{max-width:min(94vw,620px)}
/* Row editors. The selected row is what the minus button acts on, so it has
 * to be obvious which one that is - a border alone is too quiet here. */
#logDlg{max-width:min(96vw,900px);width:900px}
#logText{height:60vh;overflow:auto;white-space:pre-wrap;word-break:break-word;
font-family:ui-monospace,Consolas,monospace;font-size:12px;line-height:1.45;
background:var(--bg);border:1px solid var(--line);border-radius:7px;padding:10px;
margin:6px 0}
/* Busmonitor. Eigene Tabelle statt table.rows: die Zeilen sind nicht
 * auswaehlbar und die Spalten sollen nicht springen, wenn eine Adresse
 * einstellig wird. */
#monDlg{max-width:min(98vw,1100px);width:1100px}
#monBox{height:56vh;overflow:auto;background:var(--bg);border:1px solid var(--line);
border-radius:7px;margin:6px 0}
table.mon{width:100%;border-collapse:collapse;
font-family:ui-monospace,Consolas,monospace;font-size:12px;
font-variant-numeric:tabular-nums}
table.mon th{position:sticky;top:0;background:var(--card);text-align:left;
padding:6px 7px;font-size:10px;letter-spacing:.4px;text-transform:uppercase;
color:var(--dim);border-bottom:1px solid var(--line);font-family:inherit}
table.mon td{padding:3px 7px;border-bottom:1px solid rgba(43,52,68,.5);
white-space:nowrap}
table.mon td.w{white-space:normal;word-break:break-all}
/* Zwei Kanaele, damit die Farbe nicht bloss wiederholt, was daneben steht:
 * die linke Kante nennt die Seite, die Tuenung der Zeile die Richtung. */
table.mon tbody tr{cursor:pointer}
table.mon tr.tx td{background:rgba(210,153,34,.10)}
table.mon tr.tp td:first-child{box-shadow:inset 3px 0 0 var(--ok)}
table.mon tr.ip td:first-child{box-shadow:inset 3px 0 0 var(--acc)}
table.mon tr.tun td:first-child{box-shadow:inset 3px 0 0 var(--warn)}
table.mon tbody tr:hover td{background:rgba(255,255,255,.06)}
/* Alles zur angeklickten Gruppenadresse. Nach dem Hover notiert, damit die
 * Auswahl beim Ueberfahren nicht verschwindet. */
table.mon tr.mark td{background:rgba(94,168,255,.20)}
table.mon td.val{color:var(--fg)}
table.mon td.dim{color:var(--dim)}
.fields.wide>div{flex:1 1 130px}
table.rows{width:100%;border-collapse:collapse;margin:6px 0 4px}table.rows td{padding:2px 3px}
table.rows tr.hd th{padding:2px 4px 5px;text-align:left;font-size:11px;
font-weight:600;letter-spacing:.4px;text-transform:uppercase;color:var(--dim);
border-bottom:1px solid var(--line);cursor:help}
table.rows tr{cursor:pointer}
table.rows tr.sel td{background:rgba(94,168,255,.14)}
table.rows tr.sel td:first-child{box-shadow:inset 3px 0 0 var(--acc)}
table.rows input,table.rows select{margin:2px 0;padding:6px;font-size:13px}
table.rows input[type=checkbox]{margin:0 0 0 6px}
.rowbtns{display:flex;gap:8px}
button.ico{width:38px;padding:7px 0;font-size:16px;line-height:1;flex:0 0 auto}
/* Piktogramme als Strichzeichnung statt Unicode-Glyphen: U+23F0 und Verwandte
 * rendern auf vielen Systemen als Farb-Emoji und passen dann weder zum Thema
 * noch zueinander. currentColor haelt sie an der Schaltflaechenfarbe. */
button.ico svg{display:block;width:16px;height:16px;margin:0 auto;
fill:none;stroke:currentColor;stroke-width:1.5;
stroke-linecap:round;stroke-linejoin:round}
button.ico svg .sol{fill:currentColor;stroke:none}
/* Eine Checkbox-Zeile direkt unter einer Schaltflaechenreihe oder einem
 * Absatz klebte am Vorgaenger - sie braucht denselben Luftraum wie ein
 * eigener Abschnitt. */
.actions + label.chk, p + label.chk, .bar + label.chk,
.fields + label.chk{margin-top:14px}
label.chk{display:flex;align-items:center;gap:9px;font-size:13px}
small{color:var(--dim)}
</style>
</head>
<body>
<header>
  <h1>Selfbus KNX/IP Interface</h1>
  <span class="badge" id="knxBadge" data-dyn></span>
  <span class="badge" id="netBadge" onclick="openWifi()">...</span>
  <button class="sec" id="langBtn" onclick="toggleLang()"
          title="Sprache / Language">EN</button>
  <button class="sec" id="infoBtn" onclick="infoDlg.showModal()"
          title="Über dieses Projekt">i</button>
</header>

<main>
  <section class="card">
    <h2>Status</h2>
    <div class="row"><span>Laufzeit</span><span id="uptime">-</span></div>
    <div class="row"><span>Betriebsstunden</span><span id="hours">-</span></div>
    <div class="row"><span>Physikalische Adresse</span><span id="pa">-</span></div>
    <div class="row"><span>Name in der ETS</span><span id="knxName">-</span></div>
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
      Alle Gruppentelegramme weiterleiten</label>
    <p><small id="rtNote" data-dyn></small></p>
    <p><small>Solange die ETS das Gerät nicht programmiert hat, leitet es
    ohnehin alles weiter &ndash; ohne Download gibt es keine Filtertabelle,
    und ein Koppler ohne Tabelle sperrt sonst jedes Gruppentelegramm. Mit dem
    ersten Download übernimmt die Tabelle von selbst.</small></p>
    <p><small>Der Schalter übersteuert auch das: Er lässt eine geladene
    Filtertabelle ignorieren und jedes Gruppentelegramm passieren. Nur
    einschalten, wenn dieses Gerät die einzige Verbindung zwischen Linie und
    IP ist &ndash; sonst drohen Telegrammschleifen.</small></p>
  </section>

  <section class="card">
    <h2>KNX TP1 &ndash; SB-Interface</h2>
    <div class="row"><span>Verbindung</span><span id="tpConn">-</span></div>
    <div class="row"><span>Schnittstelle</span><span id="tpType">-</span></div>
    <div class="row"><span>Baudrate</span><span id="tpBaud">-</span></div>
    <div class="row"><span>Selbsttest</span><span id="tpTest">-</span></div>
    <div class="row"><span>Buslast</span><span id="tpLoad">-</span></div>
    <div class="bar"><i id="tpLoadBar"></i><b id="tpLoadPeak"></b></div>
    <div class="actions">
      <button class="sec" onclick="resetPeaks('bus')">Spitzenwert zur&uuml;cksetzen</button>
      <button class="sec" id="ispBtn" style="display:none"
              onclick="openLpc()">SB-Interface programmieren</button>
    </div>
  </section>

  <section class="card">
    <h2>Telegramme</h2>
    <div class="row"><span>TP empfangen</span><span id="tpRx">-</span></div>
    <div class="row"><span>TP verworfen (fremd)</span><span id="tpIgn">-</span></div>
    <div class="row"><span>TP ungültig</span><span id="tpInv">-</span></div>
    <div class="row"><span>TP gesendet</span><span id="tpTx">-</span></div>
    <div class="row"><span>IP empfangen</span><span id="ipRx">-</span></div>
    <div class="row"><span>IP gesendet</span><span id="ipTx">-</span></div>
    <div class="row"><span>Busmonitor</span><span id="monSt">-</span></div>
    <div class="actions">
      <button class="sec" onclick="openMon()">Busmonitor</button>
    </div>
  </section>

  <section class="card">
    <h2>Zeitserver</h2>
    <div class="row"><span>Aktuelle Zeit</span><span id="tsNow">-</span></div>
    <div class="row"><span>Zeitquelle</span><span id="tsSrc">-</span></div>
    <div class="row"><span>NTP-Server</span><span id="tsNtpAct">-</span></div>
    <div class="row"><span>RTC (RV-3028)</span><span id="tsRtc">-</span></div>
    <div class="row"><span>Nächstes Senden</span><span id="tsNext">-</span></div>
    <div class="actions">
      <button class="sec" onclick="openTime()">Einstellungen</button>
      <button class="sec" onclick="syncBrowser()">Zeit vom Browser</button>
      <button class="sec" onclick="sendTime()">Jetzt senden</button>
    </div>
  </section>

  <section class="card">
    <h2>Netzwerk</h2>
    <div class="row"><span>Gerätename</span><span id="devName">-</span></div>
    <div class="row"><span>Hostname</span><span id="hostName">-</span></div>
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
    <label class="chk" id="fbRow" title="WLAN &uuml;bernimmt bei getrenntem Ethernet">
      <input type="checkbox" id="wifiFb" onchange="setFallback()">
      WLAN-Ersatzverbindung</label>
    <div class="actions">
      <button class="sec" onclick="openWifi()">WLAN einrichten</button>
      <button class="sec" onclick="openName()">Name &auml;ndern</button>
    </div>
  </section>

  <section class="card">
    <h2>System</h2>
    <div class="row"><span>Chip</span><span id="chip">-</span></div>
    <div class="row"><span>Takt</span><span id="cpu">-</span></div>
    <div class="row"><span>Last Kern 0</span><span id="cpu0">-</span></div>
    <div class="bar"><i id="cpu0Bar"></i><b id="cpu0Peak"></b></div>
    <div class="row"><span>Last Hauptschleife</span><span id="loopLoad">-</span></div>
    <div class="bar"><i id="loopLoadBar"></i><b id="loopLoadPeak"></b></div>
    <div class="row"><span>L&auml;ngster Durchlauf</span><span id="loopMax">-</span></div>
    <div class="row"><span>Freier Speicher</span><span id="heap">-</span></div>
    <div class="row"><span>Flash</span><span id="flash">-</span></div>
    <div class="row"><span>PSRAM</span><span id="psram">-</span></div>
    <div class="row"><span>Protokollpuffer</span><span id="logbuf">-</span></div>
    <div class="row"><span>Zugangsschutz</span><span id="authSt">-</span></div>
    <div class="actions">
      <button class="sec" onclick="showParts()">Partitionstabelle</button>
      <button class="sec" onclick="showLog()">Protokoll</button>
      <button class="sec" onclick="openAuth()">Zugang</button>
      <button class="sec" onclick="resetPeaks('cpu')">Spitzenwerte zur&uuml;cksetzen</button>
      <button class="sec" onclick="resetHours()">Betriebsstunden zur&uuml;cksetzen</button>
      <button class="sec" onclick="askReboot()"
              title="Offene ETS-Verbindungen reißen dabei ab">Ger&auml;t neu starten</button>
      <button class="sec" onclick="factoryReset()"
              title="Löscht den gesamten NVS-Speicher">Werkseinstellungen</button>
    </div>
  </section>

  <section class="card">
    <h2>Hardware-Profil</h2>
    <div class="row"><span>Quelle</span><span id="hwSrc">-</span></div>
    <div class="row"><span>KNX-UART</span><span id="hwKnx">-</span></div>
    <div class="row"><span>SB-Interface ISP</span><span id="hwLpc">-</span></div>
    <div class="row"><span>Taster</span><span id="hwBtns">-</span></div>
    <div class="row"><span>LEDs</span><span id="hwLeds">-</span></div>
    <div class="row"><span>I2C (RTC)</span><span id="hwI2c">-</span></div>
    <div class="row"><span>SPI (W5500)</span><span id="hwEth">-</span></div>
    <div class="row"><span>PSRAM-Aufteilung</span><span id="hwMem">-</span></div>
    <div class="row"><span>Online-Update</span><span id="hwUpd">-</span></div>
    <div class="row"><span>SB-Interface-Download</span><span id="hwLpcDl">-</span></div>
    <div class="actions">
      <button class="sec" onclick="openHw()">Bearbeiten</button>
      <button class="sec" onclick="hwFile.click()">JSON laden</button>
      <button class="sec" onclick="hwDownload()">JSON speichern</button>
      <input type="file" id="hwFile" accept=".json" style="display:none" onchange="hwUpload()">
    </div>
    <label class="chk" id="beatRow" style="margin-top:14px;display:none">
      <input type="checkbox" id="beat" onchange="setBeat()">
      Heartbeat aktiv</label>
    <div id="brightRow" style="display:none">
      <label>LED-Helligkeit <span id="brightVal" data-dyn></span></label>
      <input type="range" id="bright" min="1" max="100"
             oninput="$('brightVal').textContent = this.value + ' %'"
             onchange="setBright()">
    </div>
    <p><small>Änderungen am Profil werden erst nach einem Neustart aktiv.
    Reine Zuordnungen wirken sofort.</small></p>
  </section>

  <section class="card">
    <h2>Firmware</h2>
    <div class="row"><span>Version</span><span id="ver">-</span></div>
    <div class="row"><span>Build</span><span id="build">-</span></div>
    <div class="row"><span>Aktive Partition</span><span id="partRun">-</span></div>
    <div class="row"><span>Zweite Partition</span><span id="partAlt">-</span></div>
    <div class="row"><span>Update</span><span id="updState">-</span></div>
    <div class="bar" id="updBar" style="display:none"><i id="updFill"></i></div>
    <div class="actions">
      <button class="sec" onclick="checkUpdate()">Online prüfen</button>
      <button class="sec" id="instBtn" onclick="installUpdate()" disabled>Installieren</button>
      <button class="sec" onclick="fw.click()">Datei hochladen</button>
      <input type="file" id="fw" accept=".bin" style="display:none" onchange="upload()">
      <button class="sec" id="swBtn" onclick="switchPart()">Partition wechseln</button>
    </div>
    <label style="display:block;margin-top:10px;font-size:12px;color:var(--dim)">
      SHA-256 der Datei (optional, aus <code>sha256sum</code>)</label>
    <input id="fwHash" placeholder="64 Hex-Zeichen &ndash; leer = ungeprüft">
    <p><small id="hashNote" data-dyn></small></p>
  </section>
</main>

<dialog id="authDlg">
  <h2>Zugangsschutz</h2>
  <label>Benutzername (leer lässt die Oberfläche offen)</label>
  <input id="authUser" maxlength="31" autocomplete="username">
  <label>Passwort (mindestens 8 Zeichen)</label>
  <input id="authPass" type="password" autocomplete="new-password">
  <p id="authErr" style="color:var(--err);font-size:12px"></p>
  <div class="actions">
    <button onclick="saveAuth()">Speichern</button>
    <button class="sec" onclick="authDlg.close()">Abbrechen</button>
  </div>
  <p><small>Digest-Verfahren: Das Passwort wird nicht im Klartext übertragen.
  Alles andere schon &ndash; ohne HTTPS schützt das vor unbefugtem Zugriff im
  Netz, nicht vor Mitlesen. Wirkt nach einem Neustart. Setzbar nur, wenn ein
  Taster auf Werkeinstellungen liegt: Das ist der einzige Weg zurück, wenn
  das Passwort verloren geht.</small></p>
</dialog>

<dialog id="nameDlg">
  <h2>Gerätename</h2>
  <label>1 bis 31 Zeichen aus A-Z a-z 0-9 und Bindestrich</label>
  <input id="nameIn" maxlength="31" pattern="[A-Za-z0-9-]{1,31}">
  <p id="nameErr" style="color:var(--err);font-size:12px"></p>
  <div class="actions">
    <button onclick="saveName()">Speichern</button>
    <button class="sec" onclick="nameDlg.close()">Abbrechen</button>
  </div>
  <p><small>Unterscheidet mehrere Router in derselben Anlage. Der Name ist
  zugleich der mDNS-Hostname und steht im Protokoll. Wird nach einem Neustart
  wirksam.</small></p>
</dialog>

<dialog id="logDlg">
  <h2>Protokoll</h2>
  <pre id="logText"></pre>
  <p><small id="logInfo" data-dyn></small></p>
  <label class="chk">
    <input type="checkbox" id="logKeep" onchange="setKeep()">
    Neustart überdauern (RTC-Speicher)</label>
  <div class="rowbtns" style="margin-top:12px">
    <button class="sec ico" onclick="logJump(0)" title="An den Anfang"><svg
      viewBox="0 0 16 16"><path d="M3.5 2.5h9"/><path d="M8 13.5V5.3"/>
      <path d="M4.6 8.7 8 5.3l3.4 3.4"/></svg></button>
    <button class="sec ico" onclick="logJump(1)" title="An das Ende"><svg
      viewBox="0 0 16 16"><path d="M3.5 13.5h9"/><path d="M8 2.5v8.2"/>
      <path d="M4.6 7.3 8 10.7l3.4-3.4"/></svg></button>
    <button class="sec ico" id="logFollow" onclick="toggleFollow()"
            title="Laufend aktualisieren"></button>
    <button class="sec ico" onclick="loadLog()" title="Aktualisieren"><svg
      viewBox="0 0 16 16"><path d="M2.7 8a5.3 5.3 0 0 1 9-3.8"/>
      <path d="M13.3 8a5.3 5.3 0 0 1-9 3.8"/><path d="M11.9 1.9v2.6H9.3"/>
      <path d="M4.1 14.1v-2.6h2.6"/></svg></button>
    <button class="sec ico" id="logCopy" onclick="copyLog()"
            title="In die Zwischenablage"></button>
    <button class="sec ico" onclick="downloadLog()"
            title="Als Datei speichern"><svg viewBox="0 0 16 16"
      ><path d="M2.6 3.4a.8.8 0 0 1 .8-.8h7.3l2.7 2.7v7.3a.8.8 0 0 1-.8.8H3.4a.8.8 0 0 1-.8-.8z"/>
      <path d="M5.2 2.6v3.6h5.2V2.6"/><path d="M4.6 9.2h6.8v4.2H4.6z"/></svg></button>
    <button class="sec ico" onclick="clearLog()" title="Leeren"><svg
      viewBox="0 0 16 16"><path d="M2.8 4.3h10.4"/>
      <path d="M6.3 4.3V3a.9.9 0 0 1 .9-.9h1.6a.9.9 0 0 1 .9.9v1.3"/>
      <path d="m4.3 4.3.6 8.6a1 1 0 0 0 1 .9h4.2a1 1 0 0 0 1-.9l.6-8.6"/>
      <path d="M6.8 6.7v4.6M9.2 6.7v4.6"/></svg></button>
    <button class="sec" onclick="closeLog()">Schließen</button>
  </div>
  <p><small>Das Fenster zeigt einen Ausschnitt des Ringpuffers und lädt beim
  Blättern nach. Kopiert wird die Markierung, sonst der sichtbare Ausschnitt;
  gespeichert wird der ganze Puffer.</small></p>
</dialog>

<dialog id="partDlg">
  <h2>Partitionstabelle</h2>
  <table class="rows" id="partList"></table>
  <div class="actions">
    <button class="sec" onclick="partDlg.close()">Schließen</button>
  </div>
</dialog>

<dialog id="monDlg">
  <h2>Busmonitor</h2>

  <div class="grp">
    <label class="hd">Aufzeichnen</label>
    <div class="fields wide">
      <div><label>Seite</label>
        <select id="monSides">
          <option value="tp,ip,tunnel">alle</option>
          <option value="tp">nur TP</option>
          <option value="ip">nur IP</option>
          <option value="tunnel">nur Tunnel</option>
        </select></div>
      <div><label>Start</label>
        <select id="monMode" onchange="monModeChanged()">
          <option value="now">sofort</option>
          <option value="ga">bei Gruppenadresse</option>
          <option value="repeat">bei Wiederholung</option>
          <option value="secure">bei KNX Data Secure</option>
        </select></div>
      <div><label>Trigger-Gruppenadresse</label>
        <input id="monTrig" placeholder="z.B. 1/2/3"></div>
      <div><label>Telegramme davor</label>
        <input id="monPre" type="number" min="0" value="0"></div>
      <div><label>Telegramme danach (0 = alle)</label>
        <input id="monPost" type="number" min="0" value="0"></div>
    </div>
    <label class="chk"><input type="checkbox" id="monStopFull">
      Bei vollem Puffer anhalten statt zu überschreiben</label>
    <p><small>Anhalten und wieder starten führt die Aufzeichnung fort &ndash;
    geleert wird nur auf Knopfdruck. Nicht anbieten kann das Gerät die
    ETS-Auslöser über Quittungen und ungültige Telegramme: Das SB-Interface
    quittiert selbst und verwirft Fehlerhaftes, bevor der Stack es
    sieht.</small></p>
    <div class="rowbtns" style="margin-top:12px">
      <button id="monRun" onclick="monToggle()">Start</button>
      <button class="sec" onclick="monClear()">Leeren</button>
      <button class="sec ico" id="monFollow" onclick="monToggleFollow()"
              title="Laufend aktualisieren"></button>
    </div>
  </div>

  <div class="grp">
    <label class="hd">Anzeige filtern</label>
    <div class="fields wide">
      <div><label>Seite</label>
        <select id="fSide" onchange="monRender()">
          <option value="">alle</option>
          <option value="1">TP</option>
          <option value="0">IP</option>
          <option value="2">Tunnel</option>
        </select></div>
      <div><label>Richtung</label>
        <select id="fDir" onchange="monRender()">
          <option value="">alle</option>
          <option value="0">empfangen</option>
          <option value="1">gesendet</option>
        </select></div>
      <div><label>Adressart</label>
        <select id="fKind" onchange="monRender()">
          <option value="">alle</option>
          <option value="1">Gruppe</option>
          <option value="0">physikalisch</option>
        </select></div>
      <div><label>Quelle enthält</label>
        <input id="fSrc" oninput="monRender()" placeholder="1.1."></div>
      <div><label>Ziel enthält</label>
        <input id="fDst" oninput="monRender()" placeholder="1/2/"></div>
      <div><label>Dienst enthält</label>
        <input id="fSvc" oninput="monRender()" placeholder="Write"></div>
    </div>
  </div>

  <div class="grp">
    <label class="hd">Telegramm senden</label>
    <div class="fields wide">
      <div><label>Gruppenadresse</label>
        <input id="wGa" placeholder="1/2/3"></div>
      <div><label>Wert (hex)</label>
        <input id="wVal" placeholder="01" oninput="wSync()"></div>
    </div>
    <label class="chk"><input type="checkbox" id="wShort" checked>
      Kurze Form (Wert in der APCI, wie ETS bei Schalten und Dimmen)</label>
    <p><small>Das Gerät kennt keine Datenpunkttypen &ndash; die Filtertabelle
    enthält nur Adressen. Der Wert geht als rohe Oktette hinaus, deuten muss
    ihn der Empfänger. Ein Schaltbefehl ist 01 beziehungsweise 00, ein
    Prozentwert 80 für 50 %.</small></p>
    <div class="rowbtns" style="margin-top:12px">
      <button onclick="wSend()">Senden</button>
      <span id="wMsg"></span>
    </div>
  </div>

  <p><small id="monInfo" data-dyn></small></p>
  <div id="monBox"><table class="mon" id="monTbl"></table></div>

  <div class="actions">
    <button class="sec" onclick="monLoad(0)">Ältere laden</button>
    <button class="sec" onclick="monLoad(1)">Neueste laden</button>
    <button class="sec" onclick="monDownload()">Als CSV speichern</button>
    <button class="sec" onclick="monClose()">Schließen</button>
  </div>
  <p><small>Der Ring liegt im PSRAM; ohne PSRAM bleibt der Monitor aus und
  es hilft nur der Gruppenmonitor der ETS. Angezeigt wird immer nur ein
  Ausschnitt &ndash; der Filter wirkt auf das Geladene, nicht auf die
  Aufzeichnung.</small></p>
  <p><small>Farben: die linke Kante nennt die Seite (gr&uuml;n TP, blau IP),
  die get&ouml;nte Zeile das Senden. Ein Klick auf eine Zeile hebt alles zur
  selben Gruppenadresse hervor, ein zweiter hebt es wieder auf.</small></p>
  <p><small>Die Spalte Wert ist ein Vorschlag nach L&auml;nge, keine Messung:
  Den Datenpunkttyp einer Gruppenadresse kennt nur das ETS-Projekt, die
  Filtertabelle im Ger&auml;t enth&auml;lt allein Adressen. Bei 1 Bit und 2 Byte
  trifft die Deutung fast immer, bei 1 Byte ist sie mehrdeutig &ndash; deshalb
  stehen die Rohbytes daneben.</small></p>
</dialog>

<dialog id="infoDlg">
  <h2>Über dieses Projekt</h2>
  <p><small>Selfbus KNX/IP Interface &ndash; eine KNXnet/IP-Schnittstelle auf
  ESP32 für den freien KNX-Baukasten von Selfbus.</small></p>
  <div class="row"><span>Quelltext</span>
    <a href="https://github.com/jelli123/IP-Interface-ESP32" target="_blank"
       rel="noopener">jelli123/IP-Interface-ESP32</a></div>
  <div class="row"><span>Lizenz</span>
    <a href="https://github.com/jelli123/IP-Interface-ESP32?tab=GPL-3.0-1-ov-file"
       target="_blank" rel="noopener">GPL-3.0</a></div>
  <div class="row"><span>Selfbus</span>
    <a href="https://selfbus.org" target="_blank" rel="noopener">selfbus.org</a></div>
  <p><small>Diese Firmware ist freie Software: weitergeben und ändern unter
  den Bedingungen der GNU General Public License, Version 3. Ohne jede
  Gewährleistung &ndash; auch ohne die implizite Zusicherung der
  Marktreife oder Eignung für einen bestimmten Zweck.</small></p>

  <h2>Verwendete Komponenten</h2>
  <div class="cred">
    <div class="row"><a href="https://github.com/thelsing/knx" target="_blank"
      rel="noopener">KNX-Stack (thelsing/knx)</a><span>GPL-3.0</span></div>
    <div class="row"><a href="https://github.com/ESP32Async/ESPAsyncWebServer"
      target="_blank" rel="noopener">ESPAsyncWebServer</a><span>LGPL-3.0</span></div>
    <div class="row"><a href="https://github.com/ESP32Async/AsyncTCP"
      target="_blank" rel="noopener">AsyncTCP</a><span>LGPL-3.0</span></div>
    <div class="row"><a href="https://github.com/jnthas/Improv-WiFi-Library"
      target="_blank" rel="noopener">Improv-WiFi-Library</a><span>MIT</span></div>
    <div class="row"><a href="https://github.com/espressif/arduino-esp32"
      target="_blank" rel="noopener">Arduino-ESP32</a><span>LGPL-2.1-or-later</span></div>
    <div class="row"><a href="https://github.com/espressif/esp-idf" target="_blank"
      rel="noopener">ESP-IDF</a><span>Apache-2.0</span></div>
    <div class="row"><a href="https://github.com/FreeRTOS/FreeRTOS-Kernel"
      target="_blank" rel="noopener">FreeRTOS-Kernel</a><span>MIT</span></div>
    <div class="row"><a href="https://savannah.nongnu.org/projects/lwip/"
      target="_blank" rel="noopener">lwIP</a><span>BSD-3-Clause</span></div>
    <div class="row"><a href="https://github.com/Mbed-TLS/mbedtls" target="_blank"
      rel="noopener">Mbed TLS</a><span>Apache-2.0</span></div>
  </div>
  <p><small>Alle genannten Lizenzen sind mit der GPL-3.0 vereinbar. Der
  KNX-Stack steht selbst unter GPL-3.0 und bestimmt damit die Lizenz des
  Ganzen. Den vollständigen Lizenztext jeder Komponente enthält ihr
  Quelltextarchiv.</small></p>

  <div class="actions">
    <button class="sec" onclick="infoDlg.close()">Schließen</button>
  </div>
</dialog>

<dialog id="wifiDlg">
  <h2>WLAN einrichten</h2>
  <select id="ssidSel"><option>Netzwerke suchen...</option></select>
  <input type="password" id="pw" placeholder="Passwort" autocomplete="off">
  <div class="actions">
    <button onclick="connect()">Verbinden</button>
    <button class="sec" onclick="scan()">Neu suchen</button>
    <button class="sec" onclick="wifiDlg.close()">Abbrechen</button>
  </div>
  <p><small>Nach dem Verbinden startet das Gerät neu.</small></p>
</dialog>

<dialog id="filterDlg">
  <h2>Filtertabelle</h2>
  <p><small id="filterInfo"></small></p>
  <div id="filterList" style="max-height:50vh;overflow:auto;font-size:13px;
       font-variant-numeric:tabular-nums;line-height:1.7"></div>
  <div class="actions">
    <button class="sec" onclick="filterDlg.close()">Schließen</button>
  </div>
</dialog>

<dialog id="lpcDlg">
  <h2>SB-Interface programmieren</h2>
  <div class="row"><span>Chip</span><span id="lpcChip" data-dyn>-</span></div>
  <div class="row"><span>Seriennummer</span><span id="lpcUid" data-dyn>-</span></div>
  <div class="row"><span>Bootloader</span><span id="lpcBoot" data-dyn>-</span></div>
  <div class="row"><span>Flash-Inhalt</span><span id="lpcImg" data-dyn>-</span></div>
  <div class="row"><span>TP-UART-Emulation</span><span id="lpcTp" data-dyn>-</span></div>
  <div class="row"><span>Gew&auml;hlte Datei</span><span id="lpcFileSt" data-dyn>-</span></div>
  <div class="row" id="lpcOffRow" style="display:none">
    <span>Angeboten</span><span id="lpcOff" data-dyn>-</span></div>
  <div class="row"><span>Zustand</span><span id="lpcState" data-dyn>-</span></div>
  <div class="bar" id="lpcBar" style="display:none"><i id="lpcFill"></i></div>
  <div class="actions">
    <button class="sec" onclick="lpcProbe()">Erkennen</button>
    <button class="sec" onclick="lpcFile.click()">Datei w&auml;hlen</button>
    <input type="file" id="lpcFile" accept=".hex,.bin" style="display:none"
           onchange="lpcUpload()">
    <button class="sec" id="lpcFetchBtn" onclick="lpcFetch()"
            style="display:none">Online holen</button>
    <button id="lpcWriteBtn" onclick="lpcWrite()" disabled>Programmieren</button>
    <button class="sec" onclick="lpcRun()">SB-Interface neu starten</button>
    <button class="sec" onclick="closeLpc()">Schlie&szlig;en</button>
  </div>
  <p><small>Der ESP32 legt den LPC über zwei Steuerleitungen in seinen
  ROM-Bootlader und spricht ihn über dieselbe UART an, die sonst der
  KNX-Stack benutzt. Während eines Auftrags ruht der Busverkehr für einige
  Sekunden.</small></p>
  <p><small>Angenommen werden Intel-Hex und rohe Binärdateien, jeweils ab
  Adresse 0. Die Prüfsumme der Vektortabelle wird nachgerechnet und
  nötigenfalls gesetzt &ndash; ohne sie startet der Bootlader das Programm
  nicht. Geschrieben wird erst nach einem zweiten Klick, und jeder Block
  wird gegen die Kopie im RAM des LPC verglichen.</small></p>
  <p><small>„Online holen“ lädt die Datei aus dem Manifest, das im
  Hardware-Profil steht, und prüft ihre SHA-256-Summe. Das ist keine
  Aktualitätsprüfung: Der TP-UART-Emulator kennt keinen Versionsbefehl, das
  Gerät kann also nicht wissen, was im LPC bereits steht.</small></p>
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
  <label>Zeitzone</label>
  <select id="tsTzSel" onchange="tzPick()">
    <option value="CET-1CEST,M3.5.0,M10.5.0/3">Mitteleuropa mit Sommerzeit &ndash; Berlin, Wien, Zürich, Paris, Rom, Madrid</option>
    <option value="GMT0BST,M3.5.0/1,M10.5.0">Westeuropa mit Sommerzeit &ndash; London, Dublin, Lissabon</option>
    <option value="EET-2EEST,M3.5.0/3,M10.5.0/4">Osteuropa mit Sommerzeit &ndash; Athen, Helsinki, Bukarest, Riga</option>
    <option value="&lt;+03&gt;-3">Istanbul, Moskau &ndash; UTC+3, keine Sommerzeit</option>
    <option value="CET-1">Mitteleuropa ohne Sommerzeit &ndash; feste UTC+1</option>
    <option value="UTC0">UTC &ndash; ohne Versatz, ohne Sommerzeit</option>
    <option value="EST5EDT,M3.2.0,M11.1.0">USA Ostküste &ndash; New York</option>
    <option value="CST6CDT,M3.2.0,M11.1.0">USA Mitte &ndash; Chicago</option>
    <option value="MST7MDT,M3.2.0,M11.1.0">USA Gebirgszone &ndash; Denver</option>
    <option value="PST8PDT,M3.2.0,M11.1.0">USA Westküste &ndash; Los Angeles</option>
    <option value="">eigene Angabe in POSIX-Schreibweise</option>
  </select>
  <input id="tsTz" placeholder="CET-1CEST,M3.5.0,M10.5.0/3" style="display:none">
  <p><small>Die Auswahl schreibt eine POSIX-Zeitzonenregel aus vier Teilen:
  Kürzel und Versatz der Normalzeit, Kürzel der Sommerzeit, Beginn und Ende
  der Sommerzeit. Das Vorzeichen des Versatzes ist umgekehrt &ndash; er nennt,
  was zur Ortszeit addiert UTC ergibt. In "CET-1CEST,M3.5.0,M10.5.0/3" steht
  "CET-1" deshalb für UTC+1, "M3.5.0" für den letzten Sonntag im März
  (Monat 3, Woche 5 = letzte, Tag 0 = Sonntag) und "M10.5.0/3" für den letzten
  Sonntag im Oktober um 3 Uhr.</small></p>
  <label>Zeit manuell setzen</label>
  <input id="tsManual" type="datetime-local" step="1">
  <div class="actions">
    <button onclick="saveTime()">Speichern</button>
    <button class="sec" onclick="setManual()">Zeit übernehmen</button>
    <button class="sec" onclick="timeDlg.close()">Schließen</button>
  </div>
  <p><small>Ohne Internetzugang NTP abschalten und die Zeit per Browser oder
  manuell setzen. Mit RV-3028 bleibt sie über einen Stromausfall erhalten.</small></p>
</dialog>

<dialog id="hwDlg">
  <h2>Hardware-Profil</h2>
  <p><small id="hwChip" data-dyn></small></p>

  <div class="grp">
    <label class="hd">KNX-UART</label>
    <div class="fields">
      <div><label>UART-Nummer</label><input id="hwUart" type="number" min="0"></div>
      <div><label>RX</label><input id="hwRx" type="number" min="-1"></div>
      <div><label>TX</label><input id="hwTx" type="number" min="-1"></div>
    </div>
  </div>

  <div class="grp">
    <label class="hd">SB-Interface programmieren</label>
    <div class="fields">
      <div><label>Reset</label><input id="hwLpcRst" type="number" min="-1"></div>
      <div><label>ISP</label><input id="hwLpcIsp" type="number" min="-1"></div>
    </div>
    <label class="chk"><input type="checkbox" id="hwLpcInv">
      Steuerleitungen laufen über Inverter</label>
    <p><small>&minus;1 hei&szlig;t nicht verdrahtet. Zwei Leitungen zum LPC des
    SB-Interface: /RESET und PIO0_1. Ohne Inverter werden sie nur nach Masse
    gezogen und sonst losgelassen &ndash; die Pull-ups des LPC halten ihn dann
    im Anwendungsprogramm. Die UART teilen sie sich mit dem
    KNX-Stack.</small></p>
  </div>

  <div class="grp">
    <label>Taster</label>
    <table class="rows" id="hwBtnTbl"></table>
    <div class="rowbtns">
      <button class="sec ico" onclick="rowAdd('btn')" title="Zeile hinzufügen">+</button>
      <button class="sec ico" onclick="rowDel('btn')" title="Markierte Zeile entfernen">&minus;</button>
    </div>
    <p><small>Kurz ist ein Druck unter einer Sekunde, lang ab zwei Sekunden,
    sehr lang ab sechs. Derselbe GPIO darf mehrfach vorkommen, solange sich
    die Auslösung unterscheidet.</small></p>
  </div>

  <div class="grp">
    <label>LEDs</label>
    <table class="rows" id="hwLedTbl"></table>
    <div class="rowbtns">
      <button class="sec ico" onclick="rowAdd('led')" title="Zeile hinzufügen">+</button>
      <button class="sec ico" onclick="rowDel('led')" title="Markierte Zeile entfernen">&minus;</button>
    </div>
    <p><small>Eine adressierbare LED ist eine Position in einer Kette. Zeilen
    mit demselben GPIO gehören zur selben Kette und brauchen denselben Chiptyp,
    aber verschiedene Positionen. Bleibt die LED des S3-DevKitC dunkel: v1.0
    treibt sie über GPIO 48, v1.1 über GPIO 38.</small></p>
  </div>

  <div class="grp">
    <label>Zuordnung Taster</label>
    <table class="rows" id="hwBaTbl"></table>
    <div class="rowbtns">
      <button class="sec ico" onclick="rowAdd('ba')" title="Zeile hinzufügen">+</button>
      <button class="sec ico" onclick="rowDel('ba')" title="Markierte Zeile entfernen">&minus;</button>
    </div>
    <p><small>Werkeinstellungen löscht alles, auch die WLAN-Zugangsdaten.
    WLAN ein/aus lässt sich nur abschalten, wenn ein W5500 erkannt wurde,
    und wirkt nach einem Neustart.</small></p>
  </div>

  <div class="grp">
    <label>Zuordnung LEDs</label>
    <table class="rows" id="hwLaTbl"></table>
    <div class="rowbtns">
      <button class="sec ico" onclick="rowAdd('la')" title="Zeile hinzufügen">+</button>
      <button class="sec ico" onclick="rowDel('la')" title="Markierte Zeile entfernen">&minus;</button>
      <button class="sec ico" onclick="rowMove('la',-1)" title="Nach oben">&uarr;</button>
      <button class="sec ico" onclick="rowMove('la',1)" title="Nach unten">&darr;</button>
    </div>
    <p><small>Die Reihenfolge entscheidet: Für jede LED gilt die
    oberste Zeile, deren Zustand gerade zutrifft. Dieselbe LED darf mehrfach
    vorkommen &ndash; so zeigt eine einzelne LED nacheinander
    Programmiermodus, Netzzustand und Herzschlag. Die Farbe wirkt nur bei
    adressierbaren LEDs; eine einfache LED unterscheidet die Zustände
    allein am Muster.</small></p>
  </div>

  <div class="grp">
    <label class="hd">Echtzeituhr</label>
    <label class="chk"><input type="checkbox" id="hwI2cEn"> RTC über I2C aktivieren</label>
    <div class="fields">
      <div><label>SDA</label><input id="hwSda" type="number" min="-1"></div>
      <div><label>SCL</label><input id="hwScl" type="number" min="-1"></div>
    </div>
  </div>

  <div class="grp">
    <label class="hd">Ethernet</label>
    <label class="chk"><input type="checkbox" id="hwEthEn"> Ethernet W5500 aktivieren</label>
    <div class="fields">
      <div><label>SCK</label><input id="hwSck" type="number" min="-1"></div>
      <div><label>MISO</label><input id="hwMiso" type="number" min="-1"></div>
      <div><label>MOSI</label><input id="hwMosi" type="number" min="-1"></div>
    </div>
    <div class="fields">
      <div><label>CS</label><input id="hwCs" type="number" min="-1"></div>
      <div><label>IRQ</label><input id="hwIrq" type="number" min="-1"></div>
      <div><label>RST</label><input id="hwRst" type="number" min="-1"></div>
    </div>
    <p><small>&minus;1 hei&szlig;t ungenutzt. IRQ und RST braucht der W5500
    nicht; SCK, MISO, MOSI und CS schon.</small></p>
  </div>

  <div class="grp" id="hwMemGrp">
    <label class="hd">Speicheraufteilung</label>
    <div class="fields">
      <div><label>Protokoll (KiB)</label>
        <input id="hwLogKib" type="number" min="0" step="16"
               oninput="memSync('hwLogKib')">
        <input id="hwLogRng" type="range" min="0" step="16"
               oninput="memSync('hwLogRng')"></div>
      <div><label>Busmonitor (KiB)</label>
        <input id="hwMonKib" type="number" min="0" step="16"
               oninput="memSync('hwMonKib')">
        <input id="hwMonRng" type="range" min="0" step="16"
               oninput="memSync('hwMonRng')"></div>
    </div>
    <div class="membar"><i id="memLog"></i><i id="memMon"></i><i id="memRes"></i></div>
    <p><small id="hwMemInfo" data-dyn></small></p>
    <p><small>Beides liegt im PSRAM, ein Rest bleibt f&uuml;r TLS und den
    Zwischenspeicher des Updates frei. 0 schaltet ab: ohne Protokollpuffer
    bleibt ein kleiner Notring im internen RAM, ohne Monitorring entf&auml;llt
    der Busmonitor. Der Busmonitor braucht 48 Byte je Telegramm, eine belebte
    TP1-Linie tr&auml;gt gut 30 pro Sekunde. Wirkt nach einem
    Neustart.</small></p>
  </div>

  <div class="grp">
    <label class="hd">Online-Update</label>
    <label>Firmware dieses Ger&auml;ts &ndash; Manifest-URL (leer = kein Update)</label>
    <input id="hwUpdUrl" maxlength="160"
           placeholder="https://example.org/sbip/manifest.json">
    <label>SB-Interface-Firmware &ndash; Manifest-URL (leer = kein Download)</label>
    <input id="hwLpcUrl" maxlength="160"
           placeholder="https://example.org/tpuart2emu/manifest.json">
    <p><small>Muss mit https:// beginnen. Die Firmware-Dateien werden relativ
    dazu aufgel&ouml;st, liegen also neben dem Manifest. Beide d&uuml;rfen auf
    dieselbe Datei zeigen &ndash; sie nutzen verschiedene Bl&ouml;cke
    darin.</small></p>
  </div>

  <p id="hwErr" style="color:var(--err);font-size:12px"></p>
  <div class="actions">
    <button onclick="hwSave()">Speichern</button>
    <button class="sec" onclick="hwDefaults()"
            title="Formular mit den Werten der Firmware füllen, nichts speichern">Formular zurücksetzen</button>
    <button class="sec" onclick="hwReset()"
            title="Gespeichertes Profil im Gerät löschen und neu starten">Profil im Gerät löschen</button>
    <button class="sec" onclick="hwDlg.close()">Abbrechen</button>
  </div>
  <p><small>Ungültige Pins werden abgewiesen. Startet das Gerät mit einem
  neuen Profil zweimal nicht durch, fällt es automatisch auf die Werte der
  Firmware zurück.</small></p>
</dialog>

<script>
const $ = id => document.getElementById(id);

/*
 * Zweisprachigkeit.
 *
 * Deutsch steht im Markup und ist die Quelle. Übersetzt wird über eine
 * einzige Tabelle, die vom deutschen Text auf den englischen abbildet - so
 * gibt es kein zweites Wörterbuch zu pflegen, und was fehlt, bleibt einfach
 * deutsch. Beim Umschalten zurück wird der ursprüngliche Textknoten aus
 * dem Cache wiederhergestellt.
 */
let LANG = localStorage.getItem('sbip-lang') === 'en' ? 'en' : 'de';

const EN = {
'Telegramme':'Telegrams', 'Zeitserver':'Time server', 'Netzwerk':'Network',
'Hardware-Profil':'Hardware profile', 'WLAN einrichten':'Set up Wi-Fi',
'Laufzeit':'Uptime', 'Betriebsstunden':'Operating hours',
'WLAN-Ersatzverbindung':'WiFi as a standby link',
'WLAN übernimmt bei getrenntem Ethernet':
  'WiFi takes over when Ethernet is disconnected',
'Ohne W5500 ist WLAN die einzige Verbindung.':
  'Without a W5500 WiFi is the only connection.',
'Start':'start', 'Starts':'starts',
'nicht verfügbar':'not available', 'Keine RTC vorhanden.':'No RTC fitted.',
'Betriebsstunden zurücksetzen':'Reset the operating hours',
'Betriebsstunden und Startzähler auf null setzen?':
  'Set the operating hours and the start counter to zero?',
'Physikalische Adresse':'Individual address',
'ETS-Konfiguration':'ETS configuration', 'Tunnel (max.)':'Tunnels (max.)', 'Tunnel-Adressen':'Tunnel addresses',
'Programmiermodus':'Programming mode', 'Verbindung':'Connection',
'Schnittstelle':'Interface', 'Baudrate':'Baud rate', 'Selbsttest':'Self test',
'Buslast':'Bus load', 'TP empfangen':'TP received',
'TP verworfen (fremd)':'TP discarded (foreign)', 'TP ungültig':'TP invalid',
'TP gesendet':'TP sent', 'IP empfangen':'IP received', 'IP gesendet':'IP sent',
'Aktuelle Zeit':'Current time', 'Zeitquelle':'Time source',
'NTP-Server':'NTP server', 'Nächstes Senden':'Next transmission',
'IP-Adresse':'IP address', 'Netzmaske':'Subnet mask',
'IP-Bezug':'Address source', 'fest, aus der ETS':'fixed, from the ETS',
'automatisch (DHCP)':'automatic (DHCP)',
'Takt':'Clock', 'Freier Speicher':'Free memory',
'Über dieses Projekt':'About this project',
'Wiki':'Wiki', 'Quelltext':'Source code', 'KNX-Stack':'KNX stack',
'Lizenz':'Licence', 'Selfbus':'Selfbus',
'Verwendete Komponenten':'Components used',
['Alle genannten Lizenzen sind mit der GPL-3.0 vereinbar. Der KNX-Stack steht '
+ 'selbst unter GPL-3.0 und bestimmt damit die Lizenz des Ganzen. Den '
+ 'vollständigen Lizenztext jeder Komponente enthält ihr Quelltextarchiv.']:
  'Every licence listed is compatible with the GPL-3.0. The KNX stack is '
+ 'itself GPL-3.0 and thereby sets the licence of the whole. The full licence '
+ 'text of each component ships with its own source archive.',
'Partitionstabelle':'Partition table', 'Typ':'Type', 'Adresse':'Address',
'Größe':'Size', 'Firmware':'Firmware',
'frei':'free',

/* --- Busmonitor --- */
'Busmonitor':'Bus monitor', 'Aufzeichnen':'Recording',
'Anzeige filtern':'Filter the list', 'Seite':'Side', 'Richtung':'Direction',
'Adressart':'Address type', 'Gruppe':'Group', 'physikalisch':'individual',
'Quelle enthält':'Source contains', 'Ziel enthält':'Destination contains',
'Dienst enthält':'Service contains', 'alle':'all',
'empfangen':'received', 'gesendet':'sent',
'TP und IP':'TP and IP', 'nur TP':'TP only', 'nur IP':'IP only',
'Start':'Start', 'Stopp':'Stop', 'sofort':'right away',
'bei Gruppenadresse':'on a group address',
'Trigger-Gruppenadresse':'Trigger group address',
'Bei vollem Puffer anhalten statt zu überschreiben':
  'Stop when the buffer is full instead of overwriting',
'Ältere laden':'Load older', 'Neueste laden':'Load newest',
'Als CSV speichern':'Save as CSV',
'Zeit':'Time', 'Quelle':'Source', 'Ziel':'Destination', 'Prio':'Prio',
'Dienst':'Service', 'Daten':'Data', 'Wert':'Value', 'Bits':'Bits',
'unlesbar':'undecodable',
'Aus':'off', 'Ein':'on',
'bei Wiederholung':'on a repetition', 'bei KNX Data Secure':'on KNX Data Secure',
'Telegramme davor':'Telegrams before',
'Telegramme danach (0 = alle)':'Telegrams after (0 = all)',
'ausgelöst bei':'triggered at',
['Anhalten und wieder starten führt die Aufzeichnung fort – geleert wird nur '
+ 'auf Knopfdruck. Nicht anbieten kann das Gerät die ETS-Auslöser über '
+ 'Quittungen und ungültige Telegramme: Das SB-Interface quittiert selbst und '
+ 'verwirft Fehlerhaftes, bevor der Stack es sieht.']:
  'Stopping and starting again continues the recording - it is only emptied on '
+ 'request. What the device cannot offer are the ETS triggers based on '
+ 'acknowledgements and invalid telegrams: the SB-Interface acknowledges by '
+ 'itself and discards faulty frames before the stack sees them.',
'Mo':'Mon', 'Di':'Tue', 'Mi':'Wed', 'Do':'Thu', 'Fr':'Fri', 'Sa':'Sat', 'So':'Sun',
'Telegrammen':'telegrams', 'nach dem Anhalten verworfen':'discarded after the stop',
'bereit':'ready', 'angehalten':'stopped', 'zeichnet auf':'recording',
'wartet auf den Trigger':'waiting for the trigger',
'Puffer voll':'buffer full', 'Puffer voll, angehalten':'buffer full, stopped',
'kein PSRAM':'no PSRAM', 'Stack-Hook fehlt':'stack hook missing',
'Kein PSRAM, der Busmonitor ist nicht verfügbar.':
  'No PSRAM, the bus monitor is unavailable.',
'Der Stack-Hook fehlt - siehe scripts/patch_knx.py.':
  'The stack hook is missing - see scripts/patch_knx.py.',
'Bitte eine Trigger-Gruppenadresse angeben.':'Please enter a trigger group address.',
'Aufzeichnung konnte nicht gestartet werden.':'Could not start the recording.',
'Telegramm senden':'Send a telegram', 'Gruppenadresse':'Group address',
'Wert (hex)':'Value (hex)', 'Senden':'Send',
'Senden fehlgeschlagen.':'Could not send.',
'Kurze Form (Wert in der APCI, wie ETS bei Schalten und Dimmen)':
  'Short form (value inside the APCI, as ETS sends switching and dimming)',
['Das Gerät kennt keine Datenpunkttypen – die Filtertabelle enthält nur '
+ 'Adressen. Der Wert geht als rohe Oktette hinaus, deuten muss ihn der '
+ 'Empfänger. Ein Schaltbefehl ist 01 beziehungsweise 00, ein Prozentwert 80 '
+ 'für 50 %.']:
  'The device knows no data point types - the filter table holds addresses and '
+ 'nothing else. The value goes out as raw octets and is interpreted by '
+ 'whoever receives it. Switching is 01 or 00, a percentage 80 for 50%.',
['Der Ring liegt im PSRAM; ohne PSRAM bleibt der Monitor aus und es hilft nur '
+ 'der Gruppenmonitor der ETS. Angezeigt wird immer nur ein Ausschnitt – der '
+ 'Filter wirkt auf das Geladene, nicht auf die Aufzeichnung.']:
  'The ring lives in PSRAM; without it the monitor stays off and only the ETS '
+ 'group monitor is left. The list always shows a window of the ring - the '
+ 'filter works on what was loaded, not on the recording.',
['Farben: die linke Kante nennt die Seite (grün TP, blau IP), die getönte '
+ 'Zeile das Senden. Ein Klick auf eine Zeile hebt alles zur selben '
+ 'Gruppenadresse hervor, ein zweiter hebt es wieder auf.']:
  'Colours: the left edge names the side (green TP, blue IP), a tinted row '
+ 'means sent. Clicking a row highlights everything for the same group '
+ 'address, clicking again clears it.',
['Die Spalte Wert ist ein Vorschlag nach Länge, keine Messung: Den '
+ 'Datenpunkttyp einer Gruppenadresse kennt nur das ETS-Projekt, die '
+ 'Filtertabelle im Gerät enthält allein Adressen. Bei 1 Bit und 2 Byte '
+ 'trifft die Deutung fast immer, bei 1 Byte ist sie mehrdeutig – deshalb '
+ 'stehen die Rohbytes daneben.']:
  'The value column is a guess from the length, not a measurement: the '
+ 'datapoint type of a group address is known only to the ETS project, and '
+ 'the filter table in the device holds addresses alone. For 1 bit and 2 '
+ 'bytes the reading is almost always right, for 1 byte it is ambiguous - '
+ 'which is why the raw bytes sit next to it.',

'LED-Helligkeit':'LED brightness', 'Heartbeat aktiv':'Heartbeat active',
'Neustart nötig':'Restart required',
'Alle Gruppentelegramme weiterleiten':'Forward every group telegram',
'Online-Update':'Online update',
'KNX-UART':'KNX UART', 'Echtzeituhr':'Real time clock',
'SB-Interface-Download':'SB-Interface download',
'Firmware dieses Geräts – Manifest-URL (leer = kein Update)':
  'Firmware of this device - manifest URL (empty disables the update)',
'SB-Interface-Firmware – Manifest-URL (leer = kein Download)':
  'SB-Interface firmware - manifest URL (empty disables the download)',
['Muss mit https:// beginnen. Die Firmware-Dateien werden relativ dazu '
+ 'aufgelöst, liegen also neben dem Manifest. Beide dürfen auf dieselbe Datei '
+ 'zeigen – sie nutzen verschiedene Blöcke darin.']:
  'Must start with https://. The firmware files are resolved relative to it, '
+ 'so they sit next to the manifest. Both may point at the same file - they '
+ 'read different blocks of it.',
'Angeboten':'Offered', 'Online holen':'Fetch online',
'Manifest lesen':'reading the manifest',
'Datei herunterladen':'downloading the file',
['„Online holen“ lädt die Datei aus dem Manifest, das im Hardware-Profil '
+ 'steht, und prüft ihre SHA-256-Summe. Das ist keine Aktualitätsprüfung: Der '
+ 'TP-UART-Emulator kennt keinen Versionsbefehl, das Gerät kann also nicht '
+ 'wissen, was im LPC bereits steht.']:
  '"Fetch online" downloads the file named by the manifest from the hardware '
+ 'profile and checks its SHA-256. This is not an update check: the TP-UART '
+ 'emulator has no version command, so the device cannot know what is already '
+ 'in the LPC.',
'Aktiv über den Schalter.':'Active through the switch.',
'Aktiv, weil die ETS das Gerät noch nicht programmiert hat.':
  'Active because the ETS has not programmed the device yet.',
'Die Filtertabelle der ETS entscheidet.':'The ETS filter table decides.',
['Solange die ETS das Gerät nicht programmiert hat, leitet es ohnehin alles '
+ 'weiter – ohne Download gibt es keine Filtertabelle, und ein Koppler ohne '
+ 'Tabelle sperrt sonst jedes Gruppentelegramm. Mit dem ersten Download '
+ 'übernimmt die Tabelle von selbst.']:
  'Until the ETS has programmed the device it forwards everything anyway - '
+ 'without a download there is no filter table, and a coupler without one '
+ 'blocks every group telegram. The first download hands over to the table on '
+ 'its own.',
['Der Schalter übersteuert auch das: Er lässt eine geladene Filtertabelle '
+ 'ignorieren und jedes Gruppentelegramm passieren. Nur einschalten, wenn '
+ 'dieses Gerät die einzige Verbindung zwischen Linie und IP ist – sonst '
+ 'drohen Telegrammschleifen.']:
  'The switch overrules that as well: it makes a downloaded filter table be '
+ 'ignored and lets every group telegram through. Only turn it on where this '
+ 'device is the only path between the line and IP - otherwise telegrams can '
+ 'loop.',
'Name':'Name', 'Auslösung':'Trigger', 'Chip':'Chip',
'Position':'Position', 'Zustand':'State', 'Farbe':'Colour', 'Muster':'Pattern',
'1 bis 16 Zeichen aus A-Z a-z 0-9 _ -':'1 to 16 characters from A-Z a-z 0-9 _ -',
'Frei wählbarer Name zur Zuordnung weiter unten':
  'A name of your choice, used by the assignment below',
'Eingang, an dem der Taster gegen Masse schaltet':
  'Input the button pulls to ground',
'Kurz unter 1 s, lang ab 2 s, sehr lang ab 6 s':
  'Short under 1 s, long from 2 s, very long from 6 s',
'Ausgang zur LED bzw. Datenleitung der Kette':
  'Output to the LED, or the data line of the chain',
'Einfache LED an einem GPIO oder Position in einer Kette':
  'A plain LED on one GPIO, or a position in a chain',
'Nur bei adressierbaren LEDs: Bitdauer des Chips':
  'Addressable LEDs only: bit timing of the chip',
'Nur bei adressierbaren LEDs: Platz in der Kette, ab 0':
  'Addressable LEDs only: place in the chain, from 0',
'Platz in der Kette. Eine einzelne LED steht auf 0.':
  'Place in the chain. A single LED belongs at 0.',
'Platz in der Kette, die erste LED ist die 1.':
  'Place in the chain, the first LED is number 1.',
'Name aus der Tastertabelle':'A name from the button table',
'Was der Taster auslöst':'What the button triggers',
'Name aus der LED-Tabelle':'A name from the LED table',
'Wann diese Zeile gilt':'When this row applies',
'Nur bei adressierbaren LEDs':'Addressable LEDs only',
'Wie die LED während des Zustands moduliert wird':
  'How the LED is modulated while the state holds',
['Das übersteuert die ETS: auch eine geladene Filtertabelle wird '
+ 'ignoriert. Fortfahren?']:
  'This overrides the ETS: a downloaded filter table is ignored as well. '
+ 'Continue?',
'Quelle':'Source', 'LED / Taster':'LED / button', 'Status-LED':'Status LED',
'Herzschlag \u2013 alle 2 Sekunden ein weißer Blitz':
  'Heartbeat \u2013 a white flash every 2 seconds',

'KNX zur\u00fccksetzen':'Reset KNX', 'Einstellungen':'Settings',
'ETS-Programmierung l\u00f6schen':'Erase the ETS programming',
'Filtertabelle':'Filter table', 'Partition wechseln':'Switch partition',
'L\u00e4uft aus':'Running from', 'Zweite Partition':'Second partition',
'Aktive Partition':'Active partition',
'geprüft':'verified', 'auf Bewährung':'on probation', 'neu':'new',
'ungültig':'invalid', 'abgebrochen':'aborted',
'ohne OTA-Vermerk':'no OTA record', 'unbekannt':'unknown',
'leer':'empty',
'lese Filtertabelle...':'reading the filter table...',
'Lesen fehlgeschlagen.':'Could not read it.',
['Keine Filtertabelle geladen \u2013 das Gerät ist nicht programmiert und '
+ 'sperrt jedes Gruppentelegramm.']:
  'No filter table loaded \u2013 the device is unprogrammed and blocks every '
+ 'group telegram.',
'%s Gruppenadressen werden weitergeleitet.':'%s group addresses are forwarded.',
'Angezeigt: die ersten %s.':'Showing the first %s.',
'Die Tabelle ist geladen, lässt aber nichts durch.':
  'The table is loaded but lets nothing through.',
['Beim nächsten Start die andere Partition verwenden? Das Gerät startet '
+ 'neu.']:
  'Boot from the other slot next time? The device restarts.',
['Achtung: Der Inhalt ist unbekannt. Diese Partition hat unter der '
+ 'aktuellen Firmware noch nie gelaufen, dort liegt vermutlich ein älterer '
+ 'Stand.']:
  'Careful: the contents are unknown. This slot has never run under the '
+ 'current firmware, so it most likely holds an older build.',
'Umgeschaltet. Das Gerät startet neu.':'Switched. The device restarts.',
'Umschalten nicht möglich.':'Cannot switch.',
'Zeit vom Browser':'Time from browser', 'Jetzt senden':'Send now',
'Bearbeiten':'Edit', 'JSON laden':'Load JSON', 'JSON speichern':'Save JSON',
'Online prüfen':'Check online', 'Installieren':'Install',
'Datei hochladen':'Upload file', 'Verbinden':'Connect',
'Neu suchen':'Scan again', 'Abbrechen':'Cancel', 'Speichern':'Save',
'Zeit übernehmen':'Apply time', 'Schließen':'Close',


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
'Zeitzone':'Time zone',
'Mitteleuropa mit Sommerzeit – Berlin, Wien, Zürich, Paris, Rom, Madrid':
  'Central Europe with DST - Berlin, Vienna, Zurich, Paris, Rome, Madrid',
'Westeuropa mit Sommerzeit – London, Dublin, Lissabon':
  'Western Europe with DST - London, Dublin, Lisbon',
'Osteuropa mit Sommerzeit – Athen, Helsinki, Bukarest, Riga':
  'Eastern Europe with DST - Athens, Helsinki, Bucharest, Riga',
'Istanbul, Moskau – UTC+3, keine Sommerzeit':
  'Istanbul, Moscow - UTC+3, no DST',
'Mitteleuropa ohne Sommerzeit – feste UTC+1':
  'Central Europe without DST - fixed UTC+1',
'UTC – ohne Versatz, ohne Sommerzeit':'UTC - no offset, no DST',
'USA Ostküste – New York':'US Eastern - New York',
'USA Mitte – Chicago':'US Central - Chicago',
'USA Gebirgszone – Denver':'US Mountain - Denver',
'USA Westküste – Los Angeles':'US Pacific - Los Angeles',
'eigene Angabe in POSIX-Schreibweise':'own rule in POSIX notation',
['Die Auswahl schreibt eine POSIX-Zeitzonenregel aus vier Teilen: Kürzel und '
+ 'Versatz der Normalzeit, Kürzel der Sommerzeit, Beginn und Ende der '
+ 'Sommerzeit. Das Vorzeichen des Versatzes ist umgekehrt – er nennt, was zur '
+ 'Ortszeit addiert UTC ergibt. In "CET-1CEST,M3.5.0,M10.5.0/3" steht "CET-1" '
+ 'deshalb für UTC+1, "M3.5.0" für den letzten Sonntag im März (Monat 3, '
+ 'Woche 5 = letzte, Tag 0 = Sonntag) und "M10.5.0/3" für den letzten Sonntag '
+ 'im Oktober um 3 Uhr.']:
  'The list writes a POSIX time zone rule, which has four parts: abbreviation '
+ 'and offset of standard time, abbreviation of daylight saving time, and the '
+ 'start and end of it. The sign of the offset is inverted - it states what '
+ 'has to be added to local time to arrive at UTC. In '
+ '"CET-1CEST,M3.5.0,M10.5.0/3" that makes "CET-1" mean UTC+1, "M3.5.0" the '
+ 'last Sunday in March (month 3, week 5 = last, day 0 = Sunday) and '
+ '"M10.5.0/3" the last Sunday in October at 3 a.m.',
'Zeit manuell setzen':'Set time manually',
'KNX \u2013 UART-Nummer / RX / TX':'KNX \u2013 UART number / RX / TX',
'Programmier-LED / Taster (\u22121 = nicht bestückt)':
  'Programming LED / button (\u22121 = not fitted)',
'Taster':'Buttons', 'LEDs':'LEDs',
'Zuordnung Taster':'Button assignment', 'Zuordnung LEDs':'LED assignment',
'kurzer Druck':'short press', 'langer Druck':'long press',
'sehr langer Druck':'very long press',
'RGB-LED':'RGB LED',
'Programmiermodus':'Programming mode', 'Werkeinstellungen':'Factory reset',
'WLAN Grundeinstellung':'WiFi setup', 'Gerät neu starten':'Restart the device',
'WLAN ein/aus':'WiFi on/off',
'Programmier-LED':'Programming LED', 'Heartbeat-LED':'Heartbeat LED',
'Netzwerkanzeige':'Network indicator',
'Programmiermodus aktiv':'Programming mode active',
'AP-Modus offen':'Access point open',
'keine TP-Verbindung':'No TP connection',
'online':'online', 'offline':'offline', 'Herzschlag':'Heartbeat',
'rot':'red', 'grün':'green', 'blau':'blue', 'gelb':'yellow',
'cyan':'cyan', 'magenta':'magenta', 'weiß':'white',
'Dauerlicht':'steady', 'langsam blinken':'slow blink',
'schnell blinken':'fast blink', 'Doppelblitz':'double flash',
'kurzer Blitz':'short flash',
'Nach oben':'Move up', 'Nach unten':'Move down',
'mehr Zeilen sind nicht möglich':'no more rows are possible',
'keine':'none',
'höchstens 8 Zeilen':'at most 8 rows',
'Bitte zuerst eine Zeile anklicken.':'Select a row first.',['Kurz ist ein Druck unter einer Sekunde, lang ab zwei Sekunden, sehr lang '
+ 'ab sechs. Derselbe GPIO darf mehrfach vorkommen, solange sich die '
+ 'Auslösung unterscheidet.']:
  'Short means under one second, long from two seconds, very long from six. '
+ 'The same GPIO may appear more than once as long as the trigger differs.',
['Eine adressierbare LED ist eine Position in einer Kette. Zeilen mit '
+ 'demselben GPIO gehören zur selben Kette und brauchen denselben Chiptyp, '
+ 'aber verschiedene Positionen. Bleibt die LED des S3-DevKitC dunkel: v1.0 '
+ 'treibt sie über GPIO 48, v1.1 über GPIO 38.']:
  'An addressable LED is one position in a chain. Rows sharing a GPIO belong '
+ 'to the same chain and need the same chip type but different positions. If '
+ 'the LED on an S3-DevKitC stays dark: v1.0 drives it from GPIO 48, v1.1 '
+ 'from GPIO 38.',
['Werkeinstellungen löscht alles, auch die WLAN-Zugangsdaten. WLAN ein/aus '
+ 'lässt sich nur abschalten, wenn ein W5500 erkannt wurde, und wirkt nach '
+ 'einem Neustart.']:
  'Factory reset erases everything, including the WiFi credentials. WiFi '
+ 'on/off can only be switched off once a W5500 has been found, and takes '
+ 'effect after a restart.',
['Die Reihenfolge entscheidet: Für jede LED gilt die oberste Zeile, deren '
+ 'Zustand gerade zutrifft. Dieselbe LED darf mehrfach vorkommen \u2013 so '
+ 'zeigt eine einzelne LED nacheinander Programmiermodus, Netzzustand und '
+ 'Herzschlag. Die Farbe wirkt nur bei adressierbaren LEDs; eine einfache '
+ 'LED unterscheidet die Zustände allein am Muster.']:
  'The order decides: for each LED the topmost row whose state currently '
+ 'holds is the one that shows. The same LED may appear more than once - '
+ 'that is how a single LED shows programming mode, network state and a '
+ 'heartbeat in turn. Colour only applies to addressable LEDs; a plain one '
+ 'tells the states apart by pattern alone.',
'LED low-aktiv':'LED active low',
'RTC über I2C aktivieren':'Enable the RTC on I2C',
'Ethernet W5500 aktivieren':'Enable the W5500 Ethernet',
'Formular zurücksetzen':'Reset the form',
'Profil im Gerät löschen':'Delete the stored profile',
'Formular mit den Werten der Firmware füllen, nichts speichern':
  'Fill the form with the firmware defaults, save nothing',
'Gespeichertes Profil im Gerät löschen und neu starten':
  'Delete the profile stored in the device and restart',
'SHA-256 der Datei (optional, aus':'SHA-256 of the file (optional, from',

'Gerät jetzt neu starten?':'Restart the device now?',
'startet als nächstes':'boots next',
'Werkseinstellungen':'Factory reset',
'Löscht den gesamten NVS-Speicher':'Erases the whole NVS partition',
'Löschen nicht möglich.':'Cannot erase.',
'Wirklich alles löschen? Das lässt sich nicht rückgängig machen.':
  'Really erase everything? This cannot be undone.',
['Werkseinstellungen herstellen?\n\nGelöscht werden Hardware-Profil, '
+ 'WLAN-Zugangsdaten, KNX-Konfiguration, Betriebsstunden und ein gesetztes '
+ 'Passwort. Das Gerät öffnet danach wieder einen offenen Access Point.']:
  'Restore factory settings?\n\nThis erases the hardware profile, the WiFi '
+ 'credentials, the KNX configuration, the operating hours and any password '
+ 'that is set. The device then opens an unprotected access point again.',
['Wird gelöscht. Das Gerät startet neu und ist danach nur noch über seinen '
+ 'Access Point erreichbar.']:
  'Erasing. The device restarts and is then only reachable over its access '
+ 'point.',
'Der Bootlader hat diese Partition verworfen. Nur ein neuer Upload hilft.':
  'The boot loader has rejected this partition. Only a fresh upload helps.',
'Startet nach dem Wechsel neu':'Restarts after the switch',
'Die zweite Partition ist noch nicht bekannt. Seite neu laden.':
  'The second partition is not known yet. Reload the page.',
'Die zweite Partition enthält keine startfähige Firmware.':
  'The second partition holds no bootable firmware.',
'Tunnel':'Tunnel', 'nur Tunnel':'tunnel only',
'Offene ETS-Verbindungen reißen dabei ab':
  'Open ETS connections are dropped',
'UART-Nummer':'UART number',
'Protokoll (KiB)':'Log (KiB)', 'Busmonitor (KiB)':'Bus monitor (KiB)',
'Speicheraufteilung':'Memory split', 'PSRAM-Aufteilung':'PSRAM split',
'KiB reserviert':'KiB reserved', 'KiB frei':'KiB free',
'Monitor fasst':'the monitor holds', 'Telegramme':'telegrams',
'Ohne PSRAM bleiben die Werte ohne Wirkung.':
  'Without PSRAM these values have no effect.',
['−1 heißt ungenutzt. IRQ und RST braucht der W5500 nicht; SCK, MISO, MOSI '
+ 'und CS schon.']:
  '−1 means unused. The W5500 needs no IRQ and no RST; it does need SCK, '
+ 'MISO, MOSI and CS.',
['Beides liegt im PSRAM, ein Rest bleibt für TLS und den Zwischenspeicher '
+ 'des Updates frei. 0 schaltet ab: ohne Protokollpuffer bleibt ein kleiner '
+ 'Notring im internen RAM, ohne Monitorring entfällt der Busmonitor. Der '
+ 'Busmonitor braucht 48 Byte je Telegramm, eine belebte TP1-Linie trägt gut '
+ '30 pro Sekunde. Wirkt nach einem Neustart.']:
  'Both live in PSRAM, with a remainder left free for TLS and the update '
+ 'staging buffer. 0 switches them off: without a log buffer a small fallback '
+ 'ring in internal RAM remains, without a monitor ring the bus monitor is '
+ 'gone. The monitor needs 48 bytes per telegram, and a busy TP1 line carries '
+ 'a good 30 per second. Takes effect after a restart.',

'Änderungen am Profil werden erst nach einem Neustart aktiv.':
  'Profile changes take effect after a restart.',
'Zeile hinzufügen':'Add a row',
'Markierte Zeile entfernen':'Remove the selected row',
'low-aktiv':'active low',
'Nach dem Verbinden startet das Gerät neu.':
  'The device restarts after connecting.',
['Ohne Internetzugang NTP abschalten und die Zeit per Browser oder manuell '
+ 'setzen. Mit RV-3028 bleibt sie über einen Stromausfall erhalten.']:
  'Without internet access, switch NTP off and set the time from the browser '
+ 'or by hand. With an RV-3028 it survives a power failure.',
['Ungültige Pins werden abgewiesen. Startet das Gerät mit einem neuen Profil '
+ 'zweimal nicht durch, fällt es automatisch auf die Werte des Images '
+ 'zurück. Taster beim Einschalten gedrückt halten erzwingt das ebenfalls.']:
  'Invalid pins are rejected. If the device fails to boot twice with a new '
+ 'profile, it falls back to the image defaults on its own. Holding the '
+ 'button while powering up forces the same.',

'Passwort':'Password', 'leer = aus':'empty = off',
'z.B. 0/0/1 \u2013 leer = aus':'e.g. 0/0/1 \u2013 empty = off',
'64 Hex-Zeichen \u2013 leer = ungeprüft':
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
'aus dem Betrieb vor dem Neustart':'carried over from before the restart',
'keiner':'none', 'nicht bestückt':'not fitted', 'deaktiviert':'disabled',
'nicht aktiviert':'not enabled',
'Ausschnitt':'Window', 'von':'of', 'An den Anfang':'To the top', 'An das Ende':'To the end',
'Laufend aktualisieren':'Follow', 'In die Zwischenablage':'Copy to clipboard',
'Als Datei speichern':'Save to a file', 'Ülteres wurde überschrieben':'older lines overwritten',
'Protokoll':'Log', 'Protokollpuffer':'Log buffer',
'Name in der ETS':'Name in ETS', 'nicht gesetzt':'not set',
'Zugangsschutz':'Access control', 'Zugang':'Access', 'offen':'open',
'Benutzername (leer lässt die Oberfläche offen)':
  'User name (empty leaves the dashboard open)',
'Passwort (mindestens 8 Zeichen)':'Password (at least 8 characters)',
'Gespeichert. Jetzt neu starten?':'Saved. Restart now?',
['Digest-Verfahren: Das Passwort wird nicht im Klartext übertragen. Alles '
+ 'andere schon – ohne HTTPS schützt das vor unbefugtem Zugriff im Netz, '
+ 'nicht vor Mitlesen. Wirkt nach einem Neustart. Setzbar nur, wenn ein '
+ 'Taster auf Werkeinstellungen liegt: Das ist der einzige Weg zurück, wenn '
+ 'das Passwort verloren geht.']:
  'Digest scheme: the password never crosses the wire in clear. Everything '
+ 'else does - without HTTPS this keeps others out, it does not stop anyone '
+ 'reading along. Takes effect after a restart. Can only be set while a '
+ 'button is assigned to the factory reset: that is the only way back in if '
+ 'the password is lost.',
['Erst einen Taster auf Werkeinstellungen legen - sonst gibt es kein '
+ 'Zurueck.']:
  'Assign a button to the factory reset first - otherwise there is no way '
+ 'back.',
'Neustart überdauern (RTC-Speicher)':'Survive a restart (RTC memory)',
'Gerätename':'Device name', 'Hostname':'Host name',
'Name ändern':'Change the name',
'1 bis 31 Zeichen aus A-Z a-z 0-9 und Bindestrich':
  '1 to 31 characters from A-Z a-z 0-9 and hyphen',
'Name gespeichert. Jetzt neu starten?':'Name saved. Restart now?',
['Unterscheidet mehrere Router in derselben Anlage. Der Name ist zugleich der '
+ 'mDNS-Hostname und steht im Protokoll. Wird nach einem Neustart wirksam.']:
  'Tells several routers in one installation apart. The name is also the '
+ 'mDNS host name and appears in the log. Takes effect after a restart.',
'Anfang':'Top', 'Ende':'Bottom',
'GA-Filter deaktiviert':'Group address filter off',
'Puffer voll, Älteres wurde überschrieben':
  'buffer full, older lines have been overwritten',
'Automatisch aktualisieren':'Refresh automatically',
['Die letzten Meldungen der Firmware. Das Fenster zeigt das Ende des Puffers '
+ '– oben fällt weg, was hinten nachrückt. Anfang lädt den ganzen Puffer. '
+ 'Kopiert wird die Markierung, sonst alles Angezeigte.']:
  'The most recent firmware messages. The window shows the end of the buffer '
+ '- what arrives at the bottom pushes lines off the top. Top loads the whole '
+ 'buffer. Copying takes the selection, or everything shown.',
'Aktualisieren':'Refresh', 'Leeren':'Clear', 'internes RAM':'internal RAM',
'Kopiert':'Copied',
'Herunterladen':'Download',
'noch keine vergeben':'none assigned yet',
'Image-Standard':'image defaults',
'ungültig &rarr; Standard':'invalid &rarr; defaults',
'Startfehler &rarr; Standard':'boot failure &rarr; defaults',
'Taster &rarr; Standard':'button &rarr; defaults',
'gespeichertes Profil':'stored profile',
'(Neustart nötig)':'(restart required)', 'kein':'none',
'prüfe...':'checking...', 'berechne Prüfsumme...':'computing checksum...',
'lade hoch...':'uploading...',
'erfolgreich, Neustart...':'successful, restarting...',
'fehlgeschlagen':'failed', 'Übertragung abgebrochen':'transfer aborted',
'abgelehnt':'rejected', 'Suche läuft...':'Scanning...',
'Kein Netz gefunden':'No network found',
'Zeitüberschreitung':'Timed out', 'Version %s verfügbar':'version %s available',

'Zeit konnte nicht gesetzt werden.':'Could not set the time.',
'Bitte Datum und Uhrzeit eingeben.':'Please enter a date and a time.',
'Senden nicht möglich - Uhr nicht gesetzt?':
  'Cannot send - is the clock set?',
'Profil gespeichert. Jetzt neu starten?':'Profile saved. Restart now?',
'Gespeichertes Profil verwerfen und die Werte des Images verwenden?':
  'Discard the stored profile and use the image defaults?',
'Zurückgesetzt. Jetzt neu starten?':'Reset done. Restart now?',
'Keine gültige JSON-Datei.':'Not a valid JSON file.',
'Neustart läuft. Die Seite in einigen Sekunden neu laden.':
  'Restarting. Reload the page in a few seconds.',
'Das Gerät läuft über Ethernet. WLAN ist nicht aktiv.':
  'The device runs over Ethernet. Wi-Fi is not active.',
'Zugangsdaten gespeichert. Das Gerät startet neu.':
  'Credentials saved. The device restarts.',
'Firmware jetzt installieren? Das Gerät startet danach neu.':
  'Install the firmware now? The device restarts afterwards.',
'Kein SHA-256 angegeben. Firmware ungeprüft übertragen?':
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
['Automatische Berechnung braucht HTTPS und entfällt hier. Selbst ermitteln '
+ 'mit "Get-FileHash firmware.bin" bzw. "sha256sum firmware.bin", oder leer '
+ 'lassen.']:
  'Automatic computation needs HTTPS and is unavailable here. Obtain it with '
+ '"Get-FileHash firmware.bin" or "sha256sum firmware.bin", or leave empty.',

/* --- SB-Interface programmieren --- */
'Last Kern 0':'Core 0 load', 'Last Hauptschleife':'Main loop load',
'Längster Durchlauf':'Longest pass', 'max.':'peak',
'Spitzenwerte zurücksetzen':'Clear the peaks',
'Spitzenwert zurücksetzen':'Clear the peak',
'SB-Interface programmieren':'Program the SB-Interface',
'SB-Interface ISP':'SB-Interface ISP', 'Seriennummer':'Serial number',
'Bootloader':'Boot loader', 'Flash-Inhalt':'Flash contents',
'TP-UART-Emulation':'TP-UART emulation', 'Gewählte Datei':'Selected file',
'Erkennen':'Identify', 'Datei wählen':'Choose a file',
'Programmieren':'Program',
'SB-Interface neu starten':'Restart the SB-Interface',
'noch nicht abgefragt':'not read yet', 'unbekannter Typ':'unknown type',
'leer, nie programmiert':'empty, never programmed',
'startfähiges Programm':'a program that will start',
'Inhalt ohne gültige Prüfsumme':'contents without a valid checksum',
'antwortet':'answering', 'keine Antwort':'no answer',
'bereit':'ready', 'Datei geladen':'file loaded', 'invertiert':'inverted',
'Datei wird übertragen...':'transferring the file...',
'KNX-Stack anhalten':'Pausing the KNX stack',
'Bootlader suchen':'Looking for the boot loader',
'Flash löschen':'Erasing the flash', 'schreiben':'writing',
'fertig':'finished',
'Steuerleitungen laufen über Inverter':'Control lines run through inverters',
'SB-Interface programmieren – Reset / ISP (−1 = nicht verdrahtet)':
  'Program the SB-Interface – reset / ISP (−1 = not wired)',
['Den LPC jetzt löschen und neu programmieren? Die KNX-Verbindung ruht dabei '
+ 'für einige Sekunden.']:
  'Erase and reprogram the LPC now? The KNX connection rests for a few '
+ 'seconds while it runs.',
['−1 heißt nicht verdrahtet. Zwei Leitungen zum LPC des SB-Interface: '
+ '/RESET und PIO0_1. Ohne Inverter werden sie nur nach Masse gezogen und '
+ 'sonst losgelassen – die Pull-ups des LPC halten ihn dann im '
+ 'Anwendungsprogramm. Die UART teilen sie sich mit dem KNX-Stack.']:
  '−1 means not wired. Two lines to the SB-Interface\'s LPC: /RESET and '
+ 'PIO0_1. Without inverters they are only pulled to ground and otherwise let '
+ 'go - the pull-ups on the LPC then keep it in the application. They share '
+ 'the UART with the KNX stack.',
['Der ESP32 legt den LPC über zwei Steuerleitungen in seinen ROM-Bootlader '
+ 'und spricht ihn über dieselbe UART an, die sonst der KNX-Stack benutzt. '
+ 'Während eines Auftrags ruht der Busverkehr für einige Sekunden.']:
  'Two control lines put the LPC into its ROM boot loader, and the ESP32 then '
+ 'talks to it over the same UART the KNX stack otherwise uses. Bus traffic '
+ 'rests for a few seconds while a job runs.',
['Angenommen werden Intel-Hex und rohe Binärdateien, jeweils ab Adresse 0. '
+ 'Die Prüfsumme der Vektortabelle wird nachgerechnet und nötigenfalls '
+ 'gesetzt – ohne sie startet der Bootlader das Programm nicht. Geschrieben '
+ 'wird erst nach einem zweiten Klick, und jeder Block wird gegen die Kopie '
+ 'im RAM des LPC verglichen.']:
  'Intel Hex and raw binaries are both accepted, each starting at address 0. '
+ 'The vector table checksum is recomputed and set where needed - without it '
+ 'the boot loader will not start the program. Nothing is written until a '
+ 'second click, and every block is compared against the copy in the LPC\'s '
+ 'RAM.'
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
    : 'Automatische Berechnung braucht HTTPS und entfällt hier. Selbst '
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

// Auslastung in Promille: Zahl, Balken und der Hoechststand als Strich. Das
// Alter gehoert dazu - ein Hoechststand sagt nur etwas ueber den Zeitraum aus,
// fuer den er gilt, und die Bereiche werden getrennt zurueckgesetzt.
function loadBar(id, permille, peak, age){
  $(id).textContent = (permille/10).toFixed(1) + ' % \u00b7 '
                    + t('max.') + ' ' + (peak/10).toFixed(1) + ' %'
                    + (age === undefined ? '' : ' (' + span(age) + ')');
  $(id + 'Bar').style.width = (permille/10) + '%';
  $(id + 'Peak').style.left = (peak/10) + '%';
}

/** Sekunden als grobe Dauer: 45 s, 12 min, 3 h. */
function span(s){
  if(s < 90)   return s + ' s';
  if(s < 5400) return Math.round(s/60) + ' min';
  return Math.round(s/3600) + ' h';
}

async function resetPeaks(scope){
  await fetch('/api/peaks/reset?scope=' + (scope || 'all'), {method:'POST'});
  refresh();
}

async function resetHours(){
  if(!confirm(t('Betriebsstunden und Startz\u00e4hler auf null setzen?'))) return;
  await fetch('/api/hours/reset', {method:'POST'});
  refresh();
}

// Dim a group while its leading checkbox is off, so the fields read as
// belonging to the switch above them rather than standing on their own.
// Only a checkbox that IS the group's first element counts - the row editors
// contain checkboxes of their own, and those govern a single LED, not a box.
function syncGroups(){
  document.querySelectorAll('.grp').forEach(g => {
    // Der Schalter darf hinter einer Ueberschrift stehen, aber nicht weiter
    // unten: die Zeileneditoren enthalten eigene Checkboxen je Zeile.
    const head = [...g.children].slice(0, 2).find(e => e.matches('label.chk'));
    const cb = head ? head.querySelector('input') : null;
    if(cb) g.classList.toggle('off', !cb.checked);
  });
}
document.addEventListener('change', e => {
  if(e.target.type === 'checkbox') syncGroups();
});

let last = null;

/*
 * Nur eine Abfrage gleichzeitig.
 *
 * Neben der Zeitschleife lösen auch Bedienschritte ein refresh() aus. Ohne
 * diese Sperre laufen mehrere Anfragen parallel, und der ESP32 hat nur eine
 * Handvoll TCP-Verbindungen.
 */
let busy = false;

async function refresh(){
  if(busy) return;
  busy = true;

  let s;
  try { s = await (await fetch('/api/status')).json(); }
  catch(e){ $('netBadge').textContent = 'offline'; return; }
  finally { busy = false; }
  last = s;

  $('uptime').textContent = s.uptime;

  /*
   * Laufzeit ist die Zeit seit dem letzten Start, Betriebsstunden zaehlen
   * ueber Neustarts hinweg weiter - im RAM der RTC, weil das als einziger
   * Speicher hier verschleissfrei ist.
   */
  const hrs = $('hours');
  if(s.hours_available){
    hrs.textContent = s.hours + ' h \u00b7 ' + s.starts + ' '
                    + t(s.starts === 1 ? 'Start' : 'Starts');
    hrs.title = '';
  } else {
    hrs.textContent = t('nicht verf\u00fcgbar');
    hrs.title = t('Keine RTC vorhanden.');
  }
  $('pa').textContent     = s.knx_pa;
  $('knxName').textContent = s.knx_name || t('nicht gesetzt');
  $('knxBadge').textContent = s.knx_name || s.device_name;
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
  $('rtNote').textContent = s.knx_route_all_active
      ? (s.knx_route_all ? t('Aktiv über den Schalter.')
                         : t('Aktiv, weil die ETS das Gerät noch nicht programmiert hat.'))
      : t('Die Filtertabelle der ETS entscheidet.');

  $('beatRow').style.display = s.led_beat_available ? '' : 'none';
  $('beat').checked          = s.led_heartbeat;
  $('brightRow').style.display = s.led_present ? '' : 'none';
  if(document.activeElement !== $('bright')){
    $('bright').value = s.led_brightness;
    $('brightVal').textContent = s.led_brightness + ' %';
  }

  $('tpConn').innerHTML = dot(s.tp.connected);
  $('tpType').textContent = s.tp.type;
  $('tpBaud').textContent = s.tp.baud + ' Bd, 8E1';
  $('tpTest').textContent = s.tp.self_test;
  loadBar('tpLoad', s.tp.bus_load, s.tp.bus_load_peak, s.tp.bus_load_peak_age);
  $('ispBtn').style.display = s.tp.isp ? '' : 'none';

  $('tpRx').textContent = s.tp.rx_frames;
  $('tpIgn').textContent= s.tp.rx_ignored;
  $('tpInv').textContent= s.tp.rx_invalid;
  $('tpTx').textContent = s.tp.tx_processed + ' / ' + s.tp.tx_frames;
  $('ipRx').textContent = s.ip_stats.rx_frames;
  $('ipTx').textContent = s.ip_stats.tx_frames;

  const MST = {off:'bereit', armed:'wartet auf den Trigger', running:'zeichnet auf',
               full:'Puffer voll', no_psram:'kein PSRAM', no_hook:'Stack-Hook fehlt'};
  $('monSt').textContent = t(MST[s.monitor] || s.monitor);

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

  /*
   * Ohne W5500 waere das Abschalten der Ersatzverbindung ein Weg, sich selbst
   * auszusperren - dann bleibt der Haken gesetzt und gesperrt.
   */
  $('wifiFb').checked  = s.wifi_fallback;
  $('wifiFb').disabled = s.wifi_fallback_locked;
  $('fbRow').title = t(s.wifi_fallback_locked
      ? 'Ohne W5500 ist WLAN die einzige Verbindung.'
      : 'WLAN übernimmt bei getrenntem Ethernet');
  $('devName').textContent  = s.device_name;
  $('hostName').textContent = s.device_name + '.local';

  $('chip').textContent = s.hardware.chip_model + ' rev ' + s.hardware.chip_rev;
  $('cpu').textContent  = s.hardware.cpu_freq + ' MHz';

  const load = s.hardware.cpu_load || [];
  const cpk  = s.hardware.cpu_peak || [];
  const age  = s.hardware.peak_age;
  if(load.length) loadBar('cpu0', load[0], cpk[0], age);

  /*
   * Kern 1 bekommt keinen Balken. Die Arduino-Hauptschleife ist dorthin
   * gebunden und pollt, ohne je zu blockieren - der Idle-Task kommt nie dran,
   * und der Kern steht dauerhaft auf 100 %. Was zaehlt, ist stattdessen, wie
   * viel von der Zeit der Schleife Arbeit war statt Warten.
   */
  loadBar('loopLoad', s.hardware.loop_load, s.hardware.loop_peak, age);
  $('loopMax').textContent = (s.hardware.loop_max_us/1000).toFixed(1) + ' ms';
  $('heap').textContent = Math.round(s.hardware.heap_free/1024) + ' / '
                        + Math.round(s.hardware.heap_total/1024) + ' KiB';

  const mib = b => (b/1048576).toFixed(b < 1048576 ? 2 : 1) + ' MiB';
  const kib = b => Math.round(b/1024) + ' KiB';
  // sketch_size wird einmalig kurz nach dem Start gemessen; bis dahin 0.
  $('flash').textContent = mib(s.hardware.flash_size)
      + ' \u00b7 ' + t('Partition') + ' ' + kib(s.hardware.part_size)
      + (s.hardware.sketch_size
          ? ' \u00b7 ' + t('Firmware') + ' ' + kib(s.hardware.sketch_size)
            + ' \u00b7 ' + kib(s.hardware.part_size - s.hardware.sketch_size)
            + ' ' + t('frei') : '');
  $('psram').textContent = s.hardware.psram_total
      ? (kib(s.hardware.psram_free) + ' / ' + mib(s.hardware.psram_total) + ' ' + t('frei'))
      : t('nicht aktiviert');
  $('logbuf').textContent = s.hardware.log_size
      ? (kib(s.hardware.log_used) + ' / ' + kib(s.hardware.log_size) + ' \u00b7 '
         + (s.hardware.log_psram ? 'PSRAM' : t('internes RAM')))
      : t('nicht aktiviert');
  $('authSt').innerHTML = s.auth_user
      ? dot(true) + ' ' + s.auth_user
      : '<span class="dot off"></span>' + t('offen');
  if(logDlg.open) $('logKeep').checked = s.hardware.log_rtc;
  // Both application slots with what is stored in each, so "switch" is an
  // informed decision rather than a leap.
  const PST = {valid:'geprüft', pending_verify:'auf Bewährung',
               new:'neu', invalid:'ungültig', aborted:'abgebrochen',
               undefined:'ohne OTA-Vermerk'};
  const slot = p => p.label + ' \u00b7 '
      + (p.firmware || t(p.valid ? 'unbekannt' : 'leer'))
      + ' \u00b7 ' + t(PST[p.state] || p.state)
      + (p.boot && !p.running ? ' \u00b7 ' + t('startet als n\u00e4chstes') : '');

  const parts = s.build.partitions || [];
  $('partRun').textContent = parts[0] ? slot(parts[0]) : '-';
  $('partAlt').textContent = parts[1] ? slot(parts[1]) : '-';
  // Nicht sperren, sondern beim Klick den Grund nennen: ein toter Knopf sieht
  // aus wie ein kaputtes Geraet.
  $('swBtn').title = t('Startet nach dem Wechsel neu');
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
  if($('rtAll').checked &&
     !confirm(t('Das übersteuert die ETS: auch eine geladene Filtertabelle wird ignoriert. Fortfahren?'))){
    $('rtAll').checked = false;
    return;
  }
  const body = new URLSearchParams({unfiltered: $('rtAll').checked ? '1' : '0'});
  await fetch('/api/knx/routing', {method:'POST', body});
  setTimeout(refresh, 300);
}

async function setBeat(){
  const body = new URLSearchParams({enabled: $('beat').checked ? '1' : '0'});
  await fetch('/api/led/heartbeat', {method:'POST', body});
  setTimeout(refresh, 300);
}

async function setFallback(){
  const body = new URLSearchParams({enabled: $('wifiFb').checked ? '1' : '0'});
  const r = await fetch('/api/wifi/fallback', {method:'POST', body});
  if(!r.ok) alert(t('Ohne W5500 ist WLAN die einzige Verbindung.'));
  setTimeout(refresh, 300);
}

async function setBright(){
  const body = new URLSearchParams({percent: $('bright').value});
  await fetch('/api/led/brightness', {method:'POST', body});
}

/* --- Protokollfenster ---------------------------------------------------- *
 * Der Ring ist ein halbes Megabyte gross. Alles davon in ein <pre> zu legen
 * macht das Blaettern zaeh, deshalb haelt der Browser nur einen Ausschnitt:
 * etwa doppelt so viel wie sichtbar, je ein Viertel als Reserve vor und nach
 * dem Sichtbaren. Wer an den Rand blaettert, bekommt den naechsten Abschnitt
 * nachgeladen, waehrend am anderen Ende einer wegfaellt. Nur der Download
 * holt den ganzen Puffer.
 * ------------------------------------------------------------------------- */

const LOG_WIN  = 49152; //!< Fensterbreite in Byte
const LOG_STEP = 12288; //!< Sprungweite beim Nachladen, ein Viertel davon

// Die drei Piktogramme, die zur Laufzeit wechseln. Der Rest steht im Markup.
const SVG = '<svg viewBox="0 0 16 16">';
const ICO_PLAY = SVG + '<path class="sol" d="M5.4 3 12.4 8l-7 5z"/></svg>';
const ICO_STOP = SVG + '<rect class="sol" x="4.2" y="4.2" width="7.6" height="7.6" rx="1"/></svg>';
const ICO_CLIP = SVG + '<path d="M6.2 3H4.6a1.6 1.6 0 0 0-1.6 1.6v8.8A1.6 1.6 0 0 0 4.6 15h6.8a1.6 1.6'
               + ' 0 0 0 1.6-1.6V4.6A1.6 1.6 0 0 0 11.4 3H9.8"/>'
               + '<rect x="6.1" y="1.4" width="3.8" height="2.5" rx=".8"/></svg>';
const ICO_DONE = SVG + '<path d="m3.2 8.4 3.3 3.4 6.3-7"/></svg>';

let logFrom = 0, logOldest = 0, logWritten = 0;
let logBusy = false, logFollow = false, logTimer = null;

/** Holt den Ausschnitt ab @p from. anchor: 'end', 'start' oder 'keep'. */
async function loadWindow(from, anchor){
  if(logBusy) return;
  logBusy = true;

  const box = $('logText');
  const beforeHeight = box.scrollHeight;
  const beforeTop = box.scrollTop;

  try {
    const url = '/api/log?bytes=' + LOG_WIN
              + (from === null ? '' : '&from=' + from);
    const r = await fetch(url);
    const text = await r.text();

    logFrom    = Number(r.headers.get('X-Log-From') || 0);
    logOldest  = Number(r.headers.get('X-Log-Oldest') || 0);
    logWritten = Number(r.headers.get('X-Log-Written') || 0);

    box.textContent = text;

    if(anchor === 'start')      box.scrollTop = 0;
    else if(anchor === 'end')   box.scrollTop = box.scrollHeight;
    else                        box.scrollTop = beforeTop + (box.scrollHeight - beforeHeight);
  }
  catch(e){ if(anchor !== 'keep') box.textContent = t('Lesen fehlgeschlagen.'); }
  finally { logBusy = false; }

  logInfoUpdate();
}

function logInfoUpdate(){
  const kb = n => Math.round(n/1024) + ' KiB';
  const shown = Math.min(LOG_WIN, logWritten - logFrom);
  $('logInfo').textContent =
      t('Ausschnitt') + ' ' + kb(shown) + ' ' + t('von') + ' '
      + kb(logWritten - logOldest)
      + (logOldest > 0 ? ' \u2013 ' + t('Älteres wurde überschrieben') : '');
}

/* Beim Blaettern an den Rand nachladen. Ein Viertel Fenster als Schwelle,
 * damit das Nachladen fertig ist, bevor der Rand erreicht wird. */
function logScrolled(){
  if(logBusy) return;

  const box = $('logText');
  const nearTop = box.scrollTop < box.clientHeight * 0.5;
  const nearEnd = box.scrollHeight - box.scrollTop - box.clientHeight
                  < box.clientHeight * 0.5;

  if(nearTop && logFrom > logOldest){
    loadWindow(Math.max(logOldest, logFrom - LOG_STEP), 'keep');
  }
  else if(nearEnd && logFrom + LOG_WIN < logWritten){
    loadWindow(logFrom + LOG_STEP, 'keep');
  }
}

/** Ohne Argument: das Ende des Puffers. */
async function loadLog(){
  await loadWindow(null, 'end');
}

async function logJump(toEnd){
  if(toEnd){
    await loadWindow(null, 'end');
    return;
  }
  setFollow(false);
  await loadWindow(0, 'start'); // 0 wird serverseitig auf oldest() gehoben
}

function openAuth(){
  $('authUser').value = last ? last.auth_user : '';
  $('authPass').value = '';

  // Nur warnen, nicht sperren: das Loeschen eines Passworts muss auch dann
  // gehen, wenn der Taster inzwischen fehlt.
  $('authErr').textContent = (last && last.auth_possible) ? ''
      : t('Erst einen Taster auf Werkeinstellungen legen - sonst gibt es kein Zurueck.');

  authDlg.showModal();
}

async function saveAuth(){
  const body = new URLSearchParams({
    user: $('authUser').value.trim(),
    password: $('authPass').value
  });
  const r = await fetch('/api/auth', {method:'POST', body});

  if(!r.ok){
    let msg = t('abgelehnt');
    try { const j = await r.json(); if(j.error) msg = j.error; } catch(e){}
    $('authErr').textContent = msg;
    return;
  }

  authDlg.close();
  if(confirm(t('Gespeichert. Jetzt neu starten?'))) doReboot();
}

async function setKeep(){
  const body = new URLSearchParams({enabled: $('logKeep').checked ? '1' : '0'});
  await fetch('/api/log/keep', {method:'POST', body});
}

function openName(){
  $('nameIn').value = last ? last.device_name : '';
  $('nameErr').textContent = '';
  nameDlg.showModal();
}

async function saveName(){
  const body = new URLSearchParams({name: $('nameIn').value.trim()});
  const r = await fetch('/api/name', {method:'POST', body});

  if(!r.ok){
    let msg = t('abgelehnt');
    try { const j = await r.json(); if(j.error) msg = j.error; } catch(e){}
    $('nameErr').textContent = msg;
    return;
  }

  nameDlg.close();
  if(confirm(t('Name gespeichert. Jetzt neu starten?'))) doReboot();
}

function showLog(){
  logDlg.showModal();
  if(last) $('logKeep').checked = last.hardware.log_rtc;
  $('logCopy').innerHTML = ICO_CLIP;
  $('logText').onscroll = logScrolled;
  setFollow(false);
  loadLog();
}

function closeLog(){
  setFollow(false);
  logDlg.close();
}

function setFollow(on){
  logFollow = on;
  $('logFollow').innerHTML = on ? ICO_STOP : ICO_PLAY;
  $('logFollow').classList.toggle('on', on);

  if(logTimer){ clearTimeout(logTimer); logTimer = null; }
  if(on) logTick();
}

function toggleFollow(){ setFollow(!logFollow); }

async function logTick(){
  if(!logDlg.open || !logFollow){ logTimer = null; return; }
  await loadWindow(null, 'end');
  logTimer = setTimeout(logTick, 3000);
}

async function clearLog(){
  await fetch('/api/log/clear', {method:'POST'});
  loadLog();
}

/*
 * navigator.clipboard gibt es nur im sicheren Kontext, den es hier ueber
 * http nicht gibt - genau wie bei crypto.subtle. Deshalb der Umweg ueber ein
 * kurzzeitiges textarea, das in allen Browsern auch ohne HTTPS funktioniert.
 */
async function copyLog(){
  const sel = String(window.getSelection());
  const text = sel.trim() ? sel : $('logText').textContent;
  let ok = false;

  if(window.isSecureContext && navigator.clipboard){
    try { await navigator.clipboard.writeText(text); ok = true; } catch(e){}
  }
  if(!ok){
    const ta = document.createElement('textarea');
    ta.value = text;
    ta.style.position = 'fixed';
    ta.style.opacity = '0';
    document.body.appendChild(ta);
    ta.select();
    try { ok = document.execCommand('copy'); } catch(e){}
    ta.remove();
  }

  // Das Piktogramm quittiert selbst - eine Beschriftung wuerde es ersetzen.
  const btn = $('logCopy');
  btn.innerHTML = ok ? ICO_DONE : ICO_CLIP;
  btn.title = t(ok ? 'Kopiert' : 'In die Zwischenablage');
  setTimeout(() => { btn.innerHTML = ICO_CLIP;
                     btn.title = t('In die Zwischenablage'); }, 1500);
}

function downloadLog(){
  const stamp = new Date().toISOString().slice(0,19).replace(/[-:]/g,'').replace('T','-');
  const a = document.createElement('a');
  a.href = '/api/log?download=1&bytes=' + ((last && last.hardware.log_size) || LOG_WIN);
  a.download = 'sbip-log-' + stamp + '.txt';
  a.click();
}

/* --- Busmonitor ---------------------------------------------------------- *
 * Der Ring im Geraet fasst Tausende Telegramme; im Browser stehen davon immer
 * nur die zuletzt geholten. Geladen wird ueber absolute Folgenummern, so wie
 * beim Protokollfenster, damit Blaettern und Nachladen dieselbe Rechnung
 * benutzen. Der Filter wirkt nur auf das Geholte - die Aufzeichnung selbst
 * entscheidet allein die Seitenauswahl im Geraet.
 * ------------------------------------------------------------------------- */

const MON_WIN = 400; //!< Telegramme je Abruf
let monRows = [], monState = null, monFollow = false, monTimer = null;

// Seite 2 ist die Uebergabe durch einen Tunnel-Client - weder IP noch TP,
// siehe bus_monitor.h.
const SIDE_TXT = ['IP', 'TP', 'Tunnel'];
const SIDE_CLS = ['ip', 'tp', 'tun'];
let monSel = null;   //!< hervorgehobene Gruppenadresse

function openMon(){
  monDlg.showModal();
  monModeChanged();
  monSetFollow(false);
  monRefresh().then(() => monLoad(1));
}

function monClose(){
  monSetFollow(false);
  monDlg.close();
}

function monModeChanged(){
  $('monTrig').disabled = $('monMode').value !== 'ga';
  const trig = $('monMode').value !== 'now';
  $('monPre').disabled = !trig;
  $('monPost').disabled = !trig;
}

async function monRefresh(){
  try { monState = await (await fetch('/api/monitor')).json(); }
  catch(e){ return null; }

  const s = monState;
  const ST = {off:'angehalten', armed:'wartet auf den Trigger',
              running:'zeichnet auf', full:'Puffer voll, angehalten'};

  const busy = s.state === 'armed' || s.state === 'running';
  $('monRun').textContent = t(busy ? 'Stopp' : 'Start');

  let info;
  if(!s.hook)           info = t('Der Stack-Hook fehlt - siehe scripts/patch_knx.py.');
  else if(!s.available) info = t('Kein PSRAM, der Busmonitor ist nicht verfügbar.');
  else {
    info = t(ST[s.state] || s.state) + ' \u00b7 '
         + s.count + ' ' + t('von') + ' ' + s.capacity + ' ' + t('Telegrammen');
    if(s.triggered && s.trigger_mode !== 'now')
      info += ' \u00b7 ' + t('ausgelöst bei') + ' #' + s.trigger_seq;
    if(s.missed)
      info += ' \u00b7 ' + s.missed + ' ' + t('nach dem Anhalten verworfen');
  }

  $('monInfo').textContent = info;

  const off = !s.available || !s.hook;
  $('monRun').disabled = off;
  return s;
}

/** @param newest 1 = ans Ende springen, 0 = einen Ausschnitt weiter zurueck. */
async function monLoad(newest){
  const s = await monRefresh();
  if(!s || !s.available) return;

  let from = null;
  if(!newest){
    const first = monRows.length ? monRows[0].s : s.written;
    from = Math.max(s.oldest, first - MON_WIN);

    // Nichts mehr nachzuladen - der Blick soll trotzdem an den Anfang.
    if(first <= s.oldest){ monRender('start'); return; }
  }

  const url = '/api/monitor/frames?max=' + MON_WIN + (from === null ? '' : '&from=' + from);
  try { monRows = await (await fetch(url)).json(); }
  catch(e){ return; }

  // Wer zurueckblaettert, will den Anfang des Geholten sehen, nicht das Ende.
  monRender(newest ? 'end' : 'start');
}

function monRender(scroll){
  const fSide = $('fSide').value, fDir = $('fDir').value, fKind = $('fKind').value;  const fSrc = $('fSrc').value.trim(), fDst = $('fDst').value.trim();
  const fSvc = $('fSvc').value.trim().toLowerCase();

  const rows = monRows.filter(r =>
       (fSide === '' || String(r.t) === fSide)
    && (fDir  === '' || String(r.o) === fDir)
    && (fKind === '' || String(r.g === undefined ? '' : r.g) === fKind)
    && (!fSrc || (r.src || '').indexOf(fSrc) >= 0)
    && (!fDst || (r.dst || '').indexOf(fDst) >= 0)
    && (!fSvc || (r.a  || '').toLowerCase().indexOf(fSvc) >= 0));

  const head = '<tr><th>' + t('Zeit') + '</th><th>&Delta;</th><th>' + t('Seite')
    + '</th><th></th><th>' + t('Quelle') + '</th><th>' + t('Ziel') + '</th><th>'
    + t('Prio') + '</th><th>' + t('Dienst') + '</th><th>' + t('Bits')
    + '</th><th>' + t('Daten') + '</th><th>' + t('Wert') + '</th></tr>';

  let previous = null;
  const body = rows.map(r => {
    const gap = previous === null ? '' : (r.ms - previous) + ' ms';
    previous = r.ms;
    const cls = (r.o ? 'tx' : 'rx') + ' ' + SIDE_CLS[r.t]
              + (monSel && r.dst === monSel ? ' mark' : '');
    return '<tr class="' + cls + '" onclick="monPick(\'' + esc(r.dst || '') + '\')">'
      + '<td>' + monTime(r.ms) + '</td>'
      + '<td class="dim">' + gap + '</td>'
      + '<td>' + SIDE_TXT[r.t] + '</td>'
      + '<td>' + (r.o ? '&rarr;' : '&larr;') + '</td>'
      + '<td>' + esc(r.src || '') + '</td>'
      + '<td>' + esc(r.dst || '') + '</td>'
      + '<td class="dim">' + esc(r.p || '') + '</td>'
      + '<td>' + esc(r.a || (r.raw ? t('unlesbar') : '')) + '</td>'
      + '<td class="dim">' + monBits(r) + '</td>'
      + '<td class="w dim">' + esc(r.d || r.raw || '') + '</td>'
      + '<td class="val w">' + esc(monValue(r)) + '</td></tr>';
  }).join('');

  $('monTbl').innerHTML = head + body;
  if(scroll === 'end')   $('monBox').scrollTop = $('monBox').scrollHeight;
  if(scroll === 'start') $('monBox').scrollTop = 0;
}

/*
 * Breite der Nutzdaten.
 *
 * Bis zu sechs Bit reisen im APCI-Byte selbst; wie viele davon der
 * Datenpunkttyp wirklich belegt - eines bei DPT 1, vier bei DPT 3 - steht
 * nirgends im Telegramm. Deshalb dort eine obere Schranke.
 */
function monBits(r){
  if(!r.d || r.n === undefined) return '';
  if(r.n <= 1) return '\u2264 6';
  return String((r.d.length / 2) * 8);
}

/** Alles zur selben Gruppenadresse hervorheben, erneuter Klick hebt es auf. */
function monPick(dst){
  monSel = (monSel === dst || !dst) ? null : dst;
  const top = $('monBox').scrollTop;
  monRender();
  $('monBox').scrollTop = top;
}

/*
 * Deutung der Nutzdaten nach ihrer Laenge.
 *
 * Ein Koppler kennt den Datenpunkttyp einer Gruppenadresse nicht: der steht im
 * ETS-Projekt, und die Filtertabelle, die das Geraet bekommt, enthaelt nur
 * Adressen. Was hier steht, ist deshalb ein Vorschlag anhand der Laenge - bei
 * 1 Bit und 2 Byte fast immer richtig, bei 1 Byte mehrdeutig.
 */
function monValue(r){
  // Ein Lesebefehl traegt keine Nutzdaten - die Null darin ist Fuellung.
  if(!r.d || !/GroupValue(Write|Response)/.test(r.a || '')) return '';

  const b = [];
  for(let i = 0; i + 1 < r.d.length; i += 2) b.push(parseInt(r.d.substr(i, 2), 16));
  if(!b.length) return '';

  const dpt = s => ' (' + s + ')';

  if(r.n <= 1){
    const v = b[0] & 0x3F;
    if(v === 0) return t('Aus') + dpt('1.x');
    if(v === 1) return t('Ein') + dpt('1.x');
    return v + dpt('3.x');
  }

  if(b.length === 1){
    return b[0] + dpt('5.010') + ' \u00b7 '
         + Math.round(b[0] * 100 / 255) + ' %' + dpt('5.001');
  }

  if(b.length === 2){
    const raw = (b[0] << 8) | b[1];
    let m = raw & 0x07FF;
    if(raw & 0x8000) m -= 2048;
    const f = 0.01 * m * Math.pow(2, (raw >> 11) & 0x0F);
    return (Math.round(f * 100) / 100) + dpt('9.x') + ' \u00b7 ' + raw + dpt('7.001');
  }

  if(b.length === 3){
    /*
     * Hier stossen zwei Datenpunkttypen zusammen, die sich nicht
     * unterscheiden lassen: 10.001 traegt Wochentag und Uhrzeit, 11.001 Tag,
     * Monat und Jahr. Passt beides, steht auch beides da.
     */
    const pad = n => String(n).padStart(2, '0');
    const D = ['', 'Mo', 'Di', 'Mi', 'Do', 'Fr', 'Sa', 'So'];
    const day = b[0] >> 5, h = b[0] & 0x1F;
    const out = [];

    if(h < 24 && b[1] < 60 && b[2] < 60){
      out.push((day ? t(D[day]) + ' ' : '')
             + [h, b[1], b[2]].map(pad).join(':') + dpt('10.001'));
    }
    if(b[0] >= 1 && b[0] <= 31 && b[1] >= 1 && b[1] <= 12 && b[2] < 100){
      // 0..89 meint 2000..2089, 90..99 meint 1990..1999 (DPT 11.001).
      const year = (b[2] < 90 ? 2000 : 1900) + b[2];
      out.push(pad(b[0]) + '.' + pad(b[1]) + '.' + year + dpt('11.001'));
    }
    return out.join(' \u00b7 ');
  }

  if(b.length === 4){
    const dv = new DataView(new Uint8Array(b).buffer);
    const u = dv.getUint32(0), f = dv.getFloat32(0);
    return (isFinite(f) ? (Math.round(f * 1000) / 1000) + dpt('14.x') + ' \u00b7 ' : '')
         + u + dpt('12.001');
  }

  if(b.length === 14){
    // DPT 16.000, ASCII mit Nullen aufgefuellt.
    const s = b.map(c => (c >= 0x20 && c < 0x7F) ? String.fromCharCode(c) : '')
               .join('').trim();
    return s ? '"' + s + '"' + dpt('16.000') : '';
  }

  return '';
}

/*
 * Das Geraet stempelt mit seiner Betriebszeit, weil eine Uhrzeit im
 * Aufzeichnungspfad nichts zu suchen hat. Steht die Uhr, rechnet der Browser
 * daraus die Tageszeit; sonst bleibt die Betriebszeit stehen.
 */
function monTime(ms){
  if(monState && monState.epoch_ms){
    const d = new Date(monState.epoch_ms - (monState.now_ms - ms));
    return d.toTimeString().slice(0,8) + '.'
         + String(d.getMilliseconds()).padStart(3,'0');
  }
  const s = Math.floor(ms/1000);
  return '+' + String(Math.floor(s/3600)).padStart(2,'0') + ':'
       + String(Math.floor(s/60)%60).padStart(2,'0') + ':'
       + String(s%60).padStart(2,'0') + '.'
       + String(ms%1000).padStart(3,'0');
}

async function monToggle(){
  const s = monState;
  if(s && (s.state === 'armed' || s.state === 'running')){
    await fetch('/api/monitor/stop', {method:'POST'});
    monSetFollow(false);
    await monRefresh();
    return;
  }

  const mode = $('monMode').value;
  const trigger = mode === 'ga' ? $('monTrig').value.trim() : '';
  if(mode === 'ga' && !trigger){
    alert(t('Bitte eine Trigger-Gruppenadresse angeben.'));
    return;
  }

  const body = new URLSearchParams({
    sides:        $('monSides').value,
    trigger_mode: mode,
    trigger:      trigger,
    pre:          mode === 'now' ? '0' : ($('monPre').value || '0'),
    post:         mode === 'now' ? '0' : ($('monPost').value || '0'),
    stop_full:    $('monStopFull').checked ? '1' : '0'
  });
  const r = await fetch('/api/monitor/start', {method:'POST', body});

  if(!r.ok){
    let msg = t('Aufzeichnung konnte nicht gestartet werden.');
    try { const j = await r.json(); if(j.error) msg = j.error; } catch(e){}
    alert(msg);
    return;
  }

  monSetFollow(true);
}

async function monClear(){
  await fetch('/api/monitor/clear', {method:'POST'});
  monRows = [];
  monSel  = null;
  monRender();
  monRefresh();
}

// The short form only exists for a single value of six bits.
function wSync(){
  const hex = $('wVal').value.replace(/[^0-9a-fA-F]/g,'');
  const box = $('wShort');
  const fits = hex.length === 2 && parseInt(hex,16) <= 0x3F;
  box.disabled = !fits;
  if(!fits) box.checked = false;
}

async function wSend(){
  const msg = $('wMsg');
  const body = new URLSearchParams({
    ga:    $('wGa').value,
    value: $('wVal').value,
    short: $('wShort').checked ? '1' : '0'
  });

  msg.textContent = '';
  const r = await fetch('/api/knx/write', {method:'POST', body:body});

  if(!r.ok){
    let text = t('Senden fehlgeschlagen.');
    try { const j = await r.json(); if(j.error) text = j.error; } catch(e){}
    msg.textContent = text;
    msg.className = 'bad';
    return;
  }

  msg.textContent = t('gesendet');
  msg.className = 'ok';
  setTimeout(() => { msg.textContent = ''; }, 3000);
  monRefresh();
}

function monSetFollow(on){
  monFollow = on;
  $('monFollow').innerHTML = on ? ICO_STOP : ICO_PLAY;
  $('monFollow').classList.toggle('on', on);

  if(monTimer){ clearTimeout(monTimer); monTimer = null; }
  if(on) monTick();
}

function monToggleFollow(){ monSetFollow(!monFollow); }

async function monTick(){
  if(!monDlg.open || !monFollow){ monTimer = null; return; }
  await monLoad(1);
  monTimer = setTimeout(monTick, 1500);
}

/* Ausgangspunkt ist das Geholte, nicht der Ring: was der Browser zeigt, ist
 * auch das, was in der Datei steht - sonst passen Filter und Datei nicht
 * zusammen. */
function monDownload(){
  const sep = ';';
  const head = ['ms','Seite','Richtung','Quelle','Ziel','Prio','Wdh','Hop',
                'Dienst','Daten','Wert'].join(sep);
  const body = monRows.map(r => [r.ms, SIDE_TXT[r.t], r.o ? 'TX' : 'RX',
      r.src || '', r.dst || '', r.p || '', r.r || 0, r.h === undefined ? '' : r.h,
      r.a || '', r.d || r.raw || '', monValue(r)].join(sep)).join('\n');

  const blob = new Blob([head + '\n' + body], {type:'text/csv;charset=utf-8'});
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = 'sbip-monitor-'
    + new Date().toISOString().slice(0,19).replace(/[-:]/g,'').replace('T','-') + '.csv';
  a.click();
  URL.revokeObjectURL(a.href);
}

async function showParts(){
  $('partList').innerHTML = '<tr class="hd"><th>' + t('lese Filtertabelle...') + '</th></tr>';
  partDlg.showModal();

  let p;
  try { p = await (await fetch('/api/partitions')).json(); }
  catch(e){ $('partList').innerHTML = '<tr><td>' + t('Lesen fehlgeschlagen.') + '</td></tr>'; return; }

  const hex = n => '0x' + n.toString(16).toUpperCase().padStart(6, '0');
  $('partList').innerHTML = '<tr class="hd"><th>' + t('Name') + '</th><th>' + t('Typ')
      + '</th><th>' + t('Adresse') + '</th><th>' + t('Größe') + '</th></tr>'
    + p.map(e => '<tr><td>' + esc(e.label) + '</td><td>' + e.type + '</td><td>'
      + hex(e.addr) + '</td><td>' + Math.round(e.size/1024) + ' KiB</td></tr>').join('');
}

async function showFilter(){
  $('filterInfo').textContent = t('lese Filtertabelle...');
  $('filterList').textContent = '';
  filterDlg.showModal();

  let f;
  try { f = await (await fetch('/api/knx/filter')).json(); }
  catch(e){ $('filterInfo').textContent = t('Lesen fehlgeschlagen.'); return; }

  if(!f.loaded){
    $('filterInfo').textContent = t('Keine Filtertabelle geladen \u2013 das Gerät '
      + 'ist nicht programmiert und sperrt jedes Gruppentelegramm.');
    return;
  }

  $('filterInfo').textContent = f.total
    ? t('%s Gruppenadressen werden weitergeleitet.').replace('%s', f.total)
      + (f.total > f.addresses.length
         ? ' ' + t('Angezeigt: die ersten %s.').replace('%s', f.addresses.length)
         : '')
    : t('Die Tabelle ist geladen, lässt aber nichts durch.');

  $('filterList').textContent = f.addresses.join('   ');
}

/* --- SB-Interface programmieren ------------------------------------------ *
 * Jeder Auftrag läuft im Gerät auf einer eigenen Task und dauert Sekunden.
 * Die Oberfläche stößt ihn nur an und fragt danach den Fortschritt ab.
 * ------------------------------------------------------------------------ */

const LSTAGE = {pause:'KNX-Stack anhalten', detect:'Bootlader suchen',
                erase:'Flash löschen', write:'schreiben',
                reset:'SB-Interface neu starten',
                manifest:'Manifest lesen', download:'Datei herunterladen',
                done:'fertig', failed:'fehlgeschlagen'};

let lpcLast = null, lpcTimer = null;

function openLpc(){ lpcDlg.showModal(); lpcRefresh(); }

function closeLpc(){
  if(lpcTimer){ clearTimeout(lpcTimer); lpcTimer = null; }
  lpcDlg.close();
}

async function lpcRefresh(){
  let s;
  try { s = await (await fetch('/api/lpc')).json(); } catch(e){ return; }
  lpcLast = s;
  const c = s.chip;
  const kib = b => Math.round(b/1024) + ' KiB';

  $('lpcChip').textContent = c.answered
      ? (c.part || t('unbekannter Typ')) + ' \u00b7 ' + c.part_id + ' \u00b7 '
        + kib(c.flash) + ' Flash, ' + kib(c.ram) + ' RAM'
      : t('noch nicht abgefragt');
  $('lpcUid').textContent  = c.answered ? c.uid : '-';
  $('lpcBoot').textContent = c.answered ? c.boot : '-';

  // Der Bootlader kann nur sagen, dass überhaupt ein startfähiges Programm
  // dasteht. Ob es die richtige Firmware ist, beantwortet die Zeile darunter.
  $('lpcImg').innerHTML = !c.answered ? '-'
      : c.blank         ? '<span class="dot warn"></span>' + t('leer, nie programmiert')
      : c.image_valid   ? dot(true) + ' ' + t('startfähiges Programm')
                        : '<span class="dot err"></span>' + t('Inhalt ohne gültige Prüfsumme');
  $('lpcTp').innerHTML = s.tp_connected
      ? dot(true) + ' ' + t('antwortet')
      : '<span class="dot err"></span>' + t('keine Antwort');

  $('lpcFileSt').textContent = s.image_size
      ? kib(s.image_size) + ' ' + t('bereit')
      : t('keine');

  $('lpcFetchBtn').style.display = s.can_fetch ? '' : 'none';
  $('lpcFetchBtn').disabled = s.busy;
  $('lpcOffRow').style.display = s.offered ? '' : 'none';
  $('lpcOff').textContent = s.offered || '-';

  let state = t(LSTAGE[s.stage] || s.stage || '-');
  if(!s.busy && s.ran && s.error) state = t('fehlgeschlagen') + ' \u2013 ' + s.error;
  $('lpcState').textContent = state;

  const show = s.busy && s.total > 0;
  $('lpcBar').style.display = show ? 'block' : 'none';
  if(show) $('lpcFill').style.width = (100*s.progress/s.total) + '%';

  $('lpcWriteBtn').disabled = s.busy || !s.image_size;
  return s;
}

function lpcTick(){
  if(lpcTimer) clearTimeout(lpcTimer);
  lpcTimer = setTimeout(async () => {
    lpcTimer = null;
    const s = await lpcRefresh();
    if(s && s.busy) lpcTick();
  }, 700);
}

async function lpcStart(path){
  const r = await fetch(path, {method:'POST'});
  if(!r.ok){
    let msg = t('abgelehnt');
    try { const j = await r.json(); if(j.error) msg = j.error; } catch(e){}
    $('lpcState').textContent = msg;
    return;
  }
  lpcRefresh(); lpcTick();
}

function lpcProbe(){ return lpcStart('/api/lpc/probe'); }
function lpcRun(){ return lpcStart('/api/lpc/run'); }
function lpcFetch(){ return lpcStart('/api/lpc/fetch'); }

function lpcWrite(){
  if(!confirm(t('Den LPC jetzt löschen und neu programmieren? Die '
             + 'KNX-Verbindung ruht dabei für einige Sekunden.'))) return;
  return lpcStart('/api/lpc/write');
}

async function lpcUpload(){
  const f = $('lpcFile').files[0];
  if(!f) return;
  $('lpcFile').value = '';

  $('lpcState').textContent = t('Datei wird übertragen...');
  const fd = new FormData();
  fd.append('firmware', f);

  let r;
  try { r = await fetch('/api/lpc/upload', {method:'POST', body:fd}); }
  catch(e){ $('lpcState').textContent = t('Übertragung abgebrochen'); return; }

  if(r.ok){ await lpcRefresh(); $('lpcState').textContent = t('Datei geladen'); }
  else {
    let msg = t('abgelehnt');
    try { const j = await r.json(); if(j.error) msg = j.error; } catch(e){}
    $('lpcState').textContent = msg;
    await lpcRefresh();
  }
}

async function switchPart(){
  const p = ((last && last.build.partitions) || [])[1];

  if(!p){
    alert(t('Die zweite Partition ist noch nicht bekannt. Seite neu laden.'));
    return;
  }
  if(!p.valid){
    alert(t('Die zweite Partition enthält keine startfähige Firmware.'));
    return;
  }
  if(p.state === 'invalid' || p.state === 'aborted'){
    alert(t('Der Bootlader hat diese Partition verworfen. Nur ein neuer Upload hilft.'));
    return;
  }

  let msg = t('Beim nächsten Start die andere Partition verwenden? '
            + 'Das Gerät startet neu.');

  // A slot holding a bootable image we have never run is fair game, but the
  // user should know they are jumping to something unidentified.
  if(!p.firmware){
    msg += '\n\n' + t('Achtung: Der Inhalt ist unbekannt. Diese Partition '
         + 'hat unter der aktuellen Firmware noch nie gelaufen, dort liegt '
         + 'vermutlich ein älterer Stand.');
  }

  if(!confirm(msg)) return;
  const r = await fetch('/api/ota/switch', {method:'POST'});
  if(r.ok){ alert(t('Umgeschaltet. Das Gerät startet neu.')); return; }

  let why = '';
  try { why = (await r.json()).error || ''; } catch(e){}
  alert(t('Umschalten nicht möglich.') + (why ? '\n\n' + why : ''));
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
const SRC = {ntp:'NTP', rtc:'RTC', manual:'manuell',
             carried:'aus dem Betrieb vor dem Neustart', none:'keine'};

async function refreshTime(){
  let ts;
  try { ts = await (await fetch('/api/time')).json(); } catch(e){ return; }
  $('tsNow').textContent  = ts.local_time;

  /*
   * Eine mitgenommene Uhr ist gueltig, aber ungeprueft: der Software-Reset
   * loescht die Systemzeit nicht, also laeuft sie ohne RTC und ohne NTP
   * weiter. Gelb statt gruen, damit die Anzeige das nicht verschweigt.
   */
  $('tsSrc').innerHTML    = ts.clock_valid
      ? '<span class="dot ' + (ts.source === 'carried' ? 'warn' : 'ok') + '"></span>'
        + t(SRC[ts.source]||ts.source)
      : '<span class="dot err"></span>' + t('nicht gesetzt');
  $('tsNtpAct').innerHTML = ts.ntp_enabled
      ? (ts.ntp_active ? ts.ntp_active + (ts.ntp_dhcp_active ? ' <small>(DHCP)</small>' : '')
                       : '<span class="dim">' + t('keiner') + '</span>')
      : '<span class="dim">' + t('aus') + '</span>';
  $('tsRtc').innerHTML    = ts.rtc_present ? dot(true)
                          : '<span class="dim">' + t('nicht bestückt') + '</span>';
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

  /*
   * Eine POSIX-Regel errät niemand, also fuehrt die Liste. Steht im Geraet
   * etwas, das nicht darin vorkommt, springt die Auswahl auf "eigene Angabe"
   * und gibt das Textfeld frei - eine von Hand gepflegte Regel darf nicht
   * stillschweigend durch die naechstbeste ersetzt werden.
   */
  $('tsTz').value       = ts.tz;
  const known = [...$('tsTzSel').options].some(o => o.value === ts.tz && o.value !== '');
  $('tsTzSel').value    = known ? ts.tz : '';
  tzShow();

  const d = new Date(Date.now() - new Date().getTimezoneOffset()*60000);
  $('tsManual').value   = d.toISOString().slice(0,19);
  timeDlg.showModal();
}

function tzShow(){
  $('tsTz').style.display = ($('tsTzSel').value === '') ? '' : 'none';
}

function tzPick(){
  const picked = $('tsTzSel').value;
  if(picked) $('tsTz').value = picked;
  tzShow();
  if(!picked) $('tsTz').focus();
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

// Browser-Zeit übernehmen: Date.now() ist bereits UTC-basiert.
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
  if(!r.ok) alert(t('Senden nicht möglich - Uhr nicht gesetzt?'));
  setTimeout(refreshTime, 600);
}

// --- Hardware-Profil ---
const HWF = ['knx_uart','knx_rx','knx_tx','lpc_reset','lpc_isp',
             'i2c_sda','i2c_scl','eth_sck','eth_miso','eth_mosi',
             'eth_cs','eth_irq','eth_rst','eth_spi_mhz',
             'log_kib','monitor_kib'];
const HWID = {knx_uart:'hwUart', knx_rx:'hwRx', knx_tx:'hwTx',
              lpc_reset:'hwLpcRst', lpc_isp:'hwLpcIsp',
              i2c_sda:'hwSda', i2c_scl:'hwScl',
              eth_sck:'hwSck', eth_miso:'hwMiso', eth_mosi:'hwMosi',
              eth_cs:'hwCs', eth_irq:'hwIrq', eth_rst:'hwRst',
              log_kib:'hwLogKib', monitor_kib:'hwMonKib'};
let hwState = null;

// Row editors. Order matches the enums in hw_config.h - the index is what
// travels over the API, the text is only ever shown.
const TRIG  = ['kurzer Druck','langer Druck','sehr langer Druck'];
const LKIND = ['LED','RGB-LED'];
const RGBT  = ['WS2812','SK6812'];
const BFUNC = ['Programmiermodus','Werkeinstellungen','WLAN Grundeinstellung',
               'Gerät neu starten','WLAN ein/aus'];
const LCOND = ['Programmiermodus aktiv','AP-Modus offen','keine TP-Verbindung',
               'online','offline','Heartbeat','GA-Filter deaktiviert'];
const LCOL  = ['rot','grün','blau','gelb','cyan','magenta','weiß','orange'];
const LPAT  = ['Dauerlicht','langsam blinken','schnell blinken','Doppelblitz',
               'kurzer Blitz'];

const RTBL = {btn:'hwBtnTbl', led:'hwLedTbl', ba:'hwBaTbl', la:'hwLaTbl'};
const RKEY = {btn:'buttons', led:'leds', ba:'button_assign', la:'led_assign'};
const ROWS = {btn:[], led:[], ba:[], la:[]};
const RSEL = {btn:-1, led:-1, ba:-1, la:-1};

const FBR = {unconfigured:'Image-Standard', invalid:'ungültig &rarr; Standard',
             crashloop:'Startfehler &rarr; Standard'};

async function refreshHw(){
  try { hwState = await (await fetch('/api/hwconfig')).json(); }
  catch(e){ return; }
  const a = hwState.active;

  $('hwSrc').innerHTML = hwState.using_defaults
      ? '<span class="dot err"></span>' + t(FBR[hwState.fallback]||hwState.fallback)
      : dot(true) + ' ' + t('gespeichertes Profil');
  if(hwState.reboot_pending)
    $('hwSrc').innerHTML += ' <span class="warnbadge">' + t('Neustart nötig') + '</span>';

  $('hwKnx').textContent = 'UART' + a.knx_uart + ', RX ' + a.knx_rx + ', TX ' + a.knx_tx;
  $('hwLpc').textContent = (a.lpc_reset >= 0 && a.lpc_isp >= 0)
      ? 'Reset ' + a.lpc_reset + ', ISP ' + a.lpc_isp
        + (a.lpc_invert ? ', ' + t('invertiert') : '')
      : t('aus');

  const named = (list, fn) => list.length
      ? list.map(fn).join(', ') : t('keine');
  $('hwBtns').textContent = named(a.buttons || [],
      b => b.name + ' (GPIO ' + b.pin + ', ' + t(TRIG[b.trigger] || '?') + ')');
  $('hwLeds').textContent = named(a.leds || [],
      l => l.name + ' (GPIO ' + l.pin
         + (l.kind == 1 ? ', ' + RGBT[l.rgb_type] + ' #' + (l.rgb_index + 1) : '') + ')');
  $('hwI2c').textContent = a.i2c_enabled ? ('SDA ' + a.i2c_sda + ', SCL ' + a.i2c_scl)
                                         : t('aus');
  $('hwEth').textContent = a.eth_enabled
      ? ('SCK ' + a.eth_sck + ', MISO ' + a.eth_miso + ', MOSI ' + a.eth_mosi + ', CS ' + a.eth_cs)
      : t('aus');
  $('hwMem').textContent = t('Protokoll') + ' ' + a.log_kib + ' KiB, '
                         + t('Busmonitor') + ' ' + a.monitor_kib + ' KiB';
  $('hwUpd').textContent = a.update_url || t('aus');
  $('hwLpcDl').textContent = a.lpc_url || t('aus');
}

function hwFill(p){
  for(const k of HWF){ if(HWID[k]) $(HWID[k]).value = p[k]; }
  $('hwLpcInv').checked = p.lpc_invert;
  $('hwI2cEn').checked  = p.i2c_enabled;
  $('hwEthEn').checked  = p.eth_enabled;
  $('hwUpdUrl').value   = p.update_url || '';
  $('hwLpcUrl').value   = p.lpc_url || '';
  memSync();
  // Deep copy: editing a row must not change the document we compare against
  // when the user hits "Standard" again.
  for(const kind in RTBL){
    ROWS[kind] = (p[RKEY[kind]] || []).map(o => Object.assign({}, o));
    RSEL[kind] = -1;
    rowRender(kind);
  }
  syncGroups();
}

/* --- PSRAM-Aufteilung ---------------------------------------------------- *
 * Zahl und Regler zeigen denselben Wert; wer zuletzt bewegt wurde, gewinnt.
 * Der jeweils andere weicht aus, wenn das Budget sonst ueberschritten wuerde
 * - ein Formular, das erst beim Speichern meckert, laesst einen raten, welche
 * der beiden Zahlen zu gross war.
 * ------------------------------------------------------------------------ */

function memSync(src){
  const total = hwState ? (hwState.psram_kib || 0) : 0;
  const keep  = hwState ? (hwState.psram_reserve_kib || 0) : 0;
  const budget = total > keep ? total - keep : 0;

  // Die Regler kennen ihr Maximum erst, wenn das Geraet geantwortet hat.
  $('hwLogRng').max = $('hwMonRng').max = budget;

  if(src === 'hwLogRng')      $('hwLogKib').value = $('hwLogRng').value;
  else if(src === 'hwMonRng') $('hwMonKib').value = $('hwMonRng').value;

  let lg = Math.max(0, parseInt($('hwLogKib').value, 10) || 0);
  let mo = Math.max(0, parseInt($('hwMonKib').value, 10) || 0);

  if(budget > 0 && lg + mo > budget){
    // Das zuletzt angefasste Feld behaelt seinen Wert.
    if(src === 'hwMonKib' || src === 'hwMonRng') lg = Math.max(0, budget - mo);
    else                                         mo = Math.max(0, budget - lg);
  }

  $('hwLogKib').value = $('hwLogRng').value = lg;
  $('hwMonKib').value = $('hwMonRng').value = mo;

  const span = budget || 1;
  $('memLog').style.width = (100 * Math.min(lg, span) / span) + '%';
  $('memMon').style.width = (100 * Math.min(mo, span) / span) + '%';
  $('memRes').style.width = (100 * Math.max(0, span - lg - mo) / span) + '%';

  $('hwMemInfo').textContent = total
      ? (total + ' KiB PSRAM, ' + keep + ' ' + t('KiB reserviert') + ', '
         + Math.max(0, budget - lg - mo) + ' ' + t('KiB frei') + ' \u2013 '
         + t('Monitor fasst') + ' ' + Math.floor(mo * 1024 / 48) + ' '
         + t('Telegramme'))
      : t('Ohne PSRAM bleiben die Werte ohne Wirkung.');
}

/* --- Zeileneditoren ------------------------------------------------------ *
 * Die Zeilen leben in ROWS, die Tabelle ist nur die Anzeige. Vor jedem
 * Neuzeichnen wird das DOM zurückgeschrieben, sonst gingen Eingaben beim
 * Hinzufügen einer Zeile verloren.
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

function nameCell(v, kind){
  // maxlength und pattern spiegeln nur, was die Firmware ohnehin prüft.
  // onchange statt oninput: beim Verlassen des Feldes ist der Name fertig,
  // und die Zuordnungstabelle kann ihn sofort anbieten.
  const dep = (kind === 'btn') ? 'ba' : 'la';
  return '<input maxlength="16" pattern="[A-Za-z0-9_-]{1,16}" ' +
         'title="' + esc(t('1 bis 16 Zeichen aus A-Z a-z 0-9 _ -')) + '" ' +
         'onchange="rowSync(\'' + kind + '\');rowRender(\'' + dep + '\')" value="' +
         esc(v == null ? '' : v) + '">';
}

// Kopfzeilen und Kurzhilfen je Tabelle.
const RHEAD = {
  btn: [['Name',     'Frei wählbarer Name zur Zuordnung weiter unten'],
        ['GPIO',     'Eingang, an dem der Taster gegen Masse schaltet'],
        ['Auslösung','Kurz unter 1 s, lang ab 2 s, sehr lang ab 6 s']],
  led: [['Name',     'Frei wählbarer Name zur Zuordnung weiter unten'],
        ['GPIO',     'Ausgang zur LED bzw. Datenleitung der Kette'],
        ['Typ',      'Einfache LED an einem GPIO oder Position in einer Kette'],
        ['Chip',     'Nur bei adressierbaren LEDs: Bitdauer des Chips'],
        ['Position', 'Platz in der Kette, die erste LED ist die 1.']],
  ba:  [['Taster',   'Name aus der Tastertabelle'],
        ['Funktion', 'Was der Taster auslöst']],
  la:  [['LED',      'Name aus der LED-Tabelle'],
        ['Zustand',  'Wann diese Zeile gilt'],
        ['Farbe',    'Nur bei adressierbaren LEDs'],
        ['Muster',   'Wie die LED während des Zustands moduliert wird']]
};

function rowRender(kind){
  const tbl = $(RTBL[kind]);
  const gin = hwState ? hwState.gpio_in : [];
  const gout = hwState ? hwState.gpio_out : [];

  let html = '<tr class="hd">' + RHEAD[kind].map(h =>
    '<th title="' + esc(t(h[1])) + '">' + t(h[0]) + '</th>').join('') + '</tr>';

  ROWS[kind].forEach((r, i) => {
    const sel = (RSEL[kind] === i) ? ' class="sel"' : '';
    html += '<tr' + sel + ' onclick="rowPick(\'' + kind + '\',' + i + ')">';

    if(kind === 'btn'){
      html += '<td>' + nameCell(r.name, 'btn') + '</td>';
      html += '<td><select>' + gpioOptions(gin, r.pin) + '</select></td>';
      html += '<td><select>' + pickOptions(TRIG, r.trigger) + '</select></td>';
    }
    else if(kind === 'led'){
      html += '<td>' + nameCell(r.name, 'led') + '</td>';
      html += '<td><select>' + gpioOptions(gout, r.pin) + '</select></td>';
      html += '<td><select onchange="rowSync(\'led\');rowRender(\'led\');rowRender(\'la\')">'
            + pickOptions(LKIND, r.kind) + '</select></td>';
      if(r.kind == 1){
        html += '<td><select>' + pickOptions(RGBT, r.rgb_type) + '</select></td>';
        // Anzeige ab 1, gespeichert ab 0: "die erste LED" ist die 1.
        html += '<td><input type="number" min="1" max="64" value="'
              + ((r.rgb_index|0) + 1) + '"></td>';
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
      // gesperrt - so ist ohne Erklärung klar, dass es hier nichts tut.
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

/** DOM zurück in ROWS schreiben. */
function rowSync(kind){
  const rows = $(RTBL[kind]).querySelectorAll('tr:not(.hd)');

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
      /*
       * Nach der Feldzahl entscheiden, nicht nach dem gewaehlten Typ.
       *
       * Beim Umschalten des Typs feuert onchange, bevor die Zeile neu
       * gezeichnet ist: Das Auswahlfeld meldet dann schon "RGB-LED",
       * waehrend im DOM noch die Checkbox der einfachen LED steht. Wer
       * hier dem Wert glaubt, liest die Checkbox als Chiptyp und laeuft
       * beim nicht vorhandenen Positionsfeld auf einen Fehler - die Zeile
       * bekommt stillschweigend Unsinn.
       */
      if(f.length >= 5){
        r.rgb_type  = parseInt(f[3].value, 10);
        r.rgb_index = parseInt(f[4].value, 10) - 1;
        if(!(r.rgb_index >= 0)) r.rgb_index = 0;
      } else if(f.length >= 4){
        r.active_low = f[3].checked;
      }
      r.kind = parseInt(f[2].value, 10);
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
  // Kein Neuzeichnen: ein Klick ins Namensfeld würde sonst den Cursor
  // verlieren, weil die Tabelle unter der Eingabe neu gebaut wird.
  RSEL[kind] = i;
  $(RTBL[kind]).querySelectorAll('tr:not(.hd)')
               .forEach((tr, n) => tr.classList.toggle('sel', n === i));
}

const RMAX = {btn:8, led:8, ba:8, la:12};

function rowAdd(kind){
  rowSync(kind);
  if(ROWS[kind].length >= RMAX[kind]){
    $('hwErr').textContent = t('mehr Zeilen sind nicht möglich');
    return;
  }
  if(kind === 'btn')
    ROWS.btn.push({name:'', pin:(hwState.gpio_in||[0])[0], trigger:0});
  else if(kind === 'led')
    ROWS.led.push({name:'', pin:(hwState.gpio_out||[0])[0], kind:0,
                   active_low:false, rgb_type:0, rgb_index:0});  else if(kind === 'ba')
    ROWS.ba.push({target: ROWS.btn.map(x=>x.name)[0] || '', function:0});
  else
    ROWS.la.push({target: ROWS.led.map(x=>x.name)[0] || '',
                  condition:3, colour:1, pattern:0});
  RSEL[kind] = ROWS[kind].length - 1;
  rowRender(kind);
  // Eine neue Taste oder LED soll sofort in der Zuordnung auswählbar sein,
  // ohne den Dialog zu schließen.
  if(kind === 'btn') rowRender('ba');
  if(kind === 'led') rowRender('la');
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
  // Eine gelöschte LED oder Taste darf in keiner Zuordnung stehenbleiben.
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
  p.lpc_invert     = $('hwLpcInv').checked;
  p.i2c_enabled    = $('hwI2cEn').checked;
  p.eth_enabled    = $('hwEthEn').checked;
  p.update_url     = $('hwUpdUrl').value.trim();
  p.lpc_url        = $('hwLpcUrl').value.trim();
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
    // Reine Zuordnungen wirken sofort - dann nicht nach einem Neustart
    // fragen, den niemand braucht.
    let needs = true;
    try { needs = (await r.json()).reboot_required !== false; } catch(e){}
    refreshHw();
    if(needs){
      if(confirm(t('Profil gespeichert. Jetzt neu starten?'))) doReboot();
    }
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
  const r = await fetch('/api/hwconfig/reset', {method:'POST'});
  if(!r.ok){
    let why = '';
    try { why = (await r.json()).error || ''; } catch(e){}
    alert(t('Zurücksetzen fehlgeschlagen.') + (why ? '\n\n' + why : ''));
    return;
  }
  hwDlg.close();
  refreshHw();
  if(confirm(t('Zurückgesetzt. Jetzt neu starten?'))) doReboot();
}

async function factoryReset(){
  if(!confirm(t('Werkseinstellungen herstellen?\n\n'
            + 'Gelöscht werden Hardware-Profil, WLAN-Zugangsdaten, '
            + 'KNX-Konfiguration, Betriebsstunden und ein gesetztes '
            + 'Passwort. Das Gerät öffnet danach wieder einen offenen '
            + 'Access Point.'))) return;
  if(!confirm(t('Wirklich alles löschen? Das lässt sich nicht rückgängig machen.'))) return;

  const r = await fetch('/api/factory', {method:'POST'});
  alert(t(r.ok ? 'Wird gelöscht. Das Gerät startet neu und ist danach '
               + 'nur noch über seinen Access Point erreichbar.'
               : 'Löschen nicht möglich.'));
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
  catch(e){ alert(t('Keine gültige JSON-Datei.')); return; }
  if(!await hwPost(p)) { hwDlg.showModal(); hwFill(p); }
}

async function doReboot(){
  await fetch('/api/reboot', {method:'POST'});
  alert(t('Neustart läuft. Die Seite in einigen Sekunden neu laden.'));
}

function askReboot(){
  if(confirm(t('Gerät jetzt neu starten?'))) doReboot();
}

function openWifi(){
  // Im Ethernet-Betrieb ist das WLAN gar nicht gestartet.
  if(last && last.iface === 'ethernet'){
    alert(t('Das Gerät läuft über Ethernet. WLAN ist nicht aktiv.'));
    return;
  }
  wifiDlg.showModal(); scan();
}

async function scan(){
  const sel = $('ssidSel');
  sel.innerHTML = '<option>' + t('Suche läuft...') + '</option>';
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
  sel.innerHTML = '<option>' + t('Zeitüberschreitung') + '</option>';
}

async function connect(){
  const body = new URLSearchParams({ssid:$('ssidSel').value, password:$('pw').value});
  await fetch('/api/wifi/connect', {method:'POST', body});
  wifiDlg.close();
  alert(t('Zugangsdaten gespeichert. Das Gerät startet neu.'));
}

async function checkUpdate(){
  $('updState').textContent = t('prüfe...');
  await fetch('/api/update/check');
  pollUpdate();
}

async function installUpdate(){
  if(!confirm(t('Firmware jetzt installieren? Das Gerät startet danach neu.'))) return;
  await fetch('/api/update/install', {method:'POST'});
  pollUpdate();
}

async function pollUpdate(){
  const u = await (await fetch('/api/update/status')).json();
  let text = u.state;
  if(u.error) text += ' - ' + u.error;
  else if(u.available) text = t('Version %s verfügbar').replace('%s', u.latest);
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
 * Über http://<IP>/ ist die API schlicht nicht vorhanden. Dann bleibt nur
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
    $('updState').textContent = t('berechne Prüfsumme...');
    try { hash = await sha256Hex(file); $('fwHash').value = hash; }
    catch(e){ hash = ''; }
  }

  if(!hash && !confirm(t('Kein SHA-256 angegeben. Firmware ungeprüft übertragen?'))) return;

  $('updState').textContent = t('lade hoch...');
  const fd = new FormData();
  fd.append('firmware', file);

  const headers = {};
  if(/^[0-9a-f]{64}$/.test(hash)) headers['X-SHA256'] = hash;

  let r;
  try { r = await fetch('/api/ota', {method:'POST', body:fd, headers}); }
  catch(e){ $('updState').textContent = t('Übertragung abgebrochen'); return; }

  if(r.ok){
    $('updState').textContent = t('erfolgreich, Neustart...');
  } else {
    let msg = t('fehlgeschlagen');
    try { const j = await r.json(); if(j.error) msg += ' - ' + j.error; } catch(e){}
    $('updState').textContent = msg;
  }
}

applyLang();

/*
 * Selbstplanende Abfrage statt setInterval.
 *
 * setInterval feuert unabhängig davon, ob die vorige Antwort schon da ist.
 * Braucht das Gerät einmal länger als das Intervall, stapeln sich die
 * Anfragen: der Browser öffnet für jede eine Verbindung, dem ESP32 gehen
 * die TCP-Slots aus, neue Verbindungen laufen in Wiederholungen mit
 * wachsender Wartezeit - und jede Bedienung hängt hinter dem Rückstau.
 * Genau so werden aus 200 ms Verzögerung 10 bis 30 Sekunden.
 *
 * Deshalb wird erst nach der Antwort neu geplant, und im Hintergrund gar
 * nicht: ein unsichtbarer Tab muss nichts abfragen.
 */
function poll(fn, delay){
  const tick = async () => {
    if(!document.hidden){
      try { await fn(); } catch(e){}
    }
    setTimeout(tick, delay);
  };
  setTimeout(tick, delay);
}

poll(refresh, 2000);
poll(refreshTime, 5000);

// Nach dem Zurückholen des Tabs sofort auffrischen statt bis zum nächsten
// Intervall veraltete Werte zu zeigen.
document.addEventListener('visibilitychange', () => {
  if(!document.hidden){ refresh(); refreshTime(); }
});
</script>
</body>
</html>
)HTML";
