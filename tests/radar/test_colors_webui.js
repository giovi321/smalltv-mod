// Run the real page's color loading, collecting, and reset functions.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const html=fs.readFileSync(process.env.WEBUI_HTML||'src/webui.html','utf8');
const between=(a,b)=>{const i=html.indexOf(a),k=html.indexOf(b,i);assert.ok(i>=0&&k>i,'page text not found: '+a);return html.slice(i,k)};
const groups=[['markerColor','radarMarkerColor','#ff0000'],['lineColor','radarLineColor','#424142'],['aircraftColor','radarAircraftColor','#848284'],['airportColor','radarAirportColor','#0082ff'],['labelColor','radarLabelColor','#848284']];
const els={radar:{}};
for(const [,id] of groups)els[id]={value:''};
const document={getElementById:id=>els[id]||null,querySelectorAll:()=>[]};
const ctx={document,C:{},parseInt,parseFloat,Object,collectWifi:()=>[],radarLocChanged:()=>{},radarSrcChanged:()=>{}};
vm.createContext(ctx);
vm.runInContext(between('function $(id)','// sun begin')+between('var MODEOPT=','function modeChanged()')+between('var T_TEXT=','// IANA -> POSIX')+between('function collect(){','function saveAll(){')+between('function radarResetColors(){','function esc('),ctx);
const loadRadar=between(' var r=c.radar||{};', ' renderAps(r.airports||[]);');
function load(radar){ctx.c={radar};vm.runInContext(loadRadar,ctx);}
load({});
for(const [,id,def] of groups)assert.strictEqual(els[id].value,def,'original default loads');
const chosen=['#00ff00','#000000','#de8400','#f08000','#abcdef'];
const config=Object.fromEntries(groups.map(([key],i)=>[key,chosen[i]]));
load(config);
for(const [key,id] of groups)assert.strictEqual(els[id].value,config[key],key+' loads');
const saved=ctx.collect().radar;
for(const [key] of groups)assert.strictEqual(saved[key],config[key],key+' saves');
// Reset changes the controls without issuing a save or changing loaded config.
let saves=0;ctx.j=()=>{saves++;};
ctx.radarResetColors();
assert.strictEqual(saves,0,'reset waits for Save settings');
assert.strictEqual(ctx.c.radar,config,'reset preserves loaded config');
for(const [key,id,def] of groups){
 assert.strictEqual(els[id].value,def,'reset fills picker');
 assert.strictEqual(ctx.collect().radar[key],def,'reset saves after Save settings');
}
const table=between('<table class="radar-colors">','</table>');
assert.strictEqual((table.match(/<tr>/g)||[]).length,1,'colors share one row');
assert.strictEqual((table.match(/<td>/g)||[]).length,5,'one cell per color');
for(const [,id,def] of groups)assert.ok(table.includes('id="'+id+'" type="color" value="'+def+'"'),'picker '+id);
console.log('radar color page tests passed');
