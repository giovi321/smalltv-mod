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
console.log('mode save page tests passed');
