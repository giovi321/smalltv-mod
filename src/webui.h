// webui.h — single-page config UI served from PROGMEM
//
// Tabs are segmented per feature: shared Status/WiFi/Display/Update plus one tab
// per feature (Ticker / Usage; Radar is added with WITH_RADAR). The config JSON
// mirrors the nested Settings layout: { ..shared.., ticker:{...}, usage:{...} }.
#pragma once
#include <Arduino.h>

static const char WEBUI_HTML[] PROGMEM = R"HTMLPAGE(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>SmallTV</title>
<style>
:root{--bg:#0e1116;--card:#171c24;--mut:#8b96a5;--fg:#e6edf3;--acc:#3fb950;--acc2:#2f81f7;--red:#f85149;--bd:#262d38}
*{box-sizing:border-box}
body{margin:0;font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:var(--bg);color:var(--fg);font-size:15px}
header{padding:14px 16px;border-bottom:1px solid var(--bd);display:flex;align-items:center;gap:10px}
header h1{font-size:17px;margin:0;font-weight:600}
header .dot{width:9px;height:9px;border-radius:50%;background:var(--mut)}
header .dot.ok{background:var(--acc)}
nav{display:flex;gap:4px;padding:8px;overflow-x:auto;border-bottom:1px solid var(--bd);position:sticky;top:0;background:var(--bg);z-index:5}
nav button{background:none;border:0;color:var(--mut);padding:8px 12px;border-radius:8px;font-size:14px;cursor:pointer;white-space:nowrap}
nav button.active{background:var(--card);color:var(--fg)}
main{padding:16px;max-width:680px;margin:0 auto}
.tab{display:none}.tab.active{display:block}
.card{background:var(--card);border:1px solid var(--bd);border-radius:12px;padding:16px;margin-bottom:14px}
h2{font-size:14px;text-transform:uppercase;letter-spacing:.04em;color:var(--mut);margin:0 0 12px}
label{display:block;margin:10px 0 4px;font-size:13px;color:var(--mut)}
input[type=text],input[type=password],input[type=number],input[type=url],select{
 width:100%;padding:9px 10px;background:#0b0e13;border:1px solid var(--bd);border-radius:8px;color:var(--fg);font-size:15px}
input[type=range]{width:100%}
.row{display:flex;gap:10px}.row>*{flex:1}
.chk{display:flex;align-items:center;gap:8px;margin:8px 0}
.chk input{width:18px;height:18px}
.chk label{margin:0;color:var(--fg);font-size:14px}
button.btn{background:var(--acc);color:#04130a;border:0;padding:10px 16px;border-radius:9px;font-size:15px;font-weight:600;cursor:pointer}
button.btn.sec{background:#222b36;color:var(--fg)}
button.btn.danger{background:var(--red);color:#1a0606}
button.btn:disabled{opacity:.5}
.muted{color:var(--mut);font-size:13px}
table{width:100%;border-collapse:collapse}
td{padding:6px 4px}
.symrow input{margin:0}
.kv{display:flex;justify-content:space-between;padding:5px 0;border-bottom:1px solid var(--bd)}
.kv:last-child{border:0}.kv b{font-weight:600}
.toast{position:fixed;bottom:16px;left:50%;transform:translateX(-50%);background:#0b0e13;border:1px solid var(--bd);padding:10px 16px;border-radius:10px;opacity:0;transition:.3s;pointer-events:none}
.toast.show{opacity:1}
.net{padding:8px;border:1px solid var(--bd);border-radius:8px;margin:4px 0;cursor:pointer;display:flex;justify-content:space-between}
.net:hover{border-color:var(--acc2)}
.bar{height:8px;background:#0b0e13;border-radius:6px;overflow:hidden;margin-top:8px}
.bar>div{height:100%;width:0;background:var(--acc2);transition:.2s}
small.hint{display:block;color:var(--mut);margin-top:4px;font-size:12px}
.chip{display:inline-block;margin-left:8px;padding:2px 8px;border-radius:10px;font-size:11px;font-weight:600;letter-spacing:.03em;background:var(--acc2);color:#fff;vertical-align:middle}
</style></head>
<body>
<header><span id="dot" class="dot"></span><h1>SmallTV</h1><span id="chip" class="chip" style="display:none"></span><span id="hi" class="muted"></span></header>
<nav>
 <button data-t="status" class="active">Status</button>
 <button data-t="wifi">WiFi</button>
 <button data-t="display">Display</button>
 <button data-t="ticker">Ticker</button>
 <button data-t="usage">Usage</button>
 <button data-t="radar">Radar</button>
 <button data-t="update">Update</button>
</nav>
<main>
 <!-- STATUS -->
 <section id="status" class="tab active">
  <div class="card"><h2>Device</h2><div id="statusBox" class="muted">Loading...</div></div>
  <div class="card"><h2>Tickers</h2><div id="tickBox" class="muted">-</div>
   <button class="btn sec" style="margin-top:10px" onclick="refreshNow()">Refresh data now</button></div>
 </section>

 <!-- WIFI -->
 <section id="wifi" class="tab">
  <div class="card"><h2>Saved networks</h2>
   <button class="btn sec" onclick="scan()">Scan networks</button>
   <div id="scanList"></div>
   <table id="wifiTable"></table>
   <button class="btn sec" style="margin-top:10px" onclick="addWifi()">+ Add network</button>
   <div style="margin-top:14px"><button class="btn" onclick="saveWifi()">Save &amp; connect (reboots)</button></div>
   <small class="hint">2.4&nbsp;GHz only. Up to 4 networks; at boot the device joins the strongest one it can see. Tap a scan result to fill a row. Leave a password blank to keep the stored one.</small>
  </div>
  <div class="card"><h2>Device name</h2>
   <label>Hostname</label><input id="hostname" type="text" placeholder="smalltv">
   <small class="hint">Reachable as <code>http://&lt;hostname&gt;.local</code> via mDNS. Running several SmallTVs? Give each its own name (<code>smalltv-desk</code>, <code>smalltv-shelf</code>) so browsers and the clawdmeter daemon's <code>--push-to</code> reach the right device. Saving a new name reboots the device.</small>
  </div>
  <div class="card"><h2>Setup hotspot (AP)</h2>
   <label>AP name</label><input id="apSsid" type="text">
   <label>AP password <span class="muted">(blank = open, else min 8 chars)</span></label>
   <input id="apPass" type="text" placeholder="(unchanged)">
   <small class="hint">The AP appears when no WiFi is configured or the connection fails.</small>
  </div>
 </section>

 <!-- DISPLAY (shared) -->
 <section id="display" class="tab">
  <div class="card"><h2>Mode</h2>
   <label>What this device shows</label>
   <select id="mode" onchange="modeChanged()">
    <option value="stocks">Stock / crypto ticker</option>
    <option value="usage">Claude usage</option>
    <option value="radar">Plane radar</option>
    <option value="carousel">Carousel (rotate modes)</option>
   </select>
   <div id="carouselRow">
    <label>Switch mode every (s)</label><input id="carouselSec" type="number" min="5" max="3600">
    <div class="chk"><input id="carouselTicker" type="checkbox"><label>Ticker</label></div>
    <div class="chk"><input id="carouselUsage" type="checkbox"><label>Claude usage</label></div>
    <div class="chk"><input id="carouselRadar" type="checkbox"><label>Plane radar</label></div>
   </div>
   <small class="hint">Pick the active feature, then configure it in its own tab. Carousel rotates through the ticked features.</small>
  </div>
  <div class="card"><h2>Screen</h2>
   <label>Brightness: <span id="brVal"></span>%</label>
   <input id="brightness" type="range" min="0" max="100" oninput="brVal.textContent=this.value">
   <div class="chk"><input id="autoBrightness" type="checkbox"><label>Auto-brightness (light sensor on A0)</label></div>
   <label>Orientation</label>
   <select id="rotation"><option value="0">0&deg;</option><option value="1">90&deg;</option>
    <option value="2">180&deg;</option><option value="3">270&deg;</option></select>
   <div class="chk"><input id="backlightInverted" type="checkbox"><label>Backlight is active-low (try if screen stays dark)</label></div>
  </div>
  <div class="card"><h2>Clock &amp; night mode</h2>
   <label>Timezone</label>
   <select id="tz"></select>
   <div class="muted" id="clockNow" style="margin:8px 0">Clock: -</div>
   <div class="chk"><input id="nightEnabled" type="checkbox"><label>Dim or blank the screen on a nightly schedule</label></div>
   <div class="row">
    <div><label>From</label><input id="nightStart" type="time"></div>
    <div><label>To</label><input id="nightEnd" type="time"></div>
   </div>
   <label>Night brightness: <span id="nlVal"></span>% <span class="muted">(0 = screen off)</span></label>
   <input id="nightLevel" type="range" min="0" max="100" oninput="nlVal.textContent=this.value">
   <small class="hint">Needs internet once to set the clock over NTP (no on-screen clock, this just drives the schedule). While the window is active it overrides the brightness and auto-brightness above. Times are local to the selected timezone; DST is handled automatically. After a reboot the schedule resumes once the clock re-syncs, so the screen may show normal brightness for a few seconds.</small>
  </div>
 </section>

 <!-- TICKER (feature) -->
 <section id="ticker" class="tab">
  <div class="card"><h2>Rotation &amp; data</h2>
   <div class="row">
    <div><label>Show each ticker (s)</label><input id="rotateSec" type="number" min="2" max="300"></div>
    <div><label>Refresh data (s)</label><input id="pollSec" type="number" min="10" max="3600"></div>
   </div>
   <div class="row">
    <div><label>Chart timeframe</label>
     <select id="range">
      <option value="1d">1 day</option><option value="5d">5 days</option>
      <option value="1mo">1 month</option><option value="3mo">3 months</option>
      <option value="6mo">6 months</option><option value="ytd">Year to date</option>
      <option value="1y">1 year</option><option value="2y">2 years</option>
      <option value="5y">5 years</option><option value="max">Max</option>
     </select></div>
    <div><label>Chart points</label><input id="points" type="number" min="0" max="60"></div>
   </div>
   <div class="row">
    <div><label>Change &amp; % basis</label>
     <select id="changeOnRange">
      <option value="true">Chart timeframe</option>
      <option value="false">1 day</option>
     </select></div>
   </div>
   <small class="hint">Chart timeframe: the change, arrow, colors, and chart cover the same span, so they agree. Needs chart data (2+ points); without it the device falls back to the 1-day change. At 1 day it measures from the session's first data point, so overnight gaps are not counted. 1 day: the classic change vs the previous close, which can point the other way than a longer chart.</small>
   <label>Webhook URL <span class="muted">(only for tickers set to Webhook)</span></label>
   <input id="webhookUrl" type="url" placeholder="http://n8n.local:5678/webhook/stock">
  </div>
  <div class="card"><h2>Color scheme</h2>
   <select id="colorInverted"><option value="false">Green up / Red down</option>
    <option value="true">Red up / Green down</option></select>
  </div>
  <div class="card"><h2>What to show</h2>
   <div class="chk"><input id="showName" type="checkbox"><label>Name / symbol</label></div>
   <div class="chk"><input id="showPrice" type="checkbox"><label>Price</label></div>
   <div class="chk"><input id="showChange" type="checkbox"><label>Change &amp; % change</label></div>
   <div class="chk"><input id="showChart" type="checkbox"><label>Sparkline chart</label></div>
   <div class="chk"><input id="showRangeLabel" type="checkbox"><label>Timeframe label</label></div>
   <div class="chk"><input id="showUpdatedAgo" type="checkbox"><label>"Updated N s ago"</label></div>
   <div class="chk"><input id="showPageDots" type="checkbox"><label>Rotation dots</label></div>
   <div class="chk"><input id="showPortfolio" type="checkbox"><label>Position P/L &amp; portfolio page</label></div>
  </div>
  <div class="card"><h2>Tickers (rotate on screen)</h2>
   <table id="symTable"></table>
   <button class="btn sec" style="margin-top:10px" onclick="addSym()">+ Add ticker</button>
   <small class="hint" id="symHint"></small>
  </div>
  <div class="card"><h2>cash.ch symbol finder</h2>
   <label>Instrument <span class="muted">(paste a cash.ch link, ISIN, valor, or a name)</span></label>
   <div class="row">
    <input id="cashQ" type="text" placeholder="https://www.cash.ch/... or EU0009654078">
    <button class="btn sec" style="flex:0 0 auto" onclick="cashFind()">Find</button>
   </div>
   <div id="cashRes"></div>
   <small class="hint">Searches cash.ch from your browser and turns the result into the listing key the ticker needs. Click a match to add it as a ticker.</small>
  </div>
 </section>

 <!-- USAGE (feature) -->
 <section id="usage" class="tab">
  <div class="card"><h2>Claude usage</h2>
   <label>Usage daemon URL</label>
   <input id="usageUrl" type="url" placeholder="http://192.168.1.10:8787/">
   <label>Refresh data (s)</label><input id="usagePollSec" type="number" min="10" max="3600">
   <small class="hint">Runs on the PC-side <a href="https://github.com/giovi321/clawdmeter-daemon" target="_blank">clawdmeter-daemon</a>, which reads your Claude usage and sends it here. <b>Pull:</b> set the Usage URL to the daemon. <b>Push:</b> leave it blank and run the daemon with <code>--push-to &lt;hostname&gt;.local</code> (for networks where the device cannot reach the PC). Running several SmallTVs? Give each a unique hostname in the WiFi tab so every PC pushes to its own device. Idle animation plays until data arrives.</small>
  </div>
 </section>

 <!-- RADAR (feature) -->
 <section id="radar" class="tab">
  <div class="card"><h2>Home location</h2>
   <div class="row">
    <div><label>Latitude</label><input id="radarLat" type="number" step="0.0001" placeholder="52.3676"></div>
    <div><label>Longitude</label><input id="radarLon" type="number" step="0.0001" placeholder="4.9041"></div>
   </div>
   <small class="hint">The radar centres on this point. Decimal degrees, e.g. <code>52.3676</code>, <code>4.9041</code>. Leave at 0/0 and the screen prompts you to set it.</small>
  </div>
  <div class="card"><h2>Range &amp; data</h2>
   <div class="row">
    <div><label>Range</label>
     <select id="rangeKm"><option value="5">5</option><option value="10">10</option>
      <option value="15">15</option><option value="25">25</option><option value="50">50</option></select></div>
    <div><label>Units</label>
     <select id="unitsMi"><option value="false">km</option><option value="true">mi</option></select></div>
    <div><label>Refresh (s)</label><input id="radarPollSec" type="number" min="3" max="3600"></div>
   </div>
   <label>Data source</label>
   <select id="radarSource" onchange="radarSrcChanged()">
    <option value="direct">adsb.fi (direct, no server)</option>
    <option value="webhook">Custom webhook (LAN proxy)</option>
   </select>
   <div id="radarWebhookRow"><label>Webhook URL</label>
    <input id="radarWebhookUrl" type="url" placeholder="http://n8n.local:5678/webhook/radar"></div>
   <small class="hint" id="radarSrcHint"></small>
  </div>
  <div class="card"><h2>What to show</h2>
   <label>Marker &amp; label size</label>
   <select id="radarUiScale"><option value="0">Small</option><option value="1">Medium</option><option value="2">Large</option></select>
   <label style="margin-top:12px">Hide aircraft below (ft, 0 = show all)</label>
   <input id="radarMinAlt" type="number" min="0" max="60000" step="100">
   <small class="hint">Drops ground traffic (parked/taxiing) and low flights. Try <code>500</code> to hide anything on or near the ground.</small>
   <div class="chk" style="margin-top:12px"><input id="showLabels" type="checkbox"><label>Callsign &amp; altitude labels</label></div>
   <div class="chk"><input id="showVectors" type="checkbox"><label>Speed / heading vectors</label></div>
   <div class="chk"><input id="showRimDots" type="checkbox"><label>Off-screen traffic dots on the rim</label></div>
  </div>
  <div class="card"><h2>Airports</h2>
   <table id="apTable"></table>
   <button class="btn sec" style="margin-top:10px" onclick="addAp()">+ Add airport</button>
   <small class="hint">A few home-area airports drawn as markers. ICAO code (e.g. <code>LSZH</code>) and its lat/lon. Up to 6.</small>
  </div>
 </section>

 <!-- UPDATE -->
 <section id="update" class="tab">
  <div class="card"><h2>Update from GitHub</h2>
   <div class="muted">Installed: <b id="fwVer">-</b></div>
   <div style="margin-top:10px">
    <button class="btn sec" onclick="checkUpdate()" id="chkBtn">Check for latest</button>
    <button class="btn" style="margin-left:8px" onclick="selfUpdate()" id="ghUpBtn" disabled>Update now</button>
   </div>
   <div id="ghMsg" class="muted" style="margin-top:8px"></div>
   <small class="hint">Pulls the newest release straight from <a id="repoLink" href="https://github.com/giovi321/smalltv-mod/releases" target="_blank">the GitHub repo</a>. HTTPS OTA is tight on the ESP8266; if it fails, use the manual upload below.</small>
  </div>
  <div class="card"><h2>Manual update (OTA)</h2>
   <input id="fw" type="file" accept=".bin">
   <div style="margin-top:12px"><button class="btn" onclick="upload()" id="upBtn">Upload &amp; flash</button></div>
   <div class="bar"><div id="upBar"></div></div>
   <div id="upMsg" class="muted" style="margin-top:8px"></div>
   <small class="hint">Upload a firmware.bin from the <a href="https://github.com/giovi321/smalltv-mod/releases" target="_blank">releases page</a> or a local build. The device reboots when done.</small>
  </div>
  <div class="card"><h2>Settings backup</h2>
   <button class="btn sec" onclick="location.href='/api/export'">Export settings</button>
   <input id="cfgFile" type="file" accept=".json,application/json" style="margin-top:10px">
   <div style="margin-top:10px"><button class="btn" onclick="importCfg()">Import &amp; reboot</button></div>
   <small class="hint">The export is the device's <code>config.json</code>, including WiFi passwords in clear text; treat the file accordingly. Import applies everything and reboots.</small>
  </div>
  <div class="card"><h2>Maintenance</h2>
   <button class="btn sec" onclick="reboot()">Reboot</button>
   <button class="btn danger" style="margin-left:8px" onclick="factory()">Factory reset</button>
  </div>
 </section>
</main>

<div style="text-align:center;padding:0 0 16px"><button class="btn" onclick="saveAll()">Save settings</button></div>
<div style="text-align:center;padding:0 0 24px;font-size:12px">
 <a id="footRepo" href="https://github.com/giovi321/smalltv-mod" target="_blank" style="color:var(--acc2);text-decoration:none">GitHub: giovi321/smalltv-mod</a>
 <span id="footVer" class="muted"></span>
</div>
<div id="toast" class="toast"></div>

<script>
var C={};
function $(id){return document.getElementById(id)}
// null-safe field helpers: a lean build removes some feature tabs entirely
function sv(id,v){var e=$(id);if(e)e.value=(v!=null?v:'')}
function sc(id,v){var e=$(id);if(e)e.checked=!!v}
function gv(id){var e=$(id);return e?e.value:''}
function gc(id){var e=$(id);return e?e.checked:false}
function toast(m){var t=$('toast');t.textContent=m;t.classList.add('show');setTimeout(function(){t.classList.remove('show')},2200)}
function j(url,opt){return fetch(url,opt).then(function(r){return r.json()})}

// tabs
document.querySelectorAll('nav button').forEach(function(b){b.onclick=function(){
 document.querySelectorAll('nav button').forEach(function(x){x.classList.remove('active')});
 document.querySelectorAll('.tab').forEach(function(x){x.classList.remove('active')});
 b.classList.add('active');$(b.dataset.t).classList.add('active');
}});

// field groups by their location in the nested config
var T_TEXT=['webhookUrl','range'];                   // ticker strings
var T_NUM=['rotateSec','pollSec','points'];          // ticker numbers
var T_BOOL=['showName','showPrice','showChange','showChart','showRangeLabel','showUpdatedAgo','showPageDots','showPortfolio'];

// IANA -> POSIX TZ. The device stores/uses the POSIX rule; this map lives in the
// browser so the firmware carries no tz database (same idea as the cash finder).
var TZMAP={
 '':'UTC0','UTC':'UTC0',
 'Europe/London':'GMT0BST,M3.5.0/1,M10.5.0','Europe/Dublin':'GMT0IST,M3.5.0/1,M10.5.0',
 'Europe/Lisbon':'WET0WEST,M3.5.0/1,M10.5.0',
 'Europe/Rome':'CET-1CEST,M3.5.0,M10.5.0/3','Europe/Paris':'CET-1CEST,M3.5.0,M10.5.0/3',
 'Europe/Berlin':'CET-1CEST,M3.5.0,M10.5.0/3','Europe/Madrid':'CET-1CEST,M3.5.0,M10.5.0/3',
 'Europe/Amsterdam':'CET-1CEST,M3.5.0,M10.5.0/3','Europe/Brussels':'CET-1CEST,M3.5.0,M10.5.0/3',
 'Europe/Zurich':'CET-1CEST,M3.5.0,M10.5.0/3','Europe/Vienna':'CET-1CEST,M3.5.0,M10.5.0/3',
 'Europe/Warsaw':'CET-1CEST,M3.5.0,M10.5.0/3','Europe/Prague':'CET-1CEST,M3.5.0,M10.5.0/3',
 'Europe/Stockholm':'CET-1CEST,M3.5.0,M10.5.0/3','Europe/Oslo':'CET-1CEST,M3.5.0,M10.5.0/3',
 'Europe/Copenhagen':'CET-1CEST,M3.5.0,M10.5.0/3',
 'Europe/Athens':'EET-2EEST,M3.5.0/3,M10.5.0/4','Europe/Helsinki':'EET-2EEST,M3.5.0/3,M10.5.0/4',
 'Europe/Bucharest':'EET-2EEST,M3.5.0/3,M10.5.0/4','Europe/Kyiv':'EET-2EEST,M3.5.0/3,M10.5.0/4',
 'Europe/Istanbul':'<+03>-3','Europe/Moscow':'MSK-3',
 'America/New_York':'EST5EDT,M3.2.0,M11.1.0','America/Toronto':'EST5EDT,M3.2.0,M11.1.0',
 'America/Chicago':'CST6CDT,M3.2.0,M11.1.0','America/Denver':'MST7MDT,M3.2.0,M11.1.0',
 'America/Phoenix':'MST7','America/Los_Angeles':'PST8PDT,M3.2.0,M11.1.0',
 'America/Anchorage':'AKST9AKDT,M3.2.0,M11.1.0','America/Sao_Paulo':'<-03>3',
 'America/Mexico_City':'CST6','America/Bogota':'<-05>5','America/Argentina/Buenos_Aires':'<-03>3',
 'Asia/Dubai':'<+04>-4','Asia/Karachi':'PKT-5','Asia/Kolkata':'IST-5:30',
 'Asia/Dhaka':'<+06>-6','Asia/Bangkok':'<+07>-7','Asia/Jakarta':'WIB-7',
 'Asia/Shanghai':'CST-8','Asia/Hong_Kong':'HKT-8','Asia/Singapore':'<+08>-8',
 'Asia/Taipei':'CST-8','Asia/Tokyo':'JST-9','Asia/Seoul':'KST-9',
 'Australia/Perth':'AWST-8','Australia/Sydney':'AEST-10AEDT,M10.1.0,M4.1.0/3',
 'Australia/Adelaide':'ACST-9:30ACDT,M10.1.0,M4.1.0/3','Australia/Brisbane':'AEST-10',
 'Pacific/Auckland':'NZST-12NZDT,M9.5.0,M4.1.0/3','Pacific/Honolulu':'HST10'};
function fillTz(){var s=$('tz');if(!s)return;var keys=Object.keys(TZMAP).filter(function(k){return k!==''});
 keys.sort();s.innerHTML='<option value="">UTC</option>'+keys.map(function(k){return '<option value="'+k+'">'+k+'</option>'}).join('');}

var MODEOPT={ticker:'stocks',usage:'usage',radar:'radar'};
var CAROPT={ticker:'carouselTicker',usage:'carouselUsage',radar:'carouselRadar'};
function hideFeat(name){
 var b=document.querySelector('nav button[data-t="'+name+'"]'); if(b)b.remove();
 var sec=$(name); if(sec)sec.remove();
 var o=document.querySelector('#mode option[value="'+MODEOPT[name]+'"]'); if(o)o.remove();
 var c=$(CAROPT[name]); if(c)c.closest('.chk').remove();
}
function modeChanged(){if(!$('mode'))return;
 $('carouselRow').style.display=$('mode').value==='carousel'?'block':'none';}
function loadConfig(){return j('/api/config').then(function(c){C=c;
 var f=c.features||{}; ['ticker','usage','radar'].forEach(function(k){if(f[k]===false)hideFeat(k)});
 var t=c.ticker||{}, u=c.usage||{};
 // shared
 ['apSsid','apPass','hostname'].forEach(function(k){$(k).value=c[k]!=null?c[k]:''});
 renderWifi(c.wifi||(c.staSsid?[{ssid:c.staSsid,passSet:c.staPassSet}]:[]));
 $('brightness').value=c.brightness; $('brVal').textContent=c.brightness;
 $('rotation').value=c.rotation;
 $('autoBrightness').checked=!!c.autoBrightness;
 $('backlightInverted').checked=!!c.backlightInverted;
 // header chip = which chip this firmware was built for
 var chipName={esp8266:'ESP8266',esp32c2:'ESP32-C2',esp32:'ESP32'}[c.chip]||'';
 var chE=$('chip'); if(chE&&chipName){chE.textContent=chipName;chE.style.display='inline-block';}
 // clock slice
 fillTz(); var ck=c.clock||{};
 if(ck.tz && !(ck.tz in TZMAP)){var _ts=$('tz'); if(_ts){var _o=document.createElement('option');_o.value=ck.tz;_o.textContent=ck.tz;_ts.appendChild(_o);}}
 sv('tz',ck.tz||''); sc('nightEnabled',!!ck.nightEnabled);
 sv('nightStart',ck.nightStart||'22:00'); sv('nightEnd',ck.nightEnd||'07:00');
 sv('nightLevel',ck.nightLevel!=null?ck.nightLevel:0); $('nlVal')&&($('nlVal').textContent=(ck.nightLevel!=null?ck.nightLevel:0));
 $('mode').value=c.mode||'stocks'; modeChanged();
 sv('carouselSec',c.carouselSec||30);
 sc('carouselTicker',c.carouselTicker!==false); sc('carouselUsage',c.carouselUsage!==false); sc('carouselRadar',c.carouselRadar!==false);
 // ticker slice
 T_TEXT.forEach(function(k){sv(k,t[k])});
 T_NUM.forEach(function(k){sv(k,t[k])});
 T_BOOL.forEach(function(k){sc(k,t[k])});
 sv('colorInverted',t.colorInverted?'true':'false');
 sv('changeOnRange',t.changeOnRange===false?'false':'true');
 renderSyms(t.symbols||[]); symHintFor('yahoo');
 // usage slice
 sv('usageUrl',u.usageUrl);
 sv('usagePollSec',u.pollSec);
 // radar slice
 var r=c.radar||{};
 sv('radarLat',r.lat); sv('radarLon',r.lon);
 sv('rangeKm',r.rangeKm||20);
 sv('unitsMi',r.unitsMi?'true':'false');
 sv('radarPollSec',r.pollSec);
 sv('radarSource',r.source||'direct'); radarSrcChanged();
 sv('radarWebhookUrl',r.webhookUrl);
 sc('showLabels',r.showLabels); sc('showVectors',r.showVectors); sc('showRimDots',r.showRimDots);
 sv('radarUiScale',r.uiScale!=null?r.uiScale:1);
 sv('radarMinAlt',r.minAltFt!=null?r.minAltFt:0);
 renderAps(r.airports||[]);
 var ap=$('apPass'); if(ap)ap.placeholder=c.apPassSet?'(unchanged)':'(open)';
})}

function esc(s){return (''+(s==null?'':s)).replace(/[<>&"]/g,function(c){return {'<':'&lt;','>':'&gt;','&':'&amp;','"':'&quot;'}[c]})}
function symHintFor(v){var h=$('symHint');if(!h)return;
 h.innerHTML=(v==='cash'
  ?'<b>cash.ch</b>: fetched directly by the device. The symbol is a listing key like <code>147478611-246-333</code>; the finder below turns a cash.ch link, ISIN, or name into one.'
   +(C.chip==='esp8266'?' <b>On this ESP8266</b>, cash.ch\'s TLS is beyond this chip &mdash; use the <b>GitHub</b> source for the same listing key instead (a scheduled workflow publishes it). The ESP32 boards fetch cash.ch directly.':'')
  :v==='github'
  ?'<b>GitHub</b>: reads a listing key\'s quote from a small JSON file the repo\'s <code>quotes</code> workflow publishes (a proxy for cash.ch on chips that can\'t reach it directly). The symbol is the cash.ch listing key, and it must be listed in <code>quotes-config.json</code>.'
  :v==='webhook'
  ?'<b>Webhook</b>: the device asks the webhook URL above and passes the symbol through as-is, so use whatever your endpoint understands.'
  :'<b>Yahoo Finance</b>: fetched directly by the device. Use Yahoo symbols: <code>AAPL</code>, <code>NESN.SW</code> (Swiss stocks end in <code>.SW</code>), <code>BTC-USD</code>, <code>EURUSD=X</code>.')
  +' Name is optional; if set it overrides the source\'s name. Qty and per-unit cost are optional too: set both and the ticker shows your P/L plus a portfolio summary page.';}

// cash.ch symbol finder: runs in YOUR browser (cash.ch answers cross-origin),
// the device itself is not involved in the search.
function cashFind(){var q=gv('cashQ').trim();if(!q){toast('Paste a link, ISIN, or name first');return}
 var m=q.match(/^https?:\/\/\S*?(\d{5,12})/); if(m)q=m[1];   // a cash.ch link carries the valor in its slug
 $('cashRes').innerHTML='<div class="muted">Searching cash.ch...</div>';
 var gq='query{textSearch(publication:CASH,search:"'+q.replace(/["\\]/g,'')+'",sort:Relevance,sortOrder:Descending,limit:10,offset:0){'+
  'equity{items{...on Equity{listingId mName market mCur mIsin}}} fund{items{...on Fund{listingId mName market mCur mIsin}}} '+
  'derivative{items{...on Derivative{listingId mName market mCur mIsin}}} bond{items{...on Bond{listingId mName market mCur mIsin}}} '+
  'index{items{...on Index{listingId mName market mCur}}} diverse{items{...on Diverse{listingId mName market mCur mIsin}}} '+
  'cryptoCurrency{items{...on CryptoCurrency{listingId mName market mCur}}}}}';
 fetch('https://www.cash.ch/_/api/graphql/prod?query='+encodeURIComponent(gq))
 .then(function(r){return r.json()})
 .then(function(d){var out=[];var b=(d.data&&d.data.textSearch)||{};
  ['equity','derivative','fund','bond','index','diverse','cryptoCurrency'].forEach(function(k){
   ((b[k]&&b[k].items)||[]).forEach(function(it){if(it&&it.listingId)out.push(it)});});
  if(!out.length){$('cashRes').innerHTML='<div class="muted">Nothing found on cash.ch</div>';return}
  var h='';out.slice(0,10).forEach(function(it){
   h+='<div class="net" onclick="cashPick(this.dataset.k)" data-k="'+esc(it.listingId)+'"><span>'+esc(it.mName||'?')+
    ' <span class="muted">'+esc(it.mIsin||'')+'</span></span><span class="muted">'+esc(it.market||'')+' '+esc(it.mCur||'')+'</span></div>';});
  $('cashRes').innerHTML=h;
 }).catch(function(){$('cashRes').innerHTML='<div class="muted">cash.ch not reachable from this browser</div>'});}
function cashPick(k){var rows=document.querySelectorAll('#symTable tr');var tr=null;
 for(var i=0;i<rows.length;i++){if(!rows[i].querySelector('.s').value.trim()){tr=rows[i];break}}
 if(!tr){if(rows.length>=8){toast('Max 8');return}addRow({});tr=$('symTable').lastChild}
 tr.querySelector('.s').value=k;tr.querySelector('.src').value='cash';symHintFor('cash');
 toast('Added '+k+'. Set a name, then Save.');}
function radarSrcChanged(){if(!$('radarSource'))return;var d=$('radarSource').value!=='webhook';
 $('radarWebhookRow').style.display=d?'none':'block';
 $('radarSrcHint').innerHTML=d
  ?'The device fetches <b>adsb.fi</b> directly over HTTPS (no key, ~1 req/s). Tight on RAM in busy airspace — use the webhook if it drops.'
  :'The device requests <code>?lat=..&amp;lon=..&amp;dist=..</code> from your LAN proxy, which pre-filters adsb.fi to a small JSON. Most reliable on the ESP8266.';}

function collect(){
 var o={mode:gv('mode'),
  carouselSec:parseInt(gv('carouselSec'))||30,
  carouselTicker:gc('carouselTicker'), carouselUsage:gc('carouselUsage'), carouselRadar:gc('carouselRadar'),
  brightness:parseInt(gv('brightness'))||0,
  rotation:parseInt(gv('rotation')),
  autoBrightness:gc('autoBrightness'),
  backlightInverted:gc('backlightInverted'),
  hostname:gv('hostname'), apSsid:gv('apSsid'), apPass:gv('apPass'),
  wifi:collectWifi()};
 // ticker slice (only if compiled in)
 if($('ticker')){
  var t={colorInverted:gv('colorInverted')==='true',changeOnRange:gv('changeOnRange')==='true'};
  T_TEXT.forEach(function(k){t[k]=gv(k)});
  T_NUM.forEach(function(k){t[k]=parseInt(gv(k))||0});
  T_BOOL.forEach(function(k){t[k]=gc(k)});
  t.symbols=[];
  document.querySelectorAll('#symTable tr').forEach(function(tr){
   var s=tr.querySelector('.s').value.trim();
   if(s)t.symbols.push({symbol:s,name:tr.querySelector('.n').value.trim(),source:tr.querySelector('.src').value,
    qty:parseFloat(tr.querySelector('.q').value)||0,cost:parseFloat(tr.querySelector('.c').value)||0});
  });
  o.ticker=t;
 }
 // usage slice
 if($('usage')) o.usage={usageUrl:gv('usageUrl'), pollSec:parseInt(gv('usagePollSec'))||0};
 // clock slice
 if($('tz')){var _tzn=gv('tz'); var _tzp=(_tzn in TZMAP)?TZMAP[_tzn]:((C.clock&&C.clock.tz===_tzn&&C.clock.tzPosix)?C.clock.tzPosix:'UTC0');
  o.clock={tz:_tzn,tzPosix:_tzp,
  nightEnabled:gc('nightEnabled'),nightStart:gv('nightStart')||'22:00',
  nightEnd:gv('nightEnd')||'07:00',nightLevel:parseInt(gv('nightLevel'))||0};}
 // radar slice
 if($('radar')){
  var r={lat:parseFloat(gv('radarLat'))||0, lon:parseFloat(gv('radarLon'))||0,
   rangeKm:parseInt(gv('rangeKm'))||20, unitsMi:gv('unitsMi')==='true',
   pollSec:parseInt(gv('radarPollSec'))||0, source:gv('radarSource'),
   webhookUrl:gv('radarWebhookUrl'),
   showLabels:gc('showLabels'), showVectors:gc('showVectors'), showRimDots:gc('showRimDots'),
   uiScale:parseInt(gv('radarUiScale'))||0, minAltFt:parseInt(gv('radarMinAlt'))||0};
  r.airports=[];
  document.querySelectorAll('#apTable tr').forEach(function(tr){
   var ic=tr.querySelector('.ai').value.trim();
   if(ic)r.airports.push({icao:ic,lat:parseFloat(tr.querySelector('.ala').value)||0,lon:parseFloat(tr.querySelector('.alo').value)||0});
  });
  o.radar=r;
 }
 return o;
}
function saveAll(){j('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(collect())})
 .then(function(r){toast(r.reboot?'Saved — rebooting...':'Saved');if(r.reboot)setTimeout(function(){location.reload()},6000);loadStatus()})}

function saveWifi(){
 var o={wifi:collectWifi()};
 j('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(o)}).then(function(){
  toast('Saved, rebooting to connect...');j('/api/reboot',{method:'POST'});
 });
}

// wifi networks (up to 4)
function renderWifi(arr){var t=$('wifiTable');if(!t)return;t.innerHTML='';arr.forEach(addWifiRow);if(!arr.length)addWifiRow({})}
function addWifiRow(o){var t=$('wifiTable');var tr=document.createElement('tr');tr.className='symrow';
 tr.innerHTML='<td style="width:44%"><input class="ws" type="text" autocomplete="off" placeholder="SSID" value="'+esc(o.ssid||'')+'"></td>'+
  '<td><input class="wp" type="password" autocomplete="off" placeholder="'+(o.passSet?'(unchanged)':'password')+'"></td>'+
  '<td style="width:34px"><button class="btn sec" style="padding:6px 10px" onclick="this.closest(\'tr\').remove()">&times;</button></td>';
 t.appendChild(tr);}
function addWifi(){if(document.querySelectorAll('#wifiTable tr').length>=4){toast('Max 4');return}addWifiRow({})}
function collectWifi(){var w=[];document.querySelectorAll('#wifiTable tr').forEach(function(tr){
 var s=tr.querySelector('.ws').value.trim();if(!s)return;
 var e={ssid:s};var p=tr.querySelector('.wp').value;if(p)e.pass=p;w.push(e);});return w}
function scanPick(ssid){var rows=document.querySelectorAll('#wifiTable tr');var tr=null;
 for(var i=0;i<rows.length;i++){if(!rows[i].querySelector('.ws').value.trim()){tr=rows[i];break}}
 if(!tr){if(rows.length>=4){toast('Max 4');return}addWifiRow({});tr=$('wifiTable').lastChild}
 tr.querySelector('.ws').value=ssid;tr.querySelector('.wp').focus();}

// symbols
function renderSyms(arr){var t=$('symTable');if(!t)return;t.innerHTML='';arr.forEach(addRow);if(!arr.length)addRow({})}
function addRow(o){var t=$('symTable');var tr=document.createElement('tr');tr.className='symrow';
 tr.innerHTML='<td style="width:24%"><input class="s" type="text" placeholder="AAPL" value="'+esc(o.symbol||'')+'"></td>'+
  '<td><input class="n" type="text" placeholder="name" value="'+esc(o.name||'')+'"></td>'+
  '<td style="width:118px"><select class="src" onchange="symHintFor(this.value)">'+
   '<option value="yahoo">Yahoo Finance</option><option value="cash">cash.ch</option><option value="github">GitHub</option><option value="webhook">Webhook</option></select></td>'+
  '<td style="width:58px"><input class="q" type="number" step="any" min="0" placeholder="qty" value="'+(o.qty>0?o.qty:'')+'"></td>'+
  '<td style="width:70px"><input class="c" type="number" step="any" min="0" placeholder="cost" value="'+(o.cost>0?o.cost:'')+'"></td>'+
  '<td style="width:34px"><button class="btn sec" style="padding:6px 10px" onclick="this.closest(\'tr\').remove()">&times;</button></td>';
 tr.querySelector('.src').value=o.source||'yahoo';
 t.appendChild(tr);}
function addSym(){if(document.querySelectorAll('#symTable tr').length>=8){toast('Max 8');return}addRow({})}

// airports
function renderAps(arr){var t=$('apTable');if(!t)return;t.innerHTML='';arr.forEach(addApRow);if(!arr.length)addApRow({})}
function addApRow(o){var t=$('apTable');var tr=document.createElement('tr');tr.className='symrow';
 tr.innerHTML='<td style="width:30%"><input class="ai" type="text" placeholder="LSZH" value="'+(o.icao||'')+'"></td>'+
  '<td><input class="ala" type="number" step="0.0001" placeholder="lat" value="'+(o.lat!=null?o.lat:'')+'"></td>'+
  '<td><input class="alo" type="number" step="0.0001" placeholder="lon" value="'+(o.lon!=null?o.lon:'')+'"></td>'+
  '<td style="width:34px"><button class="btn sec" style="padding:6px 10px" onclick="this.closest(\'tr\').remove()">&times;</button></td>';
 t.appendChild(tr);}
function addAp(){if(document.querySelectorAll('#apTable tr').length>=6){toast('Max 6');return}addApRow({})}

// wifi scan
function scan(){$('scanList').innerHTML='<div class="muted">Scanning...</div>';
 j('/api/scan').then(function(l){var h='';l.sort(function(a,b){return b.rssi-a.rssi});
  l.forEach(function(n){h+='<div class="net" onclick="scanPick(this.dataset.s)" data-s="'+
   esc(n.ssid)+'"><span>'+(n.enc?'🔒 ':'')+esc(n.ssid)+'</span><span class="muted">'+n.rssi+' dBm</span></div>'});
  $('scanList').innerHTML=h||'<div class="muted">No networks found</div>';})}

// status
function loadStatus(){j('/api/status').then(function(s){
 $('dot').className='dot'+(s.connected?' ok':'');
 $('hi').textContent=s.mode==='ap'?'setup mode':(s.ip||'');
 var cn=$('clockNow'); if(cn){var ne=!!(C.clock&&C.clock.nightEnabled);var ns=s.night?'  · night mode active':(s.nightHeld?'  · night mode waiting for NTP':'');cn.textContent=!ne?'Clock: NTP runs only when night mode is on':('Clock: '+(s.synced?(s.time||'synced')+(s.tz?' ('+s.tz+')':''):'waiting for NTP...')+ns);}
 var fw=$('fwVer'); if(fw)fw.textContent=s.fw+' '+s.version;
 // Surface the result of a boot-time GitHub update (ESP8266) once on first load,
 // so a failure that happened across the reboot is visible even if the original
 // Update tab was closed. Don't clobber an in-progress check/update message.
 if(!window._otaShown){window._otaShown=1;var gm=$('ghMsg');if(gm&&!gm.textContent&&s.updateMsg&&s.updateMsg!=='updating...')gm.textContent='Last update: '+s.updateMsg}
 var fv=$('footVer'); if(fv)fv.textContent=' v'+s.version;
 if(s.repo){var rl=$('repoLink'); if(rl)rl.href=s.repo+'/releases'; var fr=$('footRepo'); if(fr)fr.href=s.repo;}
 $('statusBox').innerHTML=
  kv('Firmware',s.fw+' '+s.version)+kv('Mode',s.mode.toUpperCase())+
  kv('Network',s.ssid||'-')+kv('IP',s.ip||'-')+kv('mDNS','http://'+(C.hostname||'smalltv')+'.local')+
  kv('Signal',s.rssi?s.rssi+' dBm':'-')+
  kv('Free heap',s.heap+' B')+kv('Uptime',fmtUp(s.uptime))+kv('Last reset',s.reset||'-');
 var h='';(s.tickers||[]).forEach(function(t){
  var c=t.error?'var(--red)':(t.valid?'var(--acc)':'var(--mut)');
  var pc=t.changePct!=null?(t.changePct>=0?'+':'')+t.changePct.toFixed(2)+'%':'';
  h+='<div class="kv"><b style="color:'+c+'">'+t.symbol+'</b><span>'+
   (t.valid?(t.price+'  '+pc):(t.error?'error':'...'))+'</span></div>';});
 $('tickBox').innerHTML=h||'<span class="muted">No tickers configured</span>';
})}
function kv(k,v){return '<div class="kv"><span class="muted">'+k+'</span><b>'+v+'</b></div>'}
function fmtUp(s){var d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);
 return (d?d+'d ':'')+(h?h+'h ':'')+m+'m'}
function refreshNow(){j('/api/refresh',{method:'POST'}).then(function(){toast('Refreshing...');setTimeout(loadStatus,1500)})}

// GitHub self-update
function checkUpdate(){$('ghMsg').textContent='Checking GitHub...';$('chkBtn').disabled=true;
 j('/api/checkupdate').then(function(u){$('chkBtn').disabled=false;
  if(!u.ok){$('ghMsg').textContent='Check failed: '+(u.error||'unknown');return}
  if(u.newer){$('ghMsg').innerHTML='Version <b>'+u.latest+'</b> is available (installed '+u.current+').';$('ghUpBtn').disabled=false}
  else{$('ghMsg').textContent='Up to date ('+u.current+').';$('ghUpBtn').disabled=true}
 }).catch(function(){$('chkBtn').disabled=false;$('ghMsg').textContent='Check failed'})}
function selfUpdate(){if(!confirm('Download and flash the latest release from GitHub? The device reboots if it succeeds.'))return;
 $('ghUpBtn').disabled=true;$('chkBtn').disabled=true;
 $('ghMsg').textContent='Downloading and flashing... this can take a couple of minutes and the device may reboot twice.';
 // Installed version, read synchronously from the already-loaded status so the
 // poller below can recognise success (new version) without racing a fetch.
 var cur=(($('fwVer').textContent||'').trim().split(' ').pop())||'';
 j('/api/selfupdate',{method:'POST'}).then(function(){
  var n=0;var t=setInterval(function(){n++;
   j('/api/status').then(function(s){
    if(cur&&s.version&&s.version!==cur){clearInterval(t);$('ghMsg').textContent='Updated to '+s.version+'.';$('chkBtn').disabled=false;return}
    var m=s.updateMsg||'';
    if(m&&m!=='starting...'&&m!=='updating...'){clearInterval(t);$('ghMsg').textContent='Update failed: '+m;$('chkBtn').disabled=false}
   }).catch(function(){});
   if(n>100)clearInterval(t);
  },3000);
 }).catch(function(){$('ghMsg').textContent='Could not start update';$('chkBtn').disabled=false})}

// settings backup
function importCfg(){var f=$('cfgFile').files[0];if(!f){toast('Pick a config .json first');return}
 var r=new FileReader();
 r.onload=function(){var txt=r.result;
  try{JSON.parse(txt)}catch(e){toast('Not valid JSON');return}
  if(!confirm('Apply this configuration and reboot?'))return;
  j('/api/import',{method:'POST',headers:{'Content-Type':'application/json'},body:txt})
   .then(function(){toast('Imported, rebooting...');setTimeout(function(){location.reload()},8000)})
   .catch(function(){toast('Import failed')});
 };
 r.readAsText(f);}

// maintenance
function reboot(){if(confirm('Reboot device?'))j('/api/reboot',{method:'POST'}).then(function(){toast('Rebooting...')})}
function factory(){if(confirm('Erase ALL settings and reboot?'))j('/api/factory',{method:'POST'}).then(function(){toast('Reset, rebooting...')})}

// OTA
function upload(){var f=$('fw').files[0];if(!f){toast('Pick a .bin first');return}
 var fd=new FormData();fd.append('firmware',f,f.name);
 var x=new XMLHttpRequest();x.open('POST','/update');
 $('upBtn').disabled=true;
 x.upload.onprogress=function(e){if(e.lengthComputable){var p=Math.round(e.loaded/e.total*100);$('upBar').style.width=p+'%';$('upMsg').textContent='Uploading '+p+'%'}};
 x.onload=function(){$('upBtn').disabled=false;if(x.status==200){$('upMsg').textContent='Done. Rebooting...';$('upBar').style.width='100%';setTimeout(function(){location.reload()},9000)}else{$('upMsg').textContent='Failed: '+x.responseText}};
 x.onerror=function(){$('upBtn').disabled=false;$('upMsg').textContent='Upload error'};
 x.send(fd);
}

loadConfig().then(loadStatus);
setInterval(loadStatus,5000);
</script>
</body></html>)HTMLPAGE";
