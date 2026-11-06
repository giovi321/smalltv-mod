// A check on the sources: every place that decides "is this page locked?"
// must look at the stored password alone, and the two requests that can
// change the password (save and import) must sit behind the login.
// The decision itself is tested in test_settings.cpp; this keeps the places
// that use it from drifting back to a second switch.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');

const root=process.env.SRC_ROOT||'src';
const files=[];
(function walk(d){for(const f of fs.readdirSync(d,{withFileTypes:true})){
 const p=path.join(d,f.name);
 if(f.isDirectory())walk(p);
 else if(/\.(cpp|h|ino)$/.test(f.name)&&f.name!=='webui.h')files.push(p);}})(root);

// 1. No code reads a separate "enabled" switch for the web password.
for(const f of files){
 const text=fs.readFileSync(f,'utf8');
 assert.ok(!/\bauth\s*\.\s*enabled\b/.test(text),f+' still reads auth.enabled');
}

const portal=fs.readFileSync(path.join(root,'WebPortal.cpp'),'utf8');
const body=(text,head)=>{const i=text.indexOf(head);assert.ok(i>=0,'not found: '+head);
 const k=text.indexOf('\n}\n',i);assert.ok(k>i,'end of '+head);return text.slice(i,k)};

// 2. The login check: a stored password, nothing else.
{
 const b=body(portal,'static bool requireAuth() {');
 assert.ok(/if \(!S->auth\.pass\.length\(\)\) return true;/.test(b),'requireAuth looks at the password alone');
 assert.ok(/server\.authenticate\(S->auth\.user\.c_str\(\), S->auth\.pass\.c_str\(\)\)/.test(b),'requireAuth checks the login');
}
// 3. The firmware upload callback runs before the final handler: same rule.
{
 const b=body(portal,'static void handleUpdateUpload() {');
 assert.ok(/if \(S->auth\.pass\.length\(\) &&\s*!server\.authenticate\(/.test(b),'upload callback: password alone, then the login');
}
// 4. The theme upload.
{
 const theme=fs.readFileSync(path.join(root,'features/theme/ThemeWeb.cpp'),'utf8');
 assert.ok(/uploadAllowed = allowed\(\) && \(!settings->auth\.pass\.length\(\) \|\| server->authenticate\(/.test(theme),
  'theme upload: password alone, then the login');
}
// 5. Save and import (both can carry the password, and the remove key) start with the login check.
for(const head of ['static void handlePostConfig() {','static void handleImport() {']){
 const b=body(portal,head);
 assert.ok(b.slice(head.length).trimStart().startsWith('if (!requireAuth()) return;'),head+' must start with requireAuth()');
}
console.log('password wiring tests passed');
