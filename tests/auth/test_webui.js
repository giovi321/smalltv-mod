// The password card of the settings page. A stored password locks the page;
// there is no checkbox. This runs the page's own collect(), saveAll() and the
// password functions on a small fake page (like tests/theme/test_webui_carousel.js).
// All passwords here are made up.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const html=fs.readFileSync(process.env.WEBUI_HTML||'src/webui.html','utf8');
const between=(a,b)=>{const i=html.indexOf(a),k=html.indexOf(b,i);assert.ok(i>=0&&k>i,'page text not found: '+a);return html.slice(i,k)};

// The page itself: no checkbox, two password fields, a Remove button.
assert.ok(!/id="authEnabled"/.test(html),'the "ask for a password" checkbox must be gone');
assert.ok(/id="authPass2"/.test(html),'a second password field is needed');
assert.ok(/id="authRemoveBtn"[^>]*style="display:none[;"]/.test(html),'the Remove button starts hidden');
assert.ok(!/authEnabled/.test(html),'nothing may still read the checkbox');

const helpers=between('function $(id)','// sun begin');
const pwCode=between('// password begin','// password end');
const collectCode=between('function collect(){','function saveWifi(){');   // collect() and saveAll()
const tickerVars=between('var T_TEXT=','// IANA -> POSIX');
const feat=between('var MODEOPT=','function modeChanged()');
const loadCode=between('function loadConfig(){','\n})}');                // for the auth lines only

function makePage(fields){
 const els={};
 const add=(id,props)=>{els[id]=Object.assign({id,style:{},textContent:'',value:''},props)};
 add('mode',{value:'stocks'});add('carouselSec',{value:'45'});add('brightness',{value:'80'});
 add('rotation',{value:'0'});add('hostname',{value:'smalltv-test'});add('apSsid',{value:'SmallTV-Test'});
 add('authUser',{value:'admin'});add('authPass',{value:''});add('authPass2',{value:''});
 add('authRemoveBtn',{style:{display:'none'}});add('authState',{});
 for(const k of Object.keys(fields))els[k].value=fields[k];
 const document={getElementById:id=>els[id]||null,querySelector:()=>null,querySelectorAll:()=>[]};
 const requests=[],toasts=[];
 let confirmAnswer=true,confirmAsked=[],fail=null;
 const ctx={document,C:{},parseInt,parseFloat,Object,JSON,Promise,collectWifi:()=>[],
  toast:m=>toasts.push(m),loadStatus:()=>{},
  confirm:m=>{confirmAsked.push(m);return confirmAnswer},
  j:(url,opt)=>{requests.push({url,opt,body:opt&&opt.body?JSON.parse(opt.body):null});
   return fail?Promise.reject(new Error(fail)):Promise.resolve({ok:true,reboot:false})}};
 vm.createContext(ctx);
 vm.runInContext(helpers+feat+tickerVars+pwCode+collectCode,ctx);
 return {ctx,els,requests,toasts,confirmAsked,setConfirm:v=>{confirmAnswer=v},setFail:v=>{fail=v}};
}
const tick=()=>new Promise(r=>setTimeout(r,0));

(async()=>{
 // collect() never sends "enabled". A typed password goes out as "pass".
 {
  const p=makePage({authPass:'test-pass-01',authPass2:'test-pass-01',authUser:'owner'});
  const o=p.ctx.collect();
  assert.deepStrictEqual(Object.keys(o.auth).sort(),['pass','user'],'auth keys: '+Object.keys(o.auth));
  assert.strictEqual(o.auth.pass,'test-pass-01');assert.strictEqual(o.auth.user,'owner');
  assert.ok(!('enabled' in o.auth),'enabled must not be sent');
 }
 // Both blank: no pass key, so the stored password is kept.
 {
  const p=makePage({});
  const o=p.ctx.collect();
  assert.deepStrictEqual(Object.keys(o.auth),['user'],'blank fields: only the user');
  assert.strictEqual(p.ctx.authProblem(),'','both blank is fine');
 }
 // The check: equal is fine, anything else is a message.
 {
  const same=makePage({authPass:'test-pass-02',authPass2:'test-pass-02'});
  assert.strictEqual(same.ctx.authProblem(),'');
  const diff=makePage({authPass:'test-pass-02',authPass2:'test-pass-03'});
  assert.match(diff.ctx.authProblem(),/not the same/);
  const only1=makePage({authPass:'test-pass-02'});
  assert.match(only1.ctx.authProblem(),/not the same/,'only the first field filled');
  const only2=makePage({authPass2:'test-pass-02'});
  assert.match(only2.ctx.authProblem(),/not the same/,'only the second field filled');
  const caseDiff=makePage({authPass:'Test-pass-02',authPass2:'test-pass-02'});
  assert.match(caseDiff.ctx.authProblem(),/not the same/,'case counts');
 }
 // saveAll(): a mismatch sends nothing and says why.
 {
  const p=makePage({authPass:'test-pass-04',authPass2:'test-pass-05'});
  p.ctx.saveAll();await tick();
  assert.strictEqual(p.requests.length,0,'mismatch: nothing may be sent');
  assert.ok(p.toasts.length===1&&/not the same/.test(p.toasts[0])&&/Nothing was saved/.test(p.toasts[0]),'message: '+p.toasts);
 }
 // saveAll(): equal fields send the password; the card then shows a password is set.
 {
  const p=makePage({authPass:'test-pass-06',authPass2:'test-pass-06'});
  p.ctx.saveAll();await tick();
  assert.strictEqual(p.requests.length,1);
  assert.strictEqual(p.requests[0].url,'/api/config');
  assert.strictEqual(p.requests[0].body.auth.pass,'test-pass-06');
  assert.ok(!('enabled' in p.requests[0].body.auth));
  assert.strictEqual(p.els.authRemoveBtn.style.display,'','Remove button shown after a password was saved');
  assert.strictEqual(p.els.authPass.value,'');assert.strictEqual(p.els.authPass2.value,'','fields cleared');
 }
 // saveAll(): both blank sends no password and leaves the Remove button as it was.
 {
  const p=makePage({});
  p.ctx.saveAll();await tick();
  assert.strictEqual(p.requests.length,1);
  assert.ok(!('pass' in p.requests[0].body.auth),'blank: no pass key');
  assert.strictEqual(p.els.authRemoveBtn.style.display,'none','still hidden');
 }
 // saveAll(): a failed save leaves the card as it was.
 {
  const p=makePage({authPass:'test-pass-07',authPass2:'test-pass-07'});
  p.setFail('Request failed');
  p.ctx.saveAll();await tick();
  assert.strictEqual(p.els.authRemoveBtn.style.display,'none','failed save: button still hidden');
 }
 // The Remove button is shown only when the device says a password is set.
 {
  const p=makePage({});
  p.ctx.authShow(false);
  assert.strictEqual(p.els.authRemoveBtn.style.display,'none');
  assert.match(p.els.authState.textContent,/No password is set/);
  p.ctx.authShow(true);
  assert.strictEqual(p.els.authRemoveBtn.style.display,'');
  assert.match(p.els.authState.textContent,/password is set/);
  assert.strictEqual(p.els.authPass.placeholder,'(unchanged)');
  p.ctx.authShow(false);
  assert.strictEqual(p.els.authPass.placeholder,'set a password');
 }
 // loadConfig() hands the device's passSet to the card (and the old "enabled" is not read).
 {
  const p=makePage({});
  const c={auth:{enabled:true,passSet:false,user:'owner'}};   // an inconsistent reply: passSet decides
  const ctx2=p.ctx;
  ctx2.__c=c;
  vm.runInContext("var au=__c.auth||{}; sv('authUser',au.user||'admin'); authShow(!!au.passSet);",ctx2);
  assert.strictEqual(p.els.authRemoveBtn.style.display,'none','passSet false: hidden');
  assert.strictEqual(p.els.authUser.value,'owner');
  // The page's own loadConfig must contain exactly these two calls.
  assert.ok(/authShow\(!!au\.passSet\)/.test(loadCode),'loadConfig calls authShow(!!au.passSet)');
  assert.ok(!/au\.enabled/.test(loadCode),'loadConfig must not read au.enabled');
 }
 // Remove: asks once, sends its own request, then the card shows the device is open.
 {
  const p=makePage({});
  p.ctx.authShow(true);
  p.ctx.authRemove();await tick();
  assert.strictEqual(p.confirmAsked.length,1,'one confirm');
  assert.strictEqual(p.requests.length,1);
  assert.strictEqual(p.requests[0].url,'/api/config');
  assert.deepStrictEqual(p.requests[0].body,{auth:{removePass:true}},'the request is only the remove key');
  assert.strictEqual(p.els.authRemoveBtn.style.display,'none','button hidden after removal');
  assert.match(p.els.authState.textContent,/No password is set/,'page says the device is open');
  assert.ok(p.toasts.some(t=>/open again/.test(t)),'toast: '+p.toasts);
 }
 // Remove declined: nothing is sent.
 {
  const p=makePage({});
  p.ctx.authShow(true);p.setConfirm(false);
  p.ctx.authRemove();await tick();
  assert.strictEqual(p.requests.length,0,'declined: no request');
  assert.strictEqual(p.els.authRemoveBtn.style.display,'','still shown');
 }
 // Remove fails: the card keeps saying a password is set.
 {
  const p=makePage({});
  p.ctx.authShow(true);p.setFail('Request failed');
  p.ctx.authRemove();await tick();
  assert.strictEqual(p.els.authRemoveBtn.style.display,'','failed: still shown');
  assert.match(p.els.authState.textContent,/password is set/);
  assert.ok(p.toasts.some(t=>/Could not remove/.test(t)),'toast: '+p.toasts);
 }
 console.log('password page tests passed');
})().catch(e=>{console.error(e);process.exit(1)});
