// A check on the sources: GET /api/status (WebPortal.cpp) writes its reply
// through a ChunkedSink, and the chunk framing depends on the request. Both
// web server cores chunk a reply of unknown length only for an HTTP/1.1 client
// (_prepareHeader: `_contentLength == CONTENT_LENGTH_UNKNOWN && _currentVersion`),
// so to an HTTP/1.0 client the sink must write the plain bytes and the handler
// must not send the last chunk. The sink itself is tested in
// test_status_stall.cpp; what only the device runs is this wiring.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');

const root=process.env.SRC_ROOT||'src';
const portal=fs.readFileSync(path.join(root,'WebPortal.cpp'),'utf8');
const body=(text,head)=>{const i=text.indexOf(head);assert.ok(i>=0,'not found: '+head);
 const k=text.indexOf('\n}\n',i);assert.ok(k>i,'end of '+head);return text.slice(i,k)};

// 1. The web server object can say whether the request is HTTP/1.0, from the
//    core's own version member (0 is "1.0"), and `server` is that object.
{
 const at=portal.indexOf('class PortalServer : public WebServerClass {');
 assert.ok(at>=0,'PortalServer');
 const cls=portal.slice(at,portal.indexOf('\n};',at));
 assert.ok(/bool requestIsHttp10\(\) const \{ return _currentVersion == 0; \}/.test(cls),
   'requestIsHttp10() reads _currentVersion == 0');
 assert.ok(/static PortalServer\s+server\(80\);/.test(portal),'server is a PortalServer');
}

// 2. The handler hands the sink "framed" = not HTTP/1.0, and ends the reply
//    with the last chunk only when it is chunked.
{
 const handler=body(portal,'static void handleStatus() {');
 assert.ok(/ChunkedSink<StatusClient>\s+sink\(client,\s*!server\.requestIsHttp10\(\)\);/.test(handler),
   'the sink is framed unless the request is HTTP/1.0');
 assert.ok(/if \(smalltv::endReply\(out, sink\) && !server\.requestIsHttp10\(\)\) server\.sendContent\(""/.test(handler),
   'the last chunk is sent only for a chunked reply');
 // The headers still go out first, with the length unknown (the core then
 // adds Transfer-Encoding for HTTP/1.1 and nothing for HTTP/1.0).
 const unknown=handler.indexOf('server.setContentLength(CONTENT_LENGTH_UNKNOWN);');
 const send=handler.indexOf('server.send(200, "application/json", "");');
 const sink=handler.indexOf('ChunkedSink<StatusClient>');
 assert.ok(unknown>=0&&send>unknown&&sink>send,'length unknown, then the headers, then the sink');
}
console.log('status wiring tests passed');
