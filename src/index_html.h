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
cursor:pointer;border:1px solid var(--line);background:var(--card)}
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
padding:9px 16px;font-size:13px;cursor:pointer;font-family:inherit}
button.sec{background:transparent;border:1px solid var(--line);color:var(--fg)}
button:disabled{opacity:.5;cursor:not-allowed}
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
.dim{color:var(--dim)}
dialog label{display:block;margin-top:10px;font-size:12px;color:var(--dim)}
.trio{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px}
.trio input{margin:6px 0}
small{color:var(--dim)}
</style>
</head>
<body>
<header>
  <h1>Selfbus KNX/IP Interface</h1>
  <span class="badge" id="netBadge" onclick="openWifi()">...</span>
</header>

<main>
  <section class="card">
    <h2>Status</h2>
    <div class="row"><span>Laufzeit</span><span id="uptime">-</span></div>
    <div class="row"><span>Physikalische Adresse</span><span id="pa">-</span></div>
    <div class="row"><span>ETS-Konfiguration</span><span id="cfg">-</span></div>
    <div class="row"><span>Tunnel (max.)</span><span id="tun">-</span></div>
    <div class="actions">
      <button id="pmBtn" onclick="toggleProg()">Programmiermodus</button>
    </div>
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
    <div class="row"><span>MAC</span><span id="mac">-</span></div>
    <div class="row"><span>Signal</span><span id="rssi">-</span></div>
    <div class="row"><span>Ethernet (W5500)</span><span id="ethSt">-</span></div>
  </section>

  <section class="card">
    <h2>System</h2>
    <div class="row"><span>Chip</span><span id="chip">-</span></div>
    <div class="row"><span>Takt</span><span id="cpu">-</span></div>
    <div class="row"><span>Freier Speicher</span><span id="heap">-</span></div>
    <div class="row"><span>Partition</span><span id="part">-</span></div>
  </section>

  <section class="card">
    <h2>Hardware-Profil</h2>
    <div class="row"><span>Quelle</span><span id="hwSrc">-</span></div>
    <div class="row"><span>KNX-UART</span><span id="hwKnx">-</span></div>
    <div class="row"><span>LED / Taster</span><span id="hwLed">-</span></div>
    <div class="row"><span>I2C (RTC)</span><span id="hwI2c">-</span></div>
    <div class="row"><span>SPI (W5500)</span><span id="hwEth">-</span></div>
    <div class="actions">
      <button class="sec" onclick="openHw()">Bearbeiten</button>
      <button class="sec" onclick="hwFile.click()">JSON laden</button>
      <button class="sec" onclick="hwDownload()">JSON speichern</button>
      <input type="file" id="hwFile" accept=".json" style="display:none" onchange="hwUpload()">
    </div>
    <p><small>Aenderungen werden erst nach einem Neustart aktiv.</small></p>
  </section>

  <section class="card">
    <h2>Firmware</h2>
    <div class="row"><span>Version</span><span id="ver">-</span></div>
    <div class="row"><span>Build</span><span id="build">-</span></div>
    <div class="row"><span>Update</span><span id="updState">-</span></div>
    <div class="bar" id="updBar" style="display:none"><i id="updFill"></i></div>
    <div class="actions">
      <button class="sec" onclick="checkUpdate()">Online pruefen</button>
      <button class="sec" id="instBtn" onclick="installUpdate()" disabled>Installieren</button>
      <button class="sec" onclick="fw.click()">Datei hochladen</button>
      <input type="file" id="fw" accept=".bin" style="display:none" onchange="upload()">
    </div>
    <label style="display:block;margin-top:10px;font-size:12px;color:var(--dim)">
      SHA-256 der Datei (optional, aus <code>sha256sum</code>)</label>
    <input id="fwHash" placeholder="64 Hex-Zeichen &ndash; leer = ungeprueft">
    <p><small id="hashNote"></small></p>
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

<dialog id="timeDlg">
  <h2>Zeitserver</h2>
  <label><input type="checkbox" id="tsEn"> Telegramme senden</label>
  <label>Gruppenadresse Datum+Zeit (DPT 19.001)</label>
  <input id="tsGaDt" placeholder="z.B. 0/0/1 &ndash; leer = aus">
  <label>Gruppenadresse Uhrzeit (DPT 10.001)</label>
  <input id="tsGaT" placeholder="leer = aus">
  <label>Gruppenadresse Datum (DPT 11.001)</label>
  <input id="tsGaD" placeholder="leer = aus">
  <label>Sendeintervall (Minuten)</label>
  <input id="tsIvl" type="number" min="1" max="1440">
  <label><input type="checkbox" id="tsNtpEn"> NTP verwenden</label>
  <label><input type="checkbox" id="tsNtpDhcp"> NTP-Server vom DHCP beziehen</label>
  <label>NTP-Server (Fallback, wenn DHCP keinen liefert)</label>
  <input id="tsNtpSrv" placeholder="pool.ntp.org">
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
  <p><small id="hwChip"></small></p>

  <label>KNX &ndash; UART-Nummer / RX / TX</label>
  <div class="trio">
    <input id="hwUart" type="number" min="0">
    <input id="hwRx"   type="number" min="-1">
    <input id="hwTx"   type="number" min="-1">
  </div>

  <label>Programmier-LED / Taster (&minus;1 = nicht bestueckt)</label>
  <div class="trio">
    <input id="hwLedPin" type="number" min="-1">
    <input id="hwBtn"    type="number" min="-1">
    <label style="margin:0;align-self:center">
      <input type="checkbox" id="hwLedLow"> LED low-aktiv</label>
  </div>

  <label><input type="checkbox" id="hwI2cEn"> RTC ueber I2C (SDA / SCL)</label>
  <div class="trio">
    <input id="hwSda" type="number" min="-1">
    <input id="hwScl" type="number" min="-1">
    <span></span>
  </div>

  <label><input type="checkbox" id="hwEthEn"> Ethernet W5500 (SCK / MISO / MOSI)</label>
  <div class="trio">
    <input id="hwSck"  type="number" min="-1">
    <input id="hwMiso" type="number" min="-1">
    <input id="hwMosi" type="number" min="-1">
  </div>
  <label>W5500 &ndash; CS / IRQ / RST (&minus;1 = ungenutzt)</label>
  <div class="trio">
    <input id="hwCs"  type="number" min="-1">
    <input id="hwIrq" type="number" min="-1">
    <input id="hwRst" type="number" min="-1">
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
const dot = ok => `<span class="dot ${ok?'ok':'err'}"></span>${ok?'OK':'Fehler'}`;

async function refresh(){
  let s;
  try { s = await (await fetch('/api/status')).json(); }
  catch(e){ $('netBadge').textContent = 'offline'; return; }

  $('uptime').textContent = s.uptime;
  $('pa').textContent     = s.knx_pa;
  $('cfg').innerHTML      = s.knx_configured ? dot(true)
                          : '<span class="dot err"></span>nicht programmiert';
  $('tun').textContent    = s.knx_max_tunnels;
  $('pmBtn').textContent  = s.prog_mode ? 'Programmiermodus AUS' : 'Programmiermodus EIN';

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
  $('mac').textContent  = s.mac;
  $('rssi').textContent = (s.is_ap_mode || s.iface === 'ethernet') ? '-' : s.rssi + ' dBm';

  const ES = {ready:'verbunden', no_link:'kein Link', no_ip:'keine IP-Adresse',
              absent:'nicht gefunden', disabled:'nicht konfiguriert'};
  const eok = s.eth.state === 'ready';
  $('ethSt').innerHTML = (s.eth.state === 'disabled' || s.eth.state === 'absent')
      ? '<span class="dim">' + (ES[s.eth.state]||s.eth.state) + '</span>'
      : (eok ? dot(true) + ' ' + s.eth.speed + ' Mbit/s ' + s.eth.duplex
             : '<span class="dot err"></span>' + (ES[s.eth.state]||s.eth.state));

  const ifn = {ethernet:'Ethernet', wifi:'WLAN'};
  $('ifc').textContent = ifn[s.iface] || s.iface;

  $('chip').textContent = s.hardware.chip_model + ' rev ' + s.hardware.chip_rev;
  $('cpu').textContent  = s.hardware.cpu_freq + ' MHz';
  $('heap').textContent = Math.round(s.hardware.heap_free/1024) + ' / '
                        + Math.round(s.hardware.heap_total/1024) + ' KiB';
  $('part').textContent = s.build.partition + ' (' + s.build.ota_state + ')';
  $('ver').textContent  = s.build.version;
  $('build').textContent= '#' + s.build.number + ' / ' + s.build.git;

  const b = $('netBadge');
  b.textContent = s.is_ap_mode ? 'AP-Modus aktiv'
                : (s.iface === 'ethernet' ? 'Ethernet'
                : (s.wifi_connected ? s.ssid : 'getrennt'));
  b.className   = 'badge ' + (s.is_ap_mode ? 'warn' : (s.wifi_connected ? 'ok' : ''));
  b.style.cursor = (s.iface === 'ethernet') ? 'default' : 'pointer';
}

async function toggleProg(){
  await fetch('/api/progmode', {method:'POST'});
  setTimeout(refresh, 300);
}

// --- Zeitserver ---
const SRC = {ntp:'NTP', rtc:'RTC', manual:'manuell', none:'keine'};

async function refreshTime(){
  let t;
  try { t = await (await fetch('/api/time')).json(); } catch(e){ return; }
  $('tsNow').textContent  = t.local_time;
  $('tsSrc').innerHTML    = t.clock_valid ? dot(true) + ' ' + (SRC[t.source]||t.source)
                                          : '<span class="dot err"></span>nicht gesetzt';
  $('tsNtpAct').innerHTML = t.ntp_enabled
      ? (t.ntp_active ? t.ntp_active + (t.ntp_dhcp_active ? ' <small>(DHCP)</small>' : '')
                      : '<span class="dim">keiner</span>')
      : '<span class="dim">aus</span>';
  $('tsRtc').innerHTML    = t.rtc_present ? dot(true) : '<span class="dim">nicht bestueckt</span>';
  $('tsNext').textContent = t.enabled ? (t.next_send_s + ' s') : 'deaktiviert';
  return t;
}

async function openTime(){
  const t = await refreshTime();
  if(!t) return;
  $('tsEn').checked     = t.enabled;
  $('tsGaDt').value     = t.ga_datetime;
  $('tsGaT').value      = t.ga_time;
  $('tsGaD').value      = t.ga_date;
  $('tsIvl').value      = t.interval_min;
  $('tsNtpEn').checked  = t.ntp_enabled;
  $('tsNtpDhcp').checked= t.ntp_from_dhcp;
  $('tsNtpSrv').value   = t.ntp_server;
  $('tsTz').value       = t.tz;
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
  if(!r.ok) alert('Zeit konnte nicht gesetzt werden.');
  setTimeout(refreshTime, 600);
}

// Manuelle Eingabe: datetime-local ist Ortszeit, daher in UTC umrechnen.
async function setManual(){
  const v = $('tsManual').value;
  if(!v){ alert('Bitte Datum und Uhrzeit eingeben.'); return; }
  const epoch = Math.floor(new Date(v).getTime()/1000);
  const body = new URLSearchParams({epoch});
  const r = await fetch('/api/time/set', {method:'POST', body});
  if(!r.ok) alert('Zeit konnte nicht gesetzt werden.');
  setTimeout(refreshTime, 600);
}

async function sendTime(){
  const r = await fetch('/api/time/send', {method:'POST'});
  if(!r.ok) alert('Senden nicht moeglich - Uhr nicht gesetzt?');
  setTimeout(refreshTime, 600);
}

// --- Hardware-Profil ---
const HWF = ['knx_uart','knx_rx','knx_tx','led','button',
             'i2c_sda','i2c_scl','eth_sck','eth_miso','eth_mosi',
             'eth_cs','eth_irq','eth_rst','eth_spi_mhz'];
const HWID = {knx_uart:'hwUart', knx_rx:'hwRx', knx_tx:'hwTx',
              led:'hwLedPin', button:'hwBtn', i2c_sda:'hwSda', i2c_scl:'hwScl',
              eth_sck:'hwSck', eth_miso:'hwMiso', eth_mosi:'hwMosi',
              eth_cs:'hwCs', eth_irq:'hwIrq', eth_rst:'hwRst'};
let hwState = null;

const FBR = {unconfigured:'Image-Standard', invalid:'ungueltig &rarr; Standard',
             crashloop:'Startfehler &rarr; Standard', button:'Taster &rarr; Standard'};

async function refreshHw(){
  try { hwState = await (await fetch('/api/hwconfig')).json(); }
  catch(e){ return; }
  const a = hwState.active;

  $('hwSrc').innerHTML = hwState.using_defaults
      ? '<span class="dot err"></span>' + (FBR[hwState.fallback]||hwState.fallback)
      : dot(true) + ' gespeichertes Profil';
  if(hwState.reboot_pending)
    $('hwSrc').innerHTML += ' <small>(Neustart noetig)</small>';

  $('hwKnx').textContent = 'UART' + a.knx_uart + ', RX ' + a.knx_rx + ', TX ' + a.knx_tx;
  $('hwLed').textContent = 'GPIO ' + a.led + (a.led_active_low ? ' (low)' : '')
                         + ' / ' + (a.button < 0 ? 'kein' : 'GPIO ' + a.button);
  $('hwI2c').textContent = a.i2c_enabled ? ('SDA ' + a.i2c_sda + ', SCL ' + a.i2c_scl) : 'aus';
  $('hwEth').textContent = a.eth_enabled
      ? ('SCK ' + a.eth_sck + ', MISO ' + a.eth_miso + ', MOSI ' + a.eth_mosi + ', CS ' + a.eth_cs)
      : 'aus';
}

function hwFill(p){
  for(const k of HWF){ if(HWID[k]) $(HWID[k]).value = p[k]; }
  $('hwLedLow').checked = p.led_active_low;
  $('hwI2cEn').checked  = p.i2c_enabled;
  $('hwEthEn').checked  = p.eth_enabled;
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
  p.led_active_low = $('hwLedLow').checked;
  p.i2c_enabled    = $('hwI2cEn').checked;
  p.eth_enabled    = $('hwEthEn').checked;
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
    if(confirm('Profil gespeichert. Jetzt neu starten?')) doReboot();
    return true;
  }
  let msg = 'abgelehnt';
  try { const j = await r.json(); if(j.error) msg = j.error; } catch(e){}
  $('hwErr').textContent = msg;
  return false;
}

function hwSave(){ return hwPost(hwCollect()); }

async function hwReset(){
  if(!confirm('Gespeichertes Profil verwerfen und die Werte des Images verwenden?')) return;
  await fetch('/api/hwconfig/reset', {method:'POST'});
  hwDlg.close();
  refreshHw();
  if(confirm('Zurueckgesetzt. Jetzt neu starten?')) doReboot();
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
  catch(e){ alert('Keine gueltige JSON-Datei.'); return; }
  if(!await hwPost(p)) { hwDlg.showModal(); hwFill(p); }
}

async function doReboot(){
  await fetch('/api/reboot', {method:'POST'});
  alert('Neustart laeuft. Die Seite in einigen Sekunden neu laden.');
}

function openWifi(){
  // Im Ethernet-Betrieb ist das WLAN gar nicht gestartet.
  if($('ifc').textContent === 'Ethernet'){
    alert('Das Geraet laeuft ueber Ethernet. WLAN ist nicht aktiv.');
    return;
  }
  wifiDlg.showModal(); scan();
}

async function scan(){
  const sel = $('ssidSel');
  sel.innerHTML = '<option>Suche laeuft...</option>';
  await fetch('/api/wifi/scan?start=1');
  for(let i=0;i<20;i++){
    await new Promise(r=>setTimeout(r,900));
    const r = await (await fetch('/api/wifi/scan')).json();
    if(Array.isArray(r)){
      sel.innerHTML = r.length
        ? r.map(n=>`<option value="${n.ssid}">${n.ssid} (${n.rssi} dBm)</option>`).join('')
        : '<option>Kein Netz gefunden</option>';
      return;
    }
  }
  sel.innerHTML = '<option>Zeitueberschreitung</option>';
}

async function connect(){
  const body = new URLSearchParams({ssid:$('ssidSel').value, password:$('pw').value});
  await fetch('/api/wifi/connect', {method:'POST', body});
  wifiDlg.close();
  alert('Zugangsdaten gespeichert. Das Geraet startet neu.');
}

async function checkUpdate(){
  $('updState').textContent = 'pruefe...';
  await fetch('/api/update/check');
  pollUpdate();
}

async function installUpdate(){
  if(!confirm('Firmware jetzt installieren? Das Geraet startet danach neu.')) return;
  await fetch('/api/update/install', {method:'POST'});
  pollUpdate();
}

async function pollUpdate(){
  const u = await (await fetch('/api/update/status')).json();
  let text = u.state;
  if(u.error) text += ' - ' + u.error;
  else if(u.available) text = 'Version ' + u.latest + ' verfuegbar';
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

$('hashNote').textContent = CAN_HASH
  ? 'Wird beim Hochladen automatisch berechnet.'
  : 'Automatische Berechnung nicht moeglich (kein HTTPS). Hash ggf. manuell eintragen.';

async function sha256Hex(file){
  const buf = await file.arrayBuffer();
  const dig = await crypto.subtle.digest('SHA-256', buf);
  return [...new Uint8Array(dig)].map(b=>b.toString(16).padStart(2,'0')).join('');
}

async function upload(){
  const file = $('fw').files[0];
  if(!file) return;

  let hash = $('fwHash').value.trim().toLowerCase();

  if(!hash && CAN_HASH){
    $('updState').textContent = 'berechne Pruefsumme...';
    try { hash = await sha256Hex(file); $('fwHash').value = hash; }
    catch(e){ hash = ''; }
  }

  if(!hash && !confirm('Kein SHA-256 angegeben. Firmware ungeprueft uebertragen?')) return;

  $('updState').textContent = 'lade hoch...';
  const fd = new FormData();
  fd.append('firmware', file);

  const headers = {};
  if(/^[0-9a-f]{64}$/.test(hash)) headers['X-SHA256'] = hash;

  const r = await fetch('/api/ota', {method:'POST', body:fd, headers});
  if(r.ok){
    $('updState').textContent = 'erfolgreich, Neustart...';
  } else {
    let msg = 'fehlgeschlagen';
    try { const j = await r.json(); if(j.error) msg += ' - ' + j.error; } catch(e){}
    $('updState').textContent = msg;
  }
}

refresh();
refreshTime();
refreshHw();
setInterval(refresh, 2000);
setInterval(refreshTime, 5000);
</script>
</body>
</html>
)HTML";
