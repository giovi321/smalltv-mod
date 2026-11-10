// The result of the manual firmware upload reaches the page as the text of
// the reply (the firmware answers 500 with a reason when it refuses the
// upload for lack of memory in one piece). The page must show that text, not
// a generic failure, and must let the user try again. This runs the page's own
// upload() on a fake page and a fake XMLHttpRequest.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const html=fs.readFileSync(process.env.WEBUI_HTML||'src/webui.html','utf8');
const i=html.indexOf('function upload(){'),k=html.indexOf('\nloadConfig()',i);
assert.ok(i>=0&&k>i,'upload() not found in the page');
const code=html.slice(i,k);

function run(status,text,fail){
 const els={fw:{files:[{name:'firmware.bin'}]},upBtn:{disabled:false},upBar:{style:{}},upMsg:{textContent:''}};
 let xhr,reloaded=0,timers=[],toasts=[];
 class XHR{constructor(){xhr=this;this.upload={}}open(m,u){this.method=m;this.url=u}
  send(body){this.sent=body;if(fail)this.onerror();else{this.status=status;this.responseText=text;this.onload()}}}
 const ctx={$:id=>els[id],toast:m=>toasts.push(m),XMLHttpRequest:XHR,
  FormData:class{constructor(){this.f={}}append(n,v,f){this.f[n]=f}},
  location:{reload:()=>{reloaded++}},setTimeout:(f,ms)=>timers.push([f,ms]),Math};
 vm.createContext(ctx);
 vm.runInContext(code+';upload();',ctx);
 return {els,xhr,reloaded,timers,toasts};
}

const REFUSED='Upload not started: not enough memory in one piece (largest free block 3888 B, needed 4104 B, free memory 19600 B). Restart the device, then upload again.';

// The refusal text reaches the page in full, and the button works again.
{
 const r=run(500,REFUSED);
 assert.strictEqual(r.xhr.method,'POST');
 assert.strictEqual(r.xhr.url,'/update');
 assert.ok(r.els.upMsg.textContent.includes('not enough memory in one piece'),'the reason is shown: '+r.els.upMsg.textContent);
 assert.ok(r.els.upMsg.textContent.includes('Restart the device, then upload again'),'what to do is shown');
 assert.ok(r.els.upMsg.textContent.includes('3888')&&r.els.upMsg.textContent.includes('4104'),'the numbers are shown');
 assert.strictEqual(r.els.upBtn.disabled,false,'the button is usable again');
 assert.strictEqual(r.timers.length,0,'no reload is scheduled after a failure');
 assert.strictEqual(r.reloaded,0);
}
// Any other failure text is shown the same way.
{
 const r=run(500,'Not Enough Space');
 assert.strictEqual(r.els.upMsg.textContent,'Failed: Not Enough Space');
}
// Success reloads the page after the reboot time.
{
 const r=run(200,'OK');
 assert.strictEqual(r.els.upMsg.textContent,'Done. Rebooting...');
 assert.strictEqual(r.timers.length,1);
 assert.strictEqual(r.timers[0][1],9000);
}
// No answer at all (the connection broke): a message, and the button is usable.
{
 const r=run(0,'',true);
 assert.strictEqual(r.els.upMsg.textContent,'Upload error');
 assert.strictEqual(r.els.upBtn.disabled,false);
}
console.log('update webui: all passed');
