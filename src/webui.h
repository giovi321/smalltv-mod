// webui.h — single-page config UI served from PROGMEM
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
</style></head>
<body>
<header><span id="dot" class="dot"></span><h1>SmallTV</h1><span id="hi" class="muted"></span></header>
<nav>
 <button data-t="status" class="active">Status</button>
 <button data-t="wifi">WiFi</button>
 <button data-t="display">Display</button>
 <button data-t="symbols" class="stockonly">Symbols</button>
 <button data-t="update">Update</button>
</nav>
<main>
 <!-- STATUS -->
 <section id="status" class="tab active">
  <div class="card"><h2>Device</h2><div id="statusBox" class="muted">Loading...</div></div>
  <div class="card stockonly"><h2>Tickers</h2><div id="tickBox" class="muted">-</div>
   <button class="btn sec" style="margin-top:10px" onclick="refreshNow()">Refresh data now</button></div>
 </section>

 <!-- WIFI -->
 <section id="wifi" class="tab">
  <div class="card"><h2>Connect to WiFi</h2>
   <button class="btn sec" onclick="scan()">Scan networks</button>
   <div id="scanList"></div>
   <label>Network name (SSID)</label><input id="staSsid" type="text" autocomplete="off">
   <label>Password <span class="muted">(leave blank to keep current)</span></label>
   <input id="staPass" type="password" autocomplete="off" placeholder="••••••••">
   <div style="margin-top:14px"><button class="btn" onclick="saveWifi()">Save &amp; connect (reboots)</button></div>
   <small class="hint">2.4&nbsp;GHz only. The device reboots and joins this network.</small>
  </div>
  <div class="card"><h2>Setup hotspot (AP)</h2>
   <label>AP name</label><input id="apSsid" type="text">
   <label>AP password <span class="muted">(blank = open, else min 8 chars)</span></label>
   <input id="apPass" type="text" placeholder="(unchanged)">
   <label>Hostname (http://name.local)</label><input id="hostname" type="text">
   <small class="hint">The AP appears when no WiFi is configured or the connection fails.</small>
  </div>
 </section>

 <!-- DISPLAY -->
 <section id="display" class="tab">
  <div class="card"><h2>Mode</h2>
   <label>What this device shows</label>
   <select id="mode" onchange="modeChanged()">
    <option value="stocks">Stock / crypto ticker</option>
    <option value="usage">Claude usage</option>
   </select>
   <div id="usageRow"><label>Usage daemon URL</label>
    <input id="usageUrl" type="url" placeholder="http://192.168.1.10:8787/"></div>
   <small class="hint" id="modeHint"></small>
  </div>
  <div class="card"><h2>Screen</h2>
   <label>Brightness: <span id="brVal"></span>%</label>
   <input id="brightness" type="range" min="0" max="100" oninput="brVal.textContent=this.value">
   <div class="chk"><input id="autoBrightness" type="checkbox"><label>Auto-brightness (light sensor on A0)</label></div>
   <label>Orientation</label>
   <select id="rotation"><option value="0">0&deg;</option><option value="1">90&deg;</option>
    <option value="2">180&deg;</option><option value="3">270&deg;</option></select>
   <div class="chk"><input id="backlightInverted" type="checkbox"><label>Backlight is active-low (try if screen stays dark)</label></div>
   <label>Color scheme</label>
   <select id="colorInverted"><option value="false">Green up / Red down</option>
    <option value="true">Red up / Green down</option></select>
  </div>
  <div class="card stockonly"><h2>Rotation &amp; data</h2>
   <div class="row">
    <div><label>Show each ticker (s)</label><input id="rotateSec" type="number" min="2" max="300"></div>
    <div><label>Refresh data (s)</label><input id="pollSec" type="number" min="10" max="3600"></div>
   </div>
   <label>Data source</label>
   <select id="source" onchange="srcChanged()">
    <option value="yahoo">Yahoo Finance (direct, no server)</option>
    <option value="webhook">Custom webhook (n8n / your own)</option>
   </select>
   <div id="webhookRow"><label>Webhook URL</label>
    <input id="webhookUrl" type="url" placeholder="http://n8n.local:5678/webhook/stock"></div>
   <div class="row">
    <div><label>Chart timeframe</label><input id="range" type="text" placeholder="1d"></div>
    <div><label>Chart points</label><input id="points" type="number" min="0" max="60"></div>
   </div>
   <small class="hint" id="srcHint"></small>
  </div>
  <div class="card stockonly"><h2>What to show</h2>
   <div class="chk"><input id="showName" type="checkbox"><label>Name / symbol</label></div>
   <div class="chk"><input id="showPrice" type="checkbox"><label>Price</label></div>
   <div class="chk"><input id="showChange" type="checkbox"><label>Change &amp; % change</label></div>
   <div class="chk"><input id="showChart" type="checkbox"><label>Sparkline chart</label></div>
   <div class="chk"><input id="showRangeLabel" type="checkbox"><label>Timeframe label</label></div>
   <div class="chk"><input id="showUpdatedAgo" type="checkbox"><label>"Updated N s ago"</label></div>
   <div class="chk"><input id="showPageDots" type="checkbox"><label>Rotation dots</label></div>
  </div>
 </section>

 <!-- SYMBOLS -->
 <section id="symbols" class="tab">
  <div class="card"><h2>Tickers (rotate on screen)</h2>
   <table id="symTable"></table>
   <button class="btn sec" style="margin-top:10px" onclick="addSym()">+ Add ticker</button>
   <small class="hint">Ticker symbol, e.g. <code>AAPL</code>, <code>NESN.SW</code>, <code>BTC-USD</code>, <code>EURUSD=X</code> (Swiss stocks use the <code>.SW</code> suffix on Yahoo). Name is optional &mdash; if set it overrides the source's name.</small>
  </div>
 </section>

 <!-- UPDATE -->
 <section id="update" class="tab">
  <div class="card"><h2>Firmware update (OTA)</h2>
   <input id="fw" type="file" accept=".bin">
   <div style="margin-top:12px"><button class="btn" onclick="upload()" id="upBtn">Upload &amp; flash</button></div>
   <div class="bar"><div id="upBar"></div></div>
   <div id="upMsg" class="muted" style="margin-top:8px"></div>
   <small class="hint">Upload the firmware.bin built by CI. The device reboots when done.</small>
  </div>
  <div class="card"><h2>Maintenance</h2>
   <button class="btn sec" onclick="reboot()">Reboot</button>
   <button class="btn danger" style="margin-left:8px" onclick="factory()">Factory reset</button>
  </div>
 </section>
</main>

<div style="text-align:center;padding:0 0 24px"><button class="btn" onclick="saveAll()">Save settings</button></div>
<div id="toast" class="toast"></div>

<script>
var C={};
function $(id){return document.getElementById(id)}
function toast(m){var t=$('toast');t.textContent=m;t.classList.add('show');setTimeout(function(){t.classList.remove('show')},2200)}
function j(url,opt){return fetch(url,opt).then(function(r){return r.json()})}

// tabs
document.querySelectorAll('nav button').forEach(function(b){b.onclick=function(){
 document.querySelectorAll('nav button').forEach(function(x){x.classList.remove('active')});
 document.querySelectorAll('.tab').forEach(function(x){x.classList.remove('active')});
 b.classList.add('active');$(b.dataset.t).classList.add('active');
}});

var BOOL=['autoBrightness','backlightInverted','showName','showPrice','showChange','showChart','showRangeLabel','showUpdatedAgo','showPageDots'];
var TEXT=['staSsid','apSsid','apPass','hostname','webhookUrl','usageUrl','range'];
var NUM=['brightness','rotateSec','pollSec','points'];

function loadConfig(){return j('/api/config').then(function(c){C=c;
 TEXT.forEach(function(k){$(k).value=c[k]!=null?c[k]:''});
 NUM.forEach(function(k){$(k).value=c[k]});
 BOOL.forEach(function(k){$(k).checked=!!c[k]});
 $('brVal').textContent=c.brightness;
 $('rotation').value=c.rotation;
 $('colorInverted').value=c.colorInverted?'true':'false';
 $('source').value=c.source||'yahoo';srcChanged();
 $('mode').value=c.mode||'stocks';modeChanged();
 $('apPass').placeholder=c.apPassSet?'(unchanged)':'(open)';
 renderSyms(c.symbols||[]);
})}
function modeChanged(){var u=$('mode').value==='usage';
 $('usageRow').style.display=u?'block':'none';
 $('modeHint').innerHTML=u
  ?'Shows your Claude <b>5h</b> &amp; <b>7d</b> usage from the local daemon (see <code>daemon/</code>). The screen plays the idle animation until the daemon starts sending data.'
  :'Shows live stock / crypto prices.';
 document.querySelectorAll('.stockonly').forEach(function(e){e.style.display=u?'none':''});}
function srcChanged(){var y=$('source').value!=='webhook';
 $('webhookRow').style.display=y?'none':'block';
 $('srcHint').innerHTML=y
  ?'The device fetches <b>Yahoo Finance</b> directly over HTTPS — no server needed. Use Yahoo symbols (e.g. <code>AAPL</code>, <code>NESN.SW</code>, <code>BTC-USD</code>, <code>EURUSD=X</code>).'
  :'The device requests <code>?symbol=..&amp;range=..&amp;points=..</code> from this URL and expects the SmallTV JSON contract back.';}

function collect(){
 var o={};
 TEXT.forEach(function(k){o[k]=$(k).value});
 NUM.forEach(function(k){o[k]=parseInt($(k).value)||0});
 BOOL.forEach(function(k){o[k]=$(k).checked});
 o.rotation=parseInt($('rotation').value);
 o.colorInverted=$('colorInverted').value==='true';
 o.source=$('source').value;
 o.mode=$('mode').value;
 var p=$('staPass').value; if(p)o.staPass=p;
 o.symbols=[];
 document.querySelectorAll('#symTable tr').forEach(function(tr){
  var s=tr.querySelector('.s').value.trim();
  if(s)o.symbols.push({symbol:s,name:tr.querySelector('.n').value.trim()});
 });
 return o;
}
function saveAll(){j('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(collect())})
 .then(function(r){toast(r.reboot?'Saved — rebooting...':'Saved');if(r.reboot)setTimeout(function(){location.reload()},6000);loadStatus()})}

function saveWifi(){
 var o={staSsid:$('staSsid').value};var p=$('staPass').value;if(p)o.staPass=p;
 j('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(o)}).then(function(){
  toast('Saved, rebooting to connect...');j('/api/reboot',{method:'POST'});
 });
}

// symbols
function renderSyms(arr){var t=$('symTable');t.innerHTML='';arr.forEach(addRow);if(!arr.length)addRow({})}
function addRow(o){var t=$('symTable');var tr=document.createElement('tr');tr.className='symrow';
 tr.innerHTML='<td style="width:42%"><input class="s" type="text" placeholder="AAPL" value="'+(o.symbol||'')+'"></td>'+
  '<td><input class="n" type="text" placeholder="name" value="'+(o.name||'')+'"></td>'+
  '<td style="width:34px"><button class="btn sec" style="padding:6px 10px" onclick="this.closest(\'tr\').remove()">&times;</button></td>';
 t.appendChild(tr);}
function addSym(){if(document.querySelectorAll('#symTable tr').length>=8){toast('Max 8');return}addRow({})}

// wifi scan
function scan(){$('scanList').innerHTML='<div class="muted">Scanning...</div>';
 j('/api/scan').then(function(l){var h='';l.sort(function(a,b){return b.rssi-a.rssi});
  l.forEach(function(n){h+='<div class="net" onclick="document.getElementById(\'staSsid\').value=this.dataset.s" data-s="'+
   n.ssid.replace(/"/g,'&quot;')+'"><span>'+(n.enc?'🔒 ':'')+n.ssid+'</span><span class="muted">'+n.rssi+' dBm</span></div>'});
  $('scanList').innerHTML=h||'<div class="muted">No networks found</div>';})}

// status
function loadStatus(){j('/api/status').then(function(s){
 $('dot').className='dot'+(s.connected?' ok':'');
 $('hi').textContent=s.mode==='ap'?'setup mode':(s.ip||'');
 $('statusBox').innerHTML=
  kv('Firmware',s.fw+' '+s.version)+kv('Mode',s.mode.toUpperCase())+
  kv('Network',s.ssid||'-')+kv('IP',s.ip||'-')+kv('Signal',s.rssi?s.rssi+' dBm':'-')+
  kv('Free heap',s.heap+' B')+kv('Uptime',fmtUp(s.uptime));
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
