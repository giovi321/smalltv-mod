// The web UI's status polling, run with a fake clock and a fake fetch.
// Property: the device never sees a second status request while the first
// is still unanswered (a woken PC with a few open tabs used to send six at
// once and crash the ESP8266), a lost reply cannot stop the polling for
// good, and a hidden tab asks for nothing.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const html=fs.readFileSync('src/webui.html','utf8');
const start=html.indexOf('var STATUS_MS');
const end=html.indexOf('function showStatus(');
assert.ok(start>0&&end>start,'status polling code not found in src/webui.html');
const polling=html.slice(start,end);
const jsonHelper=html.slice(html.indexOf('function j('),html.indexOf('// tabs'));

const flush=async()=>{for(let i=0;i<30;i++)await Promise.resolve()};
// Timers on a virtual clock: advance(ms) runs every timer due on the way,
// in time order, letting promises settle after each one.
function makeClock(){
 let now=0,seq=0;const timers=new Map();
 return {
  setTimeout(fn,ms){const id=++seq;timers.set(id,{at:now+(ms||0),fn});return id},
  clearTimeout(id){timers.delete(id)},
  pending:()=>timers.size,
  async advance(ms){
   const stop=now+ms;
   for(;;){
    await flush();
    let bestId=0,best=null;
    for(const [id,t] of timers)if(t.at<=stop&&(!best||t.at<best.at)){best=t;bestId=id}
    if(!best)break;
    timers.delete(bestId);now=Math.max(now,best.at);best.fn();
   }
   now=stop;await flush();
  }
 };
}
// A fetch that answers after `delay` ms (never, with Infinity), honours the
// abort signal like a browser does, and counts requests in flight.
function makeFetch(clock,{delay,fail=false}){
 const st={started:0,inflight:0,max:0};
 st.fetch=(url,opt)=>{
  assert.equal(url,'/api/status');
  st.started++;st.inflight++;st.max=Math.max(st.max,st.inflight);
  return new Promise((resolve,reject)=>{
   let done=false;
   const finish=f=>{if(done)return;done=true;st.inflight--;f()};
   if(opt&&opt.signal)opt.signal.addEventListener('abort',()=>finish(()=>reject(new Error('aborted'))));
   if(delay!==Infinity)clock.setTimeout(()=>finish(()=>fail?reject(new Error('down')):resolve({ok:true,json:async()=>({n:st.started})})),delay);
  });
 };
 return st;
}
function page({delay,fail=false}){
 const clock=makeClock(),net=makeFetch(clock,{delay,fail}),listeners={},shown=[];
 const document={hidden:false,addEventListener:(type,fn)=>{listeners[type]=fn}};
 const context={setTimeout:clock.setTimeout,clearTimeout:clock.clearTimeout,fetch:net.fetch,AbortController,
  document,JSON,Promise,Error,showStatus:s=>shown.push(s)};
 vm.createContext(context);vm.runInContext(jsonHelper+polling,context);
 return {clock,net,context,shown,
  hide:async()=>{document.hidden=true;listeners.visibilitychange();await flush()},
  show:async()=>{document.hidden=false;listeners.visibilitychange();await flush()}};
}
const MIN=60000;
// Start-up: polling begins when the config has been asked for, whether it
// answered or not, and never as a fixed interval that ignores the replies.
assert.doesNotMatch(html,/setInterval\(\s*loadStatus/,'status polled on a fixed interval');
assert.match(html,/loadConfig\(\)\.then\(loadStatus,\s*loadStatus\)/,'polling does not start when the config fails');

(async()=>{
 // The device answers slowly: every reply takes longer than the 5 s poll.
 {const p=page({delay:7000});p.context.loadStatus();await p.clock.advance(10*MIN);
  assert.equal(p.net.max,1,'slow replies: requests overlapped');
  assert.ok(p.net.started>=45,'slow replies: polling stalled, '+p.net.started+' requests in 10 min');
  assert.ok(p.shown.length>=45,'slow replies: answers not shown');}
 // The reply never comes: the request is given up and polling goes on.
 {const p=page({delay:Infinity});p.context.loadStatus();await p.clock.advance(10*MIN);
  assert.equal(p.net.max,1,'lost reply: requests overlapped');
  assert.ok(p.net.started>=30,'lost reply: polling stopped for good, '+p.net.started+' requests in 10 min');
  assert.ok(p.net.started<=70,'lost reply: polling too fast, '+p.net.started+' requests in 10 min');}
 // Errors do not stop it either, and it keeps its pace.
 {const p=page({delay:50,fail:true});p.context.loadStatus();await p.clock.advance(10*MIN);
  assert.equal(p.net.max,1);
  assert.ok(p.net.started>=100&&p.net.started<=125,'failing replies: '+p.net.started+' requests in 10 min');}
 // A healthy device: about one request every 5 s, never two together.
 {const p=page({delay:100});p.context.loadStatus();await p.clock.advance(10*MIN);
  assert.equal(p.net.max,1);
  assert.ok(p.net.started>=110&&p.net.started<=125,'fast replies: '+p.net.started+' requests in 10 min');}
 // Another part of the page asking for the status (after a save) while a
 // read is under way joins it instead of sending a second request.
 {const p=page({delay:3000});p.context.loadStatus();p.context.loadStatus();p.context.loadStatus();
  await p.clock.advance(1000);assert.equal(p.net.started,1);assert.equal(p.net.max,1);
  await p.clock.advance(20000);p.context.loadStatus();p.context.loadStatus();
  assert.equal(p.net.max,1);}
 // A hidden tab asks for nothing, and one visible again reads at once.
 {const p=page({delay:100});p.context.loadStatus();await p.clock.advance(12000);
  const beforeHide=p.net.started;
  await p.hide();const hiddenAt=p.net.started;
  assert.equal(hiddenAt,beforeHide,'hiding the tab sent a request');
  await p.clock.advance(30*MIN);
  assert.equal(p.net.started,hiddenAt,'hidden tab sent '+(p.net.started-hiddenAt)+' requests');
  assert.equal(p.clock.pending(),0,'hidden tab keeps a timer');
  await p.show();
  assert.equal(p.net.started,hiddenAt+1,'visible again: not exactly one read at once');
  await p.show();
  assert.equal(p.net.started,hiddenAt+1,'a second visible event sent a second read');
  await p.clock.advance(11000);
  assert.ok(p.net.started>=hiddenAt+3,'polling did not resume');
  assert.equal(p.net.max,1);}
 // Hidden while a read is under way: that one finishes, nothing follows.
 {const p=page({delay:3000});p.context.loadStatus();await p.clock.advance(1000);
  await p.hide();await p.clock.advance(10*MIN);
  assert.equal(p.net.started,1);assert.equal(p.net.inflight,0);
  await p.show();assert.equal(p.net.started,2);}
 // Visible again while a read is still under way: no second one.
 {const p=page({delay:3000});p.context.loadStatus();await p.clock.advance(500);
  await p.hide();await p.show();
  assert.equal(p.net.started,1);assert.equal(p.net.max,1);
  await p.clock.advance(MIN);assert.ok(p.net.started>1);}
 console.log('web UI status polling tests passed');
})().catch(error=>{console.error(error);process.exitCode=1});
