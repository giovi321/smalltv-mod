const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const html = fs.readFileSync('src/webui.html', 'utf8');
const start = html.indexOf('function radarLine(r){');
const end = html.indexOf('function wgLine(w){', start);
assert.ok(start >= 0 && end > start);
const ctx = vm.createContext({});
vm.runInContext(html.slice(start, end), ctx);
const line = ctx.radarLine({stage:'parse failed', count:12, http:200,
  parseError:'IncompleteInput', tlsError:7});
for (const text of ['parse failed', 'cached 12 aircraft', 'JSON IncompleteInput', 'TLS error 7']) {
  assert.ok(line.includes(text), line);
}
assert.equal(ctx.radarLine({stage:'ok', count:2}), '2 aircraft, never succeeded');
assert.equal(ctx.radarLine({stage:'idle'}), 'idle');
console.log('radar status UI passed');
