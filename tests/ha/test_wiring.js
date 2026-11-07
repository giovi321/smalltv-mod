// What the host tests cannot run: setup() and the web handlers need the
// device. This checks, on the sources, that they are wired to the boot-time
// allocation. Each check names the mistake it would catch.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const read=f=>fs.readFileSync(f,'utf8');
// Comments and #include lines do not count.
const strip=t=>t.replace(/\/\*[\s\S]*?\*\//g,'').replace(/\/\/.*$/gm,'').replace(/^#include.*$/gm,'');
const main=strip(read('src/main.cpp'));
const portal=strip(read('src/WebPortal.cpp'));
const screens=strip(read('src/features/ha/HaScreens.cpp'));
const mqtt=strip(read('src/MqttClient.cpp'));
const boot=strip(read('src/features/ha/HaBoot.cpp'));
const at=(t,s)=>{const i=t.indexOf(s);assert.ok(i>=0,'not found: '+s);return i};

// setup(): the allocation comes right after the settings are read, before the
// display, the Wi-Fi and the web server take their memory.
{
 const setup=main.slice(at(main,'void setup()'));
 const load=at(setup,'loadSettings(g_settings)');
 const ha=at(setup,'haBoot(');
 assert.ok(load<ha,'haBoot must come after loadSettings');
 assert.ok(ha<at(setup,'gfxBegin('),'haBoot must come before gfxBegin');
 assert.ok(ha<at(setup,'netBegin('),'haBoot must come before netBegin');
 assert.ok(ha<at(setup,'webPortalBegin('),'haBoot must come before the web server');
 assert.match(setup.slice(ha,ha+80),/haBoot\(haWanted\(g_settings\),\s*g_safeMode\)/,
  'haBoot gets the broker-host rule and the safe-mode flag');
 assert.match(setup.slice(Math.max(0,ha-40),ha),/#if WITH_HA|WITH_HA/,'only in builds with Home Assistant');
}

// No fixed arrays or objects left: the store and the client come from boot.
assert.ok(!/static\s+HaScreen\s+g_screens\s*\[/.test(screens),'g_screens must not be a static array');
assert.ok(!/static\s+PubSubClient\s+g_mqtt\b/.test(mqtt),'the MQTT client must not be a static object');
// Failure must come back as a value, not a panic: nothrow forms only.
for(const [name,t] of [['HaScreens.cpp',screens],['MqttClient.cpp',mqtt]]){
 const news=t.match(/\bnew\b[^;]*/g)||[];
 assert.ok(news.length>0,name+' allocates with new');
 for(const n of news)assert.match(n,/std::nothrow/,name+': every new must be new (std::nothrow): '+n);
}

// Nothing else decides "wanted" with its own rule: the status code and the
// save reply ask haProblem() with the same haWanted() setting rule.
{
 const st=portal.slice(at(portal,'static void handleStatus()'),at(portal,'static String netFingerprint'));
 assert.match(st,/haNote\s*=\s*haProblem\(haWanted\(\*S\)\)/,'handleStatus asks haProblem(haWanted(*S))');
 assert.match(st,/d\.ha\s*=\s*haNote\b/,'handleStatus puts the note into the status field');
 const post=portal.slice(at(portal,'static void handlePostConfig()'),at(portal,'static void handleScan()'));
 assert.match(post,/haNote\s*=\s*haProblem\(haWanted\(\*S\)\)/,'the save asks haProblem(haWanted(*S))');
 assert.match(post,/res\["ha"\]\s*=\s*haNote\b/,'the save reply carries the Home Assistant note');
}

// The loop and the carousel need no change, because a missing store answers
// "no screens"; but the loop must not use the client without one.
{
 const loop=mqtt.slice(at(mqtt,'void mqttLoop()'));
 const guard=at(loop,'if (!g_mqtt) return;');
 assert.ok(guard<at(loop,'g_mqtt->'),'mqttLoop checks for a client before it uses it');
}
// A failed boot must not leave memory taken.
assert.match(boot,/haScreensFree\(\)/,'the boot code gives the store back when the client fails');
console.log('ha wiring tests passed');
