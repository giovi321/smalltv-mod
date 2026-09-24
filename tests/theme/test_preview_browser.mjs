// Optional real-browser check: Node 22+ and Chrome/Chromium, no npm dependencies.
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {mkdtemp,readFile,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {setTimeout as delay} from 'node:timers/promises';

const profile=await mkdtemp(join(tmpdir(),'smalltv-preview-browser-'));
const chrome=spawn(process.env.CHROME||'google-chrome',[
 '--headless','--no-sandbox','--disable-gpu','--disable-dev-shm-usage',
 '--disable-background-networking','--no-first-run','--remote-debugging-port=0',
 '--user-data-dir='+profile,'about:blank'
],{stdio:'ignore'});
let launchError;chrome.on('error',error=>{launchError=error});
let socket;
try {
 let port;
 for(let i=0;i<200;i++){
  if(launchError)throw launchError;
  try{port=(await readFile(join(profile,'DevToolsActivePort'),'utf8')).split('\n')[0];break}catch{}
  await delay(50);
 }
 assert.ok(port,'Chrome debugging endpoint did not start');
 const targets=await (await fetch('http://127.0.0.1:'+port+'/json/list')).json();
 const page=targets.find(target=>target.type==='page');assert.ok(page);
 socket=new WebSocket(page.webSocketDebuggerUrl);
 await new Promise((resolve,reject)=>{socket.onopen=resolve;socket.onerror=reject});
 let id=0;const pending=new Map(),errors=[],requests=[];
 socket.onmessage=event=>{
  const message=JSON.parse(event.data);
  if(message.method==='Runtime.exceptionThrown')errors.push(message.params.exceptionDetails);
  if(message.method==='Network.requestWillBeSent')requests.push(message.params.request.url);
  const waiter=pending.get(message.id);
  if(waiter){pending.delete(message.id);clearTimeout(waiter.timer);message.error?waiter.reject(message.error):waiter.resolve(message.result)}
 };
 function cdp(method,params={}){
  return new Promise((resolve,reject)=>{
   const key=++id,timer=setTimeout(()=>{pending.delete(key);reject(new Error('CDP timeout: '+method))},10000);
   pending.set(key,{resolve,reject,timer});socket.send(JSON.stringify({id:key,method,params}));
  });
 }
 async function evaluate(expression){
  const result=await cdp('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});
  assert.equal(result.exceptionDetails,undefined,JSON.stringify(result.exceptionDetails));
  return result.result.value;
 }
 await cdp('Runtime.enable');await cdp('Network.enable');await cdp('Page.enable');
 const usingDefault=!process.argv[2];
 const file=resolve(process.argv[2]||'examples/themes/pixel-room-preview.html');
 await cdp('Page.navigate',{url:pathToFileURL(file).href});
 let ready=false;
 for(let i=0;i<100;i++){
  ready=await evaluate("Boolean(document.getElementById('play')&&!document.getElementById('play').disabled)");
  if(ready)break;
  await delay(50);
 }
 assert.ok(ready,'Preview images did not decode');
 assert.equal(await evaluate("document.getElementById('play').textContent"),'Pause');
 await evaluate("document.getElementById('play').click()");
 const paused=await evaluate("document.getElementById('timeline').value");await delay(250);
 assert.equal(await evaluate("document.getElementById('timeline').value"),paused,'Pause must stop playback');
 await evaluate("document.getElementById('timeline').value=15;document.getElementById('timeline').dispatchEvent(new Event('input'))");
 assert.equal(await evaluate("document.getElementById('timeline').value"),'15');
 // The exact wall-clock text is only known for the checked-in pixel-room fixture;
 // any other preview (e.g. one built from injected dynamic values) only needs
 // to prove it plays, without asserting its theme-specific pixel content.
 if(usingDefault){
  const before=await evaluate("document.getElementById('clock').textContent");
  assert.match(before,/10:24:56$/);
 }
 await evaluate("document.getElementById('play').click()");await delay(250);
 assert.ok(Number(await evaluate("document.getElementById('timeline').value"))>15,'Play must advance animation');
 await evaluate("document.getElementById('timeline').value=document.getElementById('timeline').max;document.getElementById('timeline').dispatchEvent(new Event('input'))");
 assert.equal(await evaluate("document.getElementById('play').textContent"),'Replay');
 await evaluate("document.getElementById('play').click()");
 assert.ok(Number(await evaluate("document.getElementById('timeline').value"))<15,'Replay must restart');
 assert.deepEqual(errors,[]);
 assert.ok(requests.every(url=>url.startsWith('file:')||url.startsWith('data:')),'Preview must work without network requests');
 console.log('theme preview browser tests passed (offline load, pause, seek, play, replay)');
} finally {
 if(socket)socket.close();
 const exited=new Promise(resolve=>chrome.once('exit',resolve));chrome.kill('SIGTERM');
 await Promise.race([exited,delay(2000)]);
 if(chrome.exitCode===null)chrome.kill('SIGKILL');
 await rm(profile,{recursive:true,force:true,maxRetries:5,retryDelay:200});
}
