// A check on the sources: the manual firmware upload (WebPortal.cpp) asks the
// pure rule in UpdateBuffer.h before it calls Update.begin(), refuses with the
// rule's text instead of a generic failure, never answers "OK" (and never
// schedules the reboot) for a refused upload, and does all of it on the
// ESP8266 only. The rule itself is tested in test_update_buffer.cpp.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');

const root=process.env.SRC_ROOT||'src';
const portal=fs.readFileSync(path.join(root,'WebPortal.cpp'),'utf8');
const body=(text,head)=>{const i=text.indexOf(head);assert.ok(i>=0,'not found: '+head);
 const k=text.indexOf('\n}\n',i);assert.ok(k>i,'end of '+head);return text.slice(i,k)};
// The text between `#if defined(SMALLTV_FAMILY_ESP8266)` and its `#endif`
// that contains `needle`: the check must sit inside such a block.
const esp8266Blocks=text=>{const out=[];const re=/#if defined\(SMALLTV_FAMILY_ESP8266\)\n([\s\S]*?)\n#endif/g;
 let m;while((m=re.exec(text)))out.push(m[1]);return out};

assert.ok(/#include "UpdateBuffer\.h"/.test(portal),'WebPortal.cpp includes UpdateBuffer.h');

const upload=body(portal,'static void handleUpdateUpload() {');
const done=body(portal,'static void handleUpdateDone() {');

// 1. The check runs at UPLOAD_FILE_START, before Update.begin, on the ESP8266 only.
{
 const start=upload.indexOf('if (up.status == UPLOAD_FILE_START) {');
 assert.ok(start>=0,'START branch');
 const check=upload.indexOf('updatebuf::uploadCanStart(',start);
 const begin=upload.indexOf('if (!Update.begin(',start);   // the call, not a comment
 assert.ok(check>start,'START asks uploadCanStart');
 assert.ok(begin>check,'uploadCanStart comes before Update.begin');
 assert.ok(/uploadCanStart\(\s*\w+,\s*\w+\)/.test(upload),'uploadCanStart gets the free heap and the largest block');
 assert.ok(/ESP\.getMaxFreeBlockSize\(\)/.test(upload),'the largest block is read from the core');
 assert.ok(/ESP\.getFreeHeap\(\)/.test(upload),'the free heap is read from the core');
 assert.ok(esp8266Blocks(upload).some(b=>b.includes('updatebuf::uploadCanStart(')),'the check sits in an ESP8266 block');
 // After WiFiUDP::stopAll(), which frees memory, so the numbers are the ones Update.begin sees.
 const stopAll=upload.indexOf('WiFiUDP::stopAll()');
 assert.ok(stopAll>=0&&stopAll<check,'the numbers are read after stopAll');
 // A refused start remembers why and does not call begin.
 const after=upload.slice(check,begin);
 assert.ok(/g_updateRefused = true;/.test(after),'a refused start sets the flag');
 assert.ok(/return;/.test(after),'a refused start returns before Update.begin');
}
// 2. The flag is cleared at every start.
assert.ok(/g_updateRefused = false;/.test(upload.slice(upload.indexOf('UPLOAD_FILE_START'),upload.indexOf('uploadCanStart'))),
  'START clears the flag first');
// 3. A refused upload ignores the chunks, the end and the abort.
{
 const gate=upload.indexOf('if (g_updateRefused)');
 assert.ok(gate>=0,'a gate on the flag');
 assert.ok(gate>upload.indexOf('uploadCanStart('),'the gate comes after START');
 assert.ok(gate<upload.indexOf('Update.write('),'the gate is before Update.write');
 assert.ok(gate<upload.indexOf('Update.end(true)'),'the gate is before Update.end');
}
// 4. The final handler answers with the rule's text, and neither says OK nor reboots.
{
 const refuse=done.indexOf('if (g_updateRefused) {');
 assert.ok(refuse>=0,'done handler looks at the flag');
 const block=done.slice(refuse,done.indexOf('}',done.indexOf('return;',refuse))+1);
 assert.ok(/updatebuf::refusalText\(/.test(block),'the reply is the rule\'s text');
 assert.ok(/server\.send\(500, "text\/plain", \w+\)/.test(block),'status 500 with the text');
 assert.ok(!/scheduleReboot/.test(block),'a refused upload does not reboot');
 assert.ok(/g_updateRefused = false;/.test(block),'the flag is cleared once reported');
 assert.ok(refuse<done.indexOf('Update.hasError()'),'the flag is read before the core\'s own error');
 assert.ok(esp8266Blocks(done).some(b=>b.includes('g_updateRefused')),'in an ESP8266 block');
}
// 5. The state lives in an ESP8266 block (no static RAM on the other targets).
assert.ok(esp8266Blocks(portal).some(b=>/static bool g_updateRefused/.test(b)),'g_updateRefused is declared for the ESP8266 only');

console.log('update wiring: all passed');
