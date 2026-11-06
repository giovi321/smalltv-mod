// A save from a build without a feature must not touch that feature's
// carousel flag. The page removes the checkbox of a feature that is not built
// in (hideFeat); collect() used to ask the missing box and send false.
// This runs the page's own hideFeat() and collect() on a small fake page.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const html=fs.readFileSync(process.env.WEBUI_HTML||'src/webui.html','utf8');
const between=(a,b)=>{const i=html.indexOf(a),k=html.indexOf(b,i);assert.ok(i>=0&&k>i,'page text not found: '+a);return html.slice(i,k)};
const helpers=between('function $(id)','// sun begin');
const feat=between('var MODEOPT=','function modeChanged()');
const collectCode=between('function collect(){','function saveAll(){');
const tickerVars=between('var T_TEXT=','// IANA -> POSIX');  // field lists collect() reads

const FEATS=['ticker','usage','radar','ha','theme'];
const BOX={ticker:'carouselTicker',usage:'carouselUsage',radar:'carouselRadar',ha:'carouselHa',theme:'carouselTheme'};

// A fake page: elements by id. A feature section holds its carousel box.
function makePage(ticked){
 const els={};
 const add=(id,props)=>{els[id]=Object.assign({id},props)};
 add('mode',{value:'carousel'});add('carouselSec',{value:'45'});
 add('brightness',{value:'80'});add('rotation',{value:'0'});
 add('hostname',{value:'smalltv-test'});add('apSsid',{value:'SmallTV-Test'});
 for(const f of FEATS){
  add(f,{remove(){delete els[f]}});
  add(BOX[f],{checked:!!ticked[f],closest(){return {remove(){delete els[BOX[f]]}}}});
 }
 const document={getElementById:id=>els[id]||null,querySelector:()=>null,querySelectorAll:()=>[]};
 const ctx={document,C:{},parseInt,parseFloat,Object,collectWifi:()=>[],toast:()=>{}};
 vm.createContext(ctx);
 vm.runInContext(helpers+feat+tickerVars+collectCode,ctx);
 return {ctx,els};
}

// Build with every feature: each flag is sent as ticked.
{
 const ticked={ticker:true,usage:false,radar:true,ha:false,theme:true};
 const {ctx}=makePage(ticked);
 const o=ctx.collect();
 for(const f of FEATS)assert.strictEqual(o[BOX[f]],ticked[f],'full build, '+BOX[f]);
 const flipped={ticker:false,usage:true,radar:false,ha:true,theme:false};
 const p=makePage(flipped);
 const o2=p.ctx.collect();
 for(const f of FEATS)assert.strictEqual(o2[BOX[f]],flipped[f],'full build flipped, '+BOX[f]);
}

// Build without ha, radar, ticker, usage (as loadConfig hides them): those
// four flags are not sent at all; the theme flag and the rest still are.
{
 const {ctx,els}=makePage({ticker:true,usage:true,radar:true,ha:true,theme:true});
 for(const f of ['ticker','usage','radar','ha'])ctx.hideFeat(f);
 for(const f of ['ticker','usage','radar','ha'])assert.ok(!els[BOX[f]],BOX[f]+' box should be gone');
 const o=ctx.collect();
 for(const f of ['ticker','usage','radar','ha'])
  assert.ok(!(BOX[f] in o),BOX[f]+' must not be sent, got '+o[BOX[f]]);
 assert.strictEqual(o.carouselTheme,true,'carouselTheme');
 assert.strictEqual(o.carouselSec,45);
 assert.strictEqual(o.mode,'carousel');
 assert.strictEqual(o.hostname,'smalltv-test');
}

// Build without the theme feature: only the theme flag is left out.
{
 const {ctx}=makePage({ticker:true,usage:false,radar:true,ha:false,theme:true});
 ctx.hideFeat('theme');
 const o=ctx.collect();
 assert.ok(!('carouselTheme' in o),'carouselTheme must not be sent');
 assert.strictEqual(o.carouselTicker,true);assert.strictEqual(o.carouselUsage,false);
 assert.strictEqual(o.carouselRadar,true);assert.strictEqual(o.carouselHa,false);
}
console.log('carousel flag tests passed');
