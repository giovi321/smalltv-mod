// The place search and the sun settings of the settings page, run with a
// small fake DOM (like tests/theme/test_webui.js). No browser needed.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const html = fs.readFileSync('src/webui.html', 'utf8');
const begin = html.indexOf('// sun begin');
const end = html.indexOf('// sun end');
assert.ok(begin > 0 && end > begin, 'markers "// sun begin" / "// sun end" not found in webui.html');
const code = html.slice(begin, end);

const ids = ['locLat', 'locLon', 'locQ', 'locRes', 'locMsg', 'sunEnabled', 'autoBrightness',
  'radarUseDevice', 'radarLat', 'radarLon', 'slVal', 'sunLow'];
const nodes = {};
for (const id of ids) nodes[id] = { value: '', textContent: '', checked: false, disabled: false, style: {}, children: [],
  appendChild(c) { this.children.push(c); }, set innerHTML(v) { this.children = []; }, get innerHTML() { return ''; } };
let fetched = [], reply = null, fail = false;
const context = {
  $: id => nodes[id], encodeURIComponent, JSON, Math, Number, parseFloat, parseInt, isNaN,
  document: { createElement: () => ({ value: '', textContent: '' }) },
  sv: (id, v) => { nodes[id].value = v != null ? v : ''; },
  sc: (id, v) => { nodes[id].checked = !!v; },
  gv: id => nodes[id].value, gc: id => nodes[id].checked,
  fetch: async url => { fetched.push(url); if (fail) throw new Error('offline'); return { ok: true, json: async () => reply }; },
};
vm.createContext(context);
vm.runInContext(code, context);

(async () => {
  // labels: name, region, country; missing parts are left out
  assert.equal(context.placeLabel({ name: 'Freiburg im Breisgau', admin1: 'Baden-Wurttemberg', country: 'Germany' }),
    'Freiburg im Breisgau, Baden-Wurttemberg, Germany');
  assert.equal(context.placeLabel({ name: 'Nowhere' }), 'Nowhere');

  // the search asks Open-Meteo's place search for 5 matches, with the name encoded
  nodes.locQ.value = 'Sankt Gallen/ü';
  reply = { results: [
    { name: 'Berlin', admin1: 'Berlin', country: 'Germany', latitude: 52.52437, longitude: 13.41053 },
    { name: 'Berlin', admin1: 'New Hampshire', country: 'United States', latitude: 44.46867, longitude: -71.18508 }] };
  await context.placeSearch();
  assert.equal(fetched.length, 1);
  assert.match(fetched[0], /^https:\/\/geocoding-api\.open-meteo\.com\/v1\/search\?name=Sankt%20Gallen%2F%C3%BC&count=5&/);
  assert.equal(nodes.locRes.children.length, 2);
  assert.match(nodes.locRes.children[1].textContent, /New Hampshire/);
  assert.notEqual(nodes.locRes.style.display, 'none');

  // picking a match fills in latitude and longitude with 4 decimals (the radar
  // follows this location and used to take 4: about 10 m, not 1 km)
  nodes.locRes.value = '0';
  context.placePick();
  assert.equal(nodes.locLat.value, '52.5244');
  assert.equal(nodes.locLon.value, '13.4105');
  nodes.locRes.value = '1';
  context.placePick();
  assert.equal(nodes.locLat.value, '44.4687');
  assert.equal(nodes.locLon.value, '-71.1851');

  // the hints name every setting that starts NTP, and do not overstate night mode
  assert.match(html, /enable night mode, the sun schedule, a theme clock, or WireGuard to start NTP/);
  assert.ok(!html.includes('it overrides the brightness and auto-brightness above.'),
    'night-mode hint must not say it always overrides');
  assert.match(html, /except when the sun schedule below is on: then its "When night mode is also on" choice decides/);
  assert.match(html, /Use the device location \(Display tab, Clock &amp; night mode card\)/);

  // the fields accept 4 decimals: the browser rejects a number off the step
  assert.match(html, /id="locLat" type="number" step="0\.0001"/);
  assert.match(html, /id="locLon" type="number" step="0\.0001"/);
  // an empty search sends nothing
  nodes.locQ.value = '   ';
  await context.placeSearch();
  assert.equal(fetched.length, 1);

  // no match: say so, show no list
  nodes.locQ.value = 'zzzz';
  reply = {};
  await context.placeSearch();
  assert.match(nodes.locMsg.textContent, /No place found/);
  assert.equal(nodes.locRes.style.display, 'none');

  // browser offline: say so
  fail = true;
  await context.placeSearch();
  assert.match(nodes.locMsg.textContent, /Search failed/);
  fail = false;

  // sun schedule and light sensor exclude each other
  nodes.autoBrightness.checked = true;
  nodes.sunEnabled.checked = true;
  context.sunToggled();
  assert.equal(nodes.autoBrightness.checked, false);
  nodes.sunEnabled.checked = false;
  context.sunToggled();
  nodes.autoBrightness.checked = true;
  nodes.sunEnabled.checked = true;
  context.autoToggled();
  assert.equal(nodes.sunEnabled.checked, false);
  assert.equal(nodes.autoBrightness.checked, true);

  // the radar's own coordinates are only editable when it does not use the device location
  nodes.radarUseDevice.checked = true;
  context.radarLocChanged();
  assert.equal(nodes.radarLat.disabled, true);
  assert.equal(nodes.radarLon.disabled, true);
  nodes.radarUseDevice.checked = false;
  context.radarLocChanged();
  assert.equal(nodes.radarLat.disabled, false);

  // reading the numbers: 0 is a real value for the fades and the lowest level
  assert.equal(context.numIn('0', 60, 0, 120), 0);
  assert.equal(context.numIn('', 60, 0, 120), 60);
  assert.equal(context.numIn('500', 60, 0, 120), 120);
  assert.equal(context.numIn('-3', 60, 0, 120), 0);
  assert.equal(context.numIn('45', 60, 0, 120), 45);

  console.log('sun web UI tests passed');
})().catch(e => { console.error(e); process.exit(1); });
