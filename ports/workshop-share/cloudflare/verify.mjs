import assert from 'node:assert/strict';
import {handleRequest, ORIGIN, MAX_UPLOAD_BYTES} from './worker.mjs';

const base = 'https://gateway.example.workers.dev';
const id = 'a'.repeat(64);
let calls = [];
const upstream = async (url, init) => {
  calls.push({url, init});
  return new Response(new Uint8Array([0, 97, 115, 109]), {headers: {
    'Content-Type': 'application/wasm', 'Cache-Control': 'public, max-age=3600',
    'Set-Cookie': 'origin-session=never-forward',
  }});
};
let res = await handleRequest(new Request(base + '/runtime/fake08.wasm', {headers: {
  Authorization: 'Bearer test-only', Cookie: 'private=never-forward', Range: 'bytes=0-3',
}}), upstream);
assert.equal(res.status, 200); assert.deepEqual([...new Uint8Array(await res.arrayBuffer())], [0,97,115,109]);
assert.equal(calls[0].url, ORIGIN + '/runtime/fake08.wasm');
assert.equal(calls[0].init.headers.get('range'), 'bytes=0-3');
assert.equal(calls[0].init.headers.get('authorization'), null);
assert.equal(calls[0].init.headers.get('cookie'), null); assert.equal(res.headers.get('set-cookie'), null);
assert.match(res.headers.get('content-security-policy'), /connect-src 'self'/);
assert.equal(res.headers.get('content-type'), 'application/wasm');

for (const route of ['/', '/g/' + id, '/player.js', '/worker.js', '/api/game?id=' + id,
  '/fonts/workshop-pixel.woff2', '/player.'+'a'.repeat(16)+'.css', '/player.'+'b'.repeat(16)+'.js']) {
  res = await handleRequest(new Request(base + route), upstream); assert.equal(res.status, 200);
  assert.equal(calls.at(-1).url, ORIGIN + route);
}
const payload = '{"cart":"test fixture","revision":1}';
res = await handleRequest(new Request(base + '/api/upload', {method: 'POST', headers: {
  Authorization: 'Bearer fixture-secret', 'Content-Type': 'application/json', Cookie: 'private=blocked',
}, body: payload}), async (url, init) => {
  calls.push({url,init});
  assert.equal(url, ORIGIN + '/api/upload');
  assert.equal(init.headers.get('authorization'), 'Bearer fixture-secret');
  assert.equal(init.headers.get('cookie'), null);
  assert.equal(new TextDecoder().decode(init.body), payload);
  return new Response(JSON.stringify({id, path: '/g/' + id}), {headers: {'Content-Type':'application/json'}});
});
assert.equal(res.headers.get('cache-control'), 'no-store'); assert.equal((await res.json()).path, '/g/' + id);

let unexpected = 0;
const never = async () => { unexpected++; throw Error('must never forward'); };
for (const [path, status] of [
  ['/api/game?id=bad',400], ['/api/game?id='+id+'&id='+id,400],
  ['/api/game?id='+id+'&url=https://example.com',400],
  ['/api/upload?target=https://example.com',400], ['/player.js?target=x',400],
  ['//example.com/game',404], ['/%2f%2fexample.com',404], ['/secret',404],
  ['/player.'+'a'.repeat(16)+'.css?target=x',400], ['/player.invalid.css',404],
]) {
  const options = path.startsWith('/api/upload') ? {method:'POST'} : {};
  assert.equal((await handleRequest(new Request(base+path, options), never)).status, status);
}
assert.equal((await handleRequest(new Request(base+'/api/upload',{method:'POST'}),never)).status,401);
assert.equal((await handleRequest(new Request(base+'/api/upload',{method:'GET'}),never)).status,405);
assert.equal((await handleRequest(new Request(base+'/api/upload',{method:'POST',headers:{Authorization:'Bearer fixture','Content-Type':'text/plain'},body:'x'}),never)).status,415);
const uploadHeaders = {Authorization:'Bearer fixture','Content-Type':'application/json'};
assert.equal((await handleRequest(new Request(base+'/api/upload',{method:'POST',headers:{...uploadHeaders,'Content-Length':String(MAX_UPLOAD_BYTES+1)},body:'x'}),never)).status,413);
let cancelled = false;
const largeStream = new ReadableStream({
  start(c) { c.enqueue(new Uint8Array(MAX_UPLOAD_BYTES)); c.enqueue(new Uint8Array(1)); },
  cancel() { cancelled = true; },
});
assert.equal((await handleRequest(new Request(base+'/api/upload',{method:'POST',headers:uploadHeaders,body:largeStream,duplex:'half'}),never)).status,413);
assert.equal(cancelled,true); assert.equal(unexpected,0);
const aborted = new AbortController(); aborted.abort();
assert.equal((await handleRequest(new Request(base+'/',{signal:aborted.signal}),never)).status,503);
assert.equal(unexpected,0);
const receiving = new AbortController();
let stoppedReading = false;
const slowStream = new ReadableStream({cancel(){stoppedReading=true;}});
const slowResult = handleRequest(new Request(base+'/api/upload',{method:'POST',headers:uploadHeaders,body:slowStream,duplex:'half',signal:receiving.signal}),never);
receiving.abort(); assert.equal((await slowResult).status,503); assert.equal(stoppedReading,true); assert.equal(unexpected,0);
assert.equal((await handleRequest(new Request(base+'/health'),never)).status,200);
assert.equal((await handleRequest(new Request(base+'/'),async()=>{throw Error('TLS reset')})).status,503);
res = await handleRequest(new Request(base+'/'), async()=>new Response(null,{status:302,headers:{Location:ORIGIN}}));
assert.equal(res.status,503); assert.equal(res.headers.get('location'),null);
res = await handleRequest(new Request(base+'/player.js'), async()=>new Response(null,{status:304,headers:{ETag:'fixture'}}));
assert.equal(res.status,304); assert.equal(res.headers.get('etag'),'fixture');
res = await handleRequest(new Request(base+'/api/upload',{method:'POST',headers:uploadHeaders,body:'{}'}),async()=>new Response('{"error":"denied"}',{status:401,headers:{'Content-Type':'application/json'}}));
assert.equal(res.status,401); assert.equal(res.headers.get('cache-control'),'no-store');
console.log('PASS fixed Vercel origin, same-origin game/WASM routes, bounded uploads, credential isolation, redirect rejection, retryable failures and upstream auth preservation');
