// The Status tab's "Last crash" line. It runs the page's own crashLine() on
// plain objects shaped like /api/status's lastCrash. Properties: a record from
// 3.5.0 / 3.6.0 (reason, registers, fw, uptime only) reads as before; each
// measured value of the extended record appears with its meaning; a value the
// device left out is not shown (never a made-up 0); an empty trace shows no
// "trace" text.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const html=fs.readFileSync(process.env.WEBUI_HTML||'src/webui.html','utf8');
const i=html.indexOf('function crashLine(');
const k=html.indexOf('function clearCrash(');
assert.ok(i>=0&&k>i,'crashLine not found in src/webui.html');
const ctx={fmtUp:s=>s+'s'};
vm.createContext(ctx);
vm.runInContext(html.slice(i,k),ctx);
const line=x=>ctx.crashLine(x);

// 1. the old record: exactly the old text
assert.equal(line({reason:'Exception',exccause:'0x1d',epc:'0x40201000',addr:'0x0',fw:'3.5.0',uptime:91}),
  'Exception, cause 0x1d, epc 0x40201000, addr 0x0, fw 3.5.0, up 91s');
assert.equal(line({reason:'Abort/out of memory',fw:'3.5.0'}),'Abort/out of memory, fw 3.5.0');

// 2. the full extended record (made-up values)
const full=line({reason:'Abort/out of memory',fw:'3.6.1',uptime:91,heap:3456,maxBlock:576,
  failSize:756,failAddr:'0x40201abc',lowHeap:3000,lowBlk:512,
  trace:['0x40201000','0x4010ffff'],more:2});
for(const part of ['heap 3456','block 576','756 B','0x40201abc','low heap 3000','low block 512',
    '0x40201000','0x4010ffff','2 earlier'])
  assert.ok(full.includes(part),'missing "'+part+'" in: '+full);
assert.ok(full.startsWith('Abort/out of memory, fw 3.6.1, up 91s'),'old part first: '+full);

// 3. a value left out by the device is not shown, and 0 is shown when it is a real 0
const part=line({reason:'Exception',fw:'3.6.1',heap:7000,trace:[],more:0});
assert.ok(part.includes('heap 7000'),part);
for(const bad of ['block','alloc','low','trace','earlier','undefined','NaN','null'])
  assert.ok(!part.includes(bad),'"'+bad+'" must not appear in: '+part);
// a failed allocation of size 0 is "none since boot", not a real allocation
assert.ok(!line({reason:'Exception',fw:'3.6.1',failSize:0,failAddr:'0x0'}).includes('alloc'),'failSize 0 is not shown');
assert.ok(line({reason:'Exception',fw:'3.6.1',heap:0}).includes('heap 0'),'a real 0 heap is shown');
assert.ok(line({reason:'Exception',fw:'3.6.1',maxBlock:0}).includes('block 0'),'a real 0 block is shown');

// 4. the trace is shown in order
const tr=line({reason:'Exception',fw:'3.6.1',trace:['0x40000001','0x40000002','0x40000003']});
assert.ok(tr.indexOf('0x40000001')<tr.indexOf('0x40000002')&&tr.indexOf('0x40000002')<tr.indexOf('0x40000003'),tr);
console.log('web UI crash line tests passed');
