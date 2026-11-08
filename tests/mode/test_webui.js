// A save must not send a mode the page does not offer. A build without a
// feature loses that feature's entry in the mode list (hideFeat). If the
// device stored that mode, the list shows nothing selected, gv('mode') is ''
// and collect() sent mode:'' -- which the firmware turned into the stock
// ticker. This runs the page's own hideFeat() and collect() on a fake page
// whose mode list behaves like a <select>.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const html=fs.readFileSync(process.env.WEBUI_HTML||'src/webui.html','utf8');
const between=(a,b)=>{const i=html.indexOf(a),k=html.indexOf(b,i);assert.ok(i>=0&&k>i,'page text not found: '+a);return html.slice(i,k)};
const helpers=between('function $(id)','// sun begin');
const feat=between('var MODEOPT=','function modeChanged()');
const collectCode=between('function collect(){','function saveAll(){');
const tickerVars=between('var T_TEXT=','// IANA -> POSIX');

const OPTIONS=['stocks','usage','radar','ha','theme','carousel'];

// A <select>: setting a value with no matching option leaves nothing selected.
function makeSelect(){
 const opts=OPTIONS.map(v=>({value:v,remove(){opts.splice(opts.indexOf(this),1)}}));
 let cur=opts[0].value;
 return {id:'mode',opts,
  get value(){return opts.some(o=>o.value===cur)?cur:''},
  set value(v){cur=opts.some(o=>o.value===v)?v:''}};
}

function makePage(){
 const els={};
 const add=(id,props)=>{els[id]=Object.assign({id},props)};
 els.mode=makeSelect();
 add('carouselSec',{value:'45'});add('brightness',{value:'80'});add('rotation',{value:'0'});
 add('hostname',{value:'smalltv-test'});add('apSsid',{value:'SmallTV-Test'});
 const document={getElementById:id=>els[id]||null,
  querySelector:sel=>{const m=/^#mode option\[value="(\w+)"\]$/.exec(sel);
   return m?els.mode.opts.find(o=>o.value===m[1])||null:null},
  querySelectorAll:()=>[]};
 const ctx={document,C:{},parseInt,parseFloat,Object,collectWifi:()=>[],toast:()=>{}};
 vm.createContext(ctx);
 vm.runInContext(helpers+feat+tickerVars+collectCode,ctx);
 return {ctx,els};
}

// Full build: the chosen mode is sent, whatever it is.
for(const m of OPTIONS){
 const {ctx,els}=makePage();
 els.mode.value=m;
 assert.strictEqual(ctx.collect().mode,m,'full build, mode '+m);
}

// Lean build: the stored mode is a feature the build lacks. The page cannot
// show it, so it sends no mode at all.
for(const [feat,m] of [['radar','radar'],['usage','usage'],['ha','ha'],['ticker','stocks'],['theme','theme']]){
 const {ctx,els}=makePage();
 ctx.hideFeat(feat);
 els.mode.value=m;                       // what loadConfig does with the stored mode
 assert.strictEqual(els.mode.value,'','nothing selected for '+m);
 const o=ctx.collect();
 assert.ok(!('mode' in o),'mode must not be sent when the list lacks '+m+', got '+JSON.stringify(o.mode));
 assert.strictEqual(o.hostname,'smalltv-test','the rest is still sent');
 assert.strictEqual(o.carouselSec,45);
}

// Lean build, but the stored mode is one the page does offer: it is sent.
{
 const {ctx,els}=makePage();
 ctx.hideFeat('radar');ctx.hideFeat('ha');
 els.mode.value='carousel';
 assert.strictEqual(ctx.collect().mode,'carousel');
 els.mode.value='usage';
 assert.strictEqual(ctx.collect().mode,'usage');
}

// Independent radar colors, legacy loading, and resetting without saving.
{
 const {ctx,els}=makePage();
 els.radar={};
 const groups=[['markerColor','radarMarkerColor','#ff0000'],['lineColor','radarLineColor','#424142'],['aircraftColor','radarAircraftColor','#848284'],['airportColor','radarAirportColor','#0082ff'],['labelColor','radarLabelColor','#848284']];
 for(const [,id] of groups)els[id]={value:''};
 ctx.radarLocChanged=()=>{};ctx.radarSrcChanged=()=>{};
 const loadRadar=between(' var r=c.radar||{};', ' renderAps(r.airports||[]);');
 function load(radar){ctx.c={radar};vm.runInContext(loadRadar,ctx);}
 load({uiColor:'#12abef'});
 for(const [key,id] of groups)assert.strictEqual(els[id].value,key==='markerColor'?'#ff0000':'#12abef','legacy color loads');
 load({markerColor:'#abcdef',lineColor:'#010203',aircraftColor:'#123456',airportColor:'#0082ff',labelColor:'#ffffff'});
 for(const [key,id] of groups)assert.strictEqual(els[id].value,ctx.c.radar[key],key+' loads');
 els.radarMarkerColor.value='#00ff00';
 els.radarLineColor.value='#000000';
 els.radarAircraftColor.value='#de8400';
 els.radarAirportColor.value='#f08000';
 els.radarLabelColor.value='#abcdef';
 const saved=ctx.collect().radar;
 assert.strictEqual(saved.markerColor,'#00ff00');
 assert.strictEqual(saved.lineColor,'#000000');
 assert.strictEqual(saved.aircraftColor,'#de8400');
 assert.strictEqual(saved.airportColor,'#f08000');
 assert.strictEqual(saved.labelColor,'#abcdef');
 assert.ok(!('uiColor' in saved),'save uses the separate keys');
 load({});
 for(const [,id,def] of groups)assert.strictEqual(els[id].value,def,'default loads');
 vm.runInContext(between('function radarResetColors(){','function esc('),ctx);
 for(const [,id] of groups)els[id].value='#123456';
 ctx.radarResetColors();
 for(const [key,id,def] of groups){
  assert.strictEqual(els[id].value,def,'reset fills picker');
  assert.strictEqual(ctx.collect().radar[key],def,'reset saves after Save settings');
 }
}
console.log('mode and radar color page tests passed');

// The status row exposes parser and TLS failures instead of only "parse failed".
{
 const ctx={};vm.createContext(ctx);
 vm.runInContext(between('function radarLine(r){','function wgLine(w){'),ctx);
 const line=ctx.radarLine({stage:'parse failed',http:200,parseError:'IncompleteInput',tlsError:7});
 assert.ok(line.includes('JSON IncompleteInput'));
 assert.ok(line.includes('TLS error 7'));
 assert.ok(!ctx.radarLine({stage:'idle'}).includes('JSON'));
 const cached=ctx.radarLine({count:24,error:true,stage:'parse failed',http:200,parseError:'IncompleteInput'});
 assert.ok(cached.includes('24 aircraft'));
 assert.ok(cached.includes('parse failed'),'cached aircraft must not hide the failure stage');
 assert.ok(!ctx.radarLine({count:24,error:false,stage:'ok'}).includes('parse failed'));
}

{
 const {ctx,els}=makePage();
 els.startupTextColor={value:'#ffffff'};
 assert.strictEqual(ctx.collect().startupTextColor,'#ffffff');
 assert.ok(html.includes('id="startupTextColor" type="color"'));
}
