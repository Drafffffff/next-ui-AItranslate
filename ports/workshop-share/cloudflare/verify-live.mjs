import assert from 'node:assert/strict';
import {readFile, mkdir, writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {handleRequest} from './worker.mjs';

const endpoint = process.argv[2];
if (endpoint && (new URL(endpoint).protocol !== 'https:' || new URL(endpoint).pathname !== '/')) throw Error('Expected HTTPS gateway origin');
// This public starter cart is a test fixture, not a user's project.
const knownId = '34f01411a331327a67ff9595cc3ffd96f514fc4899aa6e2731f736394522530a';
const origin = endpoint?.replace(/\/$/, '') || 'https://gateway-fixture.example.workers.dev';
let transportRetries = 0;
const request = async (path, init = {}) => {
  for(let attempt=0;;attempt++) {
    try {
      const req = new Request(origin + path, {...init, signal: AbortSignal.timeout(30000)});
      const response = await (endpoint ? fetch(req) : handleRequest(req));
      if(response.status===503 && attempt<2) { await response.body?.cancel(); transportRetries++; continue; }
      // Consume before declaring success: a truncated binary response must fail.
      const body = [204,205,304].includes(response.status) ? null : await response.arrayBuffer();
      return new Response(body,{status:response.status,headers:response.headers});
    } catch(error) {
      if(attempt>=2)throw error;
      transportRetries++;
    }
  }
};
const checks = [];
const health = await request('/health'); assert.equal(health.status,200); assert.equal((await health.json()).ok,true); checks.push('health');
const game = await request('/api/game?id='+knownId); assert.equal(game.status,200);
const payload = await game.json(); assert.equal(payload.schema,1); assert.match(payload.cart,/^pico-8 cartridge/); checks.push('persistent starter cart');
const page = await request('/g/'+knownId); const html=await page.text();
const versionedAssets=[...html.matchAll(/(?:href|src)="(\/player\.[a-f0-9]{16}\.(?:css|js))"/g)].map(match=>match[1]);
assert.equal(versionedAssets.length,2,'HTML references versioned stylesheet and script');
for(const path of versionedAssets){
 const response=await request(path);assert.equal(response.status,200,path);
 const bytes=new Uint8Array(await response.arrayBuffer());
 assert.equal(createHash('sha256').update(bytes).digest('hex').slice(0,16),path.split('.')[1],path);
}
checks.push('HTML references exact content-versioned CSS and JS');
for (const [route,type] of [
  ['/g/'+knownId,'text/html'], ['/player.js','javascript'], ['/worker.js','javascript'],
  ['/runtime/fake08.js','javascript'], ['/player.css','text/css'],
  ['/fonts/workshop-pixel.woff2','font/woff2'], ['/fonts/LICENSE.txt','text/plain'],
]) {
  const result = await request(route); assert.equal(result.status,200,route);
  assert.ok(result.headers.get('content-type')?.includes(type),route);
  assert.equal(result.headers.get('location'),null,route);
  assert.ok((await result.arrayBuffer()).byteLength>0,route); checks.push(route);
}
const wasm = await request('/runtime/fake08.wasm'); assert.equal(wasm.status,200);
const bytes = new Uint8Array(await wasm.arrayBuffer());
const expected = await readFile(new URL('../public/runtime/fake08.wasm',import.meta.url));
assert.equal(createHash('sha256').update(bytes).digest('hex'),createHash('sha256').update(expected).digest('hex')); checks.push('WASM exact SHA256');
const font = await request('/fonts/workshop-pixel.woff2');
const expectedFont = await readFile(new URL('../public/fonts/workshop-pixel.woff2',import.meta.url));
assert.equal(createHash('sha256').update(new Uint8Array(await font.arrayBuffer())).digest('hex'),createHash('sha256').update(expectedFont).digest('hex')); checks.push('pixel font exact SHA256');
const unauthorized = await request('/api/upload',{method:'POST',headers:{'Content-Type':'application/json',Authorization:'Bearer invalid-fixture'},body:'{}'});
assert.equal(unauthorized.status,401); checks.push('upstream rejects invalid upload key');
if (process.env.WORKSHOP_SHARE_KEY_FILE) {
  const secret = (await readFile(process.env.WORKSHOP_SHARE_KEY_FILE,'utf8')).trim();
  assert.ok(secret.length>=32);
  const upload = await request('/api/upload',{method:'POST',headers:{'Content-Type':'application/json',Authorization:'Bearer '+secret},body:JSON.stringify({title:payload.title,revision:payload.revision,summary:payload.summary,cart:payload.cart})});
  assert.equal(upload.status,200); const result = await upload.json();
  assert.equal(result.id,knownId); assert.equal(result.path,'/g/'+knownId); checks.push('authenticated fixture upload retains content address');
}
await mkdir('build/workshop-cloudflare',{recursive:true});
await writeFile('build/workshop-cloudflare/'+(endpoint?'live':'origin-integration')+'-verification.json',JSON.stringify({
  mode:endpoint?'deployed-cloudflare':'local-gateway-with-real-vercel',endpoint:endpoint||null,
  passed:true,checks,fixtureId:knownId,transportRetries,verifiedAt:new Date().toISOString(),
},null,2)+'\n');
console.log('PASS '+(endpoint?'Cloudflare':'local gateway → real Vercel')+': '+checks.join(', '));
