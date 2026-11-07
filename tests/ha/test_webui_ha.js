// The web page says what the device could not do about Home Assistant, and that
// switching it on or off needs a restart. This runs the page's own showStatus()
// and saveAll() on a small fake page: the Status tab gets a "Home Assistant"
// row only when the device reports a problem, and a save that changes the
// broker host says "restart" instead of "Saved" alone.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const html=fs.readFileSync(process.env.WEBUI_HTML||'src/webui.html','utf8');
const between=(a,b)=>{const i=html.indexOf(a),k=html.indexOf(b,i);assert.ok(i>=0&&k>i,'page text not found: '+a);return html.slice(i,k)};

const note=between('function haNote(','function showStatus(');
const showStatus=between('function showStatus(','function kv(');
const kvCode=between('function kv(','// WireGuard: one line');
const saveAllCode=between('function saveAll(){','function saveWifi(){');

// ---- Status tab ----
function statusRows(s){
 const els={};
 const el=id=>els[id]||(els[id]={id,textContent:'',innerHTML:'',className:'',href:''});
 const ctx={$:el,C:{},window:{},statusTimer:0,
  sunNowText:()=>null,fmtUp:()=>'1m',radarLine:()=>'',wgLine:()=>'',
  setTimeout,clearTimeout};
 vm.createContext(ctx);
 vm.runInContext(note+kvCode+showStatus,ctx);
 ctx.showStatus(Object.assign({fw:'smalltv-mod',version:'0.0.0',mode:'sta',connected:true,ssid:'home-wifi-01',ip:'192.0.2.10',
  rssi:-60,heap:7000,maxblk:3000,uptime:60,reset:'Power on'},s));
 return els.statusBox.innerHTML;
}

// Nothing wrong: the row is not there at all.
assert.ok(!/Home Assistant/.test(statusRows({})),'no Home Assistant row when the device reports no problem');

// Out of memory at boot: the row says it, says how to get out of it.
{
 const t=statusRows({ha:'memory'});
 assert.match(t,/Home Assistant/);
 assert.match(t,/not enough memory/i);
 assert.match(t,/restart/i);
}
// Switched on or off since boot: the row asks for a restart.
{
 const t=statusRows({ha:'reboot'});
 assert.match(t,/Home Assistant/);
 assert.match(t,/Restart the device/);
 assert.ok(!/not enough memory/i.test(t),'a pending switch is not a memory problem');
}
// Safe mode after a crash.
{
 const t=statusRows({ha:'safe'});
 assert.match(t,/Home Assistant/);
 assert.match(t,/safe mode/i);
}
// A code the page does not know shows no row rather than an empty one.
assert.ok(!/Home Assistant/.test(statusRows({ha:'something new'})),'unknown code: no row');

// ---- Save ----
async function saveWith(reply){
 const toasts=[];let reloaded=false,statusReads=0;
 const ctx={j:()=>Promise.resolve(reply),collect:()=>({}),JSON,
  toast:m=>toasts.push(m),loadStatus:()=>{statusReads++},
  location:{reload:()=>{reloaded=true}},setTimeout:()=>0,Promise,
  authProblem:()=>'',gv:()=>'',authShow:()=>{}};   // saveAll() checks the password fields first
 vm.createContext(ctx);
 vm.runInContext(note+saveAllCode,ctx);
 ctx.saveAll();
 for(let i=0;i<20;i++)await Promise.resolve();
 return {toasts,statusReads,reloaded};
}
(async()=>{
 let r=await saveWith({ok:true});
 assert.deepEqual(r.toasts,['Saved'],'a plain save still says Saved');
 r=await saveWith({ok:true,ha:'reboot'});
 assert.equal(r.toasts.length,1);
 assert.match(r.toasts[0],/Saved/);
 assert.match(r.toasts[0],/Restart the device/,'a host change says a restart is needed');
 assert.ok(!r.reloaded,'the page does not reload: the device is not restarting by itself');
 r=await saveWith({ok:true,ha:'memory'});
 assert.match(r.toasts[0],/not enough memory/i);
 // Rebooting for the Wi-Fi wins: it restarts anyway.
 r=await saveWith({ok:true,reboot:true,ha:'reboot'});
 assert.equal(r.toasts[0],'Saved — rebooting...');
 console.log('webui ha tests passed');
})().catch(e=>{console.error(e);process.exit(1)});
