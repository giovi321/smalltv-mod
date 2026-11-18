// A check on the sources: the two build options for forks are wired where
// they must be. The behaviour itself is tested natively (test_build_info.cpp,
// test_ota_off.cpp, tests/status); this file checks what needs the Arduino
// core and so cannot run on the host: the web handlers, the boot sequence,
// and which places show the label and which keep the plain release number.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');

const root=process.env.SRC_ROOT||'src';
const read=f=>fs.readFileSync(path.join(root,f),'utf8');
const body=(text,head)=>{const i=text.indexOf(head);assert.ok(i>=0,'not found: '+head);
 const k=text.indexOf('\n}\n',i);assert.ok(k>i,'end of '+head);return text.slice(i,k)};
// The text between a line `#if <cond>` and its matching `#endif`, nested
// `#if`s counted. One entry per such block.
const guarded=(text,cond)=>{const out=[];const lines=text.split('\n');
 for(let i=0;i<lines.length;i++){
  if(lines[i]!=='#if '+cond)continue;
  let depth=1,j=i+1;
  for(;j<lines.length&&depth>0;j++){
   if(/^#if/.test(lines[j]))depth++;
   else if(/^#endif/.test(lines[j]))depth--;
  }
  assert.equal(depth,0,'#if '+cond+' is never closed');
  out.push(lines.slice(i+1,j-1).join('\n'));
 }
 return out};

const portal=read('WebPortal.cpp');
const main=read('main.cpp');
const net=read('Net.cpp');
const ota=read('OtaUpdate.cpp');
const platform=read('Platform.cpp');
const config=read('config.h');
const info=read('BuildInfo.h');

// 1. The label is the release number plus the suffix, and config.h is not touched
// (the release workflow edits its FW_VERSION line; a neighbour would conflict).
assert.match(info,/^#define FW_VERSION_FULL\s+FW_VERSION\s+FW_VERSION_SUFFIX\s*$/m);
assert.match(info,/^#define FW_VERSION_SUFFIX\s+""\s*$/m);   // the default is no suffix
assert.match(config,/^#define FW_VERSION\s+"\d+\.\d+\.\d+"\s*$/m);
assert.ok(!/BuildInfo|FW_VERSION_/.test(config),'config.h must not mention the build options');

// 2. The label is shown in the status, the serial line, the boot screen and the
// mDNS record, and nowhere else.
assert.match(body(portal,'static void handleStatus()'),/d\.version\s*=\s*FW_VERSION_FULL\s*;/);
assert.ok(main.includes('Serial.println(FW_NAME " " FW_VERSION_FULL)'),'serial line');
assert.match(main,/gfxBoot\(g_safeMode \? "Crashed" : "SmallTV", FW_VERSION_FULL\)/);
assert.match(net,/MDNS\.addServiceTxt\("clawdmeter", "tcp", "ver",\s*FW_VERSION_FULL\)/);
for(const f of fs.readdirSync(root).filter(n=>/\.(cpp|h)$/.test(n)&&n!=='BuildInfo.h'&&n!=='webui.h')){
 const n=(read(f).match(/FW_VERSION_FULL/g)||[]).length;
 const allowed={'WebPortal.cpp':1,'main.cpp':2,'Net.cpp':1}[f]||0;
 assert.equal(n,allowed,f+' uses FW_VERSION_FULL '+n+' times, expected '+allowed);
}

// 3. The plain number stays where it is compared or stored: the update check
// compares FW_VERSION, the crash record keeps it in its 16 byte field.
assert.ok(/verNum\(FW_VERSION\)/.test(ota),'the update check compares the plain number');
assert.ok(!/FW_VERSION_FULL|FW_VERSION_SUFFIX/.test(ota.replace(/^\/\/.*$/gm,'')),'OtaUpdate.cpp must not use the label');
assert.equal((platform.match(/strlcpy\(r\.fw, FW_VERSION, sizeof\(r\.fw\)\)/g)||[]).length,2,'crash record keeps the plain number');
assert.ok(!/FW_VERSION_FULL/.test(platform));
assert.match(body(portal,'static void handleCheckUpdate()'),/o\["current"\]\s*=\s*FW_VERSION\s*;/);

// 4. /api/checkupdate says "disabled" and /api/selfupdate answers 403, both
// before anything is asked of GitHub or queued.
{
 const check=body(portal,'static void handleCheckUpdate()');
 const off=check.indexOf('if (!FW_GITHUB_UPDATE)');
 assert.ok(off>=0,'check handler asks FW_GITHUB_UPDATE');
 assert.ok(off<check.indexOf('otaCheckLatest('),'refused before the GitHub query');
 assert.match(check.slice(off,check.indexOf('otaCheckLatest(')),/server\.send\(200, "application\/json", FW_UPDATE_OFF_CHECK_REPLY\);\s*return;/);
 const self=body(portal,'static void handleSelfUpdate()');
 const soff=self.indexOf('if (!FW_GITHUB_UPDATE)');
 assert.ok(soff>=0,'self-update handler asks FW_GITHUB_UPDATE');
 assert.ok(soff<self.indexOf('g_selfUpdate = true'),'refused before the update is queued');
 assert.match(self.slice(soff,self.indexOf('g_selfUpdate = true')),/server\.send\(403, "application\/json", FW_UPDATE_OFF_SELF_REPLY\);\s*return;/);
 // Both come after the password check: an unauthenticated caller learns nothing.
 assert.ok(check.indexOf('requireAuth()')<off&&self.indexOf('requireAuth()')<soff,'password first');
}

// 5. The status tells the page, in the update-off build only: the field and the
// code that writes it are not in any other image.
assert.match(body(portal,'static void handleStatus()'),/^#ifdef FW_NO_GITHUB_UPDATE\n\s*d\.githubUpdateOff\s*=\s*true\s*;\n#endif$/m);
{
 const st=read('StatusJson.h');
 assert.match(st,/^#ifdef FW_NO_GITHUB_UPDATE\n\s*bool githubUpdateOff = false;[^\n]*\n#endif$/m);
 assert.match(st,/^#ifdef FW_NO_GITHUB_UPDATE\n(?:\s*\/\/[^\n]*\n)*\s*if \(d\.githubUpdateOff\) member\(out, PSTR\(",\\"githubUpdate\\":"\), false\);\n#endif$/m);
}

// 6. The loop's download and the update at boot are not even compiled in.
{
 const loop=body(portal,'void webPortalLoop()');
 const blocks=guarded(loop,'FW_GITHUB_UPDATE');
 assert.equal(blocks.length,1,'one guarded block in webPortalLoop');
 assert.ok(blocks[0].includes('if (g_selfUpdate)')&&blocks[0].includes('otaCheckLatest(*S)')&&blocks[0].includes('otaUpdateFromGitHub(*S)'),
  'the guarded block holds the whole download');
 assert.ok(!loop.replace(blocks[0],'').includes('otaCheckLatest')&&!loop.replace(blocks[0],'').includes('otaUpdateFromGitHub'),
  'no download call outside it');
 const setupBlocks=guarded(main,'FW_GITHUB_UPDATE');
 assert.equal(setupBlocks.length,1,'one guarded block in setup');
 assert.ok(setupBlocks[0].includes('if (otaBootRequested())')&&setupBlocks[0].includes('otaBootUpdate(g_settings)'),
  'the guarded block holds the boot update');
 assert.ok(!main.replace(setupBlocks[0],'').includes('otaBootUpdate('),'no boot update call outside it');
}

// 7. OtaUpdate.cpp: with the update off the file holds only the stubs, and the
// download libraries are not included.
{
 const i=ota.indexOf('#if !FW_GITHUB_UPDATE\n');
 assert.ok(i>=0,'stub section');
 const e=ota.indexOf('\n#else\n',i);
 assert.ok(e>i,'#else after the stubs');
 const stubs=ota.slice(i,e);
 assert.ok(!/HTTPUpdate|HTTPClient|BearSSL|WiFiClientSecure|SecureClient/.test(stubs),'stubs use no download library');
 const rest=ota.slice(e);
 assert.ok(rest.includes('#include <HTTPUpdate.h>')&&rest.includes('verNum(')&&rest.includes('ESPhttpUpdate.update('),'the real code follows the #else');
 assert.ok(ota.trimEnd().endsWith('#endif'),'the file ends with the #endif of the switch');
 assert.ok(!ota.slice(0,i).includes('HTTPUpdate'),'no download include before the switch');
}
console.log('build flag wiring checks passed');
