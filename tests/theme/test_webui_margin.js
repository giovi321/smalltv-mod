// The web UI shows why a theme did not run when the device refused it for lack
// of memory (the "themeError" field of /api/status, theme memory margin).
// Property: the text appears in the status box when the field is there, and
// nothing is added when it is not.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const html=fs.readFileSync('src/webui.html','utf8');
const start=html.indexOf('function themeNote(');
assert.ok(start>0,'themeNote() not found in src/webui.html');
const code=html.slice(start,html.indexOf('\n}',start)+2);
const context={};
vm.createContext(context);vm.runInContext(code,context);

assert.equal(context.themeNote(undefined),'');
assert.equal(context.themeNote(''),'');
assert.match(context.themeNote('not enough memory to run this theme'),/not enough memory to run this theme/);
// A text from the device is shown as text, never as markup (the status box is built with innerHTML).
assert.ok(!/[<>]/.test(context.themeNote('<b>x</b>')));

// showStatus() puts it in the status box.
const show=html.slice(html.indexOf('function showStatus('),html.indexOf('\n}',html.indexOf('function showStatus('))+2);
assert.ok(show.includes("themeNote(s.themeError)"),'showStatus() does not use themeNote(s.themeError)');
assert.match(show,/themeNote\(s\.themeError\)\?kv\('Theme'/);
console.log('web UI theme margin tests passed');
