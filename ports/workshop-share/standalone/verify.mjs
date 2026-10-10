import assert from 'node:assert/strict';
import {mkdtemp,rm,readFile,readdir,writeFile} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {createHash} from 'node:crypto';
import {request as httpRequest} from 'node:http';
import {once} from 'node:events';
import {createService} from '../../../build/workshop-native-share/server.mjs';

const dir=await mkdtemp(join(tmpdir(),'workshop-native-share-'));
const key='fixture-only-'.repeat(4), publicDir=new URL('../public/',import.meta.url).pathname;
let server,base;
const checks=[];
async function start() {
  server=await createService({gameDir:dir,publicDir,uploadKey:key});
  server.listen(0,'127.0.0.1');await once(server,'listening');base='http://127.0.0.1:'+server.address().port;
}
async function stop() {const stopped=once(server,'close');server.close();server.closeIdleConnections();await stopped;}
const options=body=>({method:'POST',headers:{Authorization:'Bearer '+key,'Content-Type':'application/json'},body:JSON.stringify(body)});
const cart='pico-8 cartridge // http://www.pico-8.com\nversion 42\n__lua__\nfunction _draw() cls(1) print("fixture",20,20,7) end\n__gfx__\n'+'0'.repeat(128)+'\n';
const payload={title:'原生直连测试',revision:1,summary:'独立服务器测试卡带',cart};
try {
  await assert.rejects(createService({gameDir:dir,publicDir,uploadKey:''}));
  await start();
  assert.equal((await (await fetch(base+'/health')).json()).ok,true);
  let res=await fetch(base+'/api/upload',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});
  assert.equal(res.status,401);assert.deepEqual(await readdir(dir),[]);checks.push('unauthorized upload stores nothing');
  res=await fetch(base+'/api/upload',options(payload));assert.equal(res.status,200);
  const result=await res.json();assert.match(result.id,/^[a-f0-9]{64}$/);assert.equal(result.path,'/g/'+result.id);
  const raw=await readFile(join(dir,result.id+'.json'));
  assert.equal(createHash('sha256').update(raw).digest('hex'),result.id);
  assert.equal(JSON.parse(raw).cart,cart);checks.push('authenticated atomic content-addressed upload');
  const reader=await fetch(base+'/api/game?id='+result.id);assert.equal(reader.status,200);assert.deepEqual(Buffer.from(await reader.arrayBuffer()),raw);
  assert.match(reader.headers.get('cache-control'),/immutable/);
  res=await fetch(base+result.path);assert.equal(res.status,200);const html=await res.text();assert.match(res.headers.get('content-security-policy'),/connect-src 'self'/);
  assert.equal(res.headers.get('cache-control'),'public, max-age=0, must-revalidate');
  const versioned=[...html.matchAll(/(?:href|src)="(\/player\.[a-f0-9]{16}\.(?:css|js))"/g)].map(x=>x[1]);assert.equal(versioned.length,2);
  for(const path of ['/player.js','/worker.js','/runtime/fake08.js','/runtime/fake08.wasm','/player.css','/fonts/workshop-pixel.woff2',...versioned]) {
    res=await fetch(base+path);assert.equal(res.status,200,path);
    assert.deepEqual(Buffer.from(await res.arrayBuffer()),await readFile(new URL('../public'+path,import.meta.url)),path);
  }
  checks.push('same-origin HTML, scripts and exact WASM bytes');
  res=await fetch(base+'/runtime/fake08.wasm',{method:'HEAD'});assert.equal(res.status,200);assert.equal((await res.arrayBuffer()).byteLength,0);assert.equal(res.headers.get('content-type'),'application/wasm');
  for(const [path,status] of [['/api/game?id=bad',400],['/api/game?id='+result.id+'&id='+result.id,400],['/api/game?id='+'b'.repeat(64),404],['/server.mjs',404],['/runtime/%2f..%2fserver.mjs',404],['/player.js?target=x',400]])assert.equal((await fetch(base+path)).status,status,path);
  res=await fetch(base+'/api/upload',options({...payload,cart:'invalid'}));assert.equal(res.status,400);
  res=await fetch(base+'/api/upload',{method:'POST',headers:{Authorization:'Bearer '+key,'Content-Type':'text/plain'},body:'x'});assert.equal(res.status,415);
  res=await fetch(base+'/api/upload',options({...payload,cart:cart+'x'.repeat(3*1024*1024)}));assert.equal(res.status,400);
  res=await fetch(base+'/api/upload',{method:'POST',headers:{Authorization:'Bearer '+key,'Content-Type':'application/json'},body:'x'.repeat(6*1024*1024+1)});assert.equal(res.status,413);
  checks.push('body/cart budgets and route/traversal/method rejection');
  const concurrent=await Promise.all(Array.from({length:12},()=>fetch(base+'/api/upload',options(payload))));
  assert.ok(concurrent.every(x=>x.status===200));assert.deepEqual(await readdir(dir),[result.id+'.json']);
  await stop();await start();
  res=await fetch(base+'/api/game?id='+result.id);assert.equal(res.status,200);assert.equal((await res.json()).cart,cart);
  checks.push('concurrent identical uploads and persistence after restart');
  await writeFile(join(dir,result.id+'.json'),'corrupted');
  res=await fetch(base+'/api/game?id='+result.id);assert.equal(res.status,503);
  res=await fetch(base+'/api/upload',options(payload));assert.equal(res.status,200);checks.push('corrupt stored cart rejected and exact reupload repairs it');
  await new Promise((resolveAbort,reject)=>{
    const req=httpRequest(base+'/api/upload',{method:'POST',headers:{Authorization:'Bearer '+key,'Content-Type':'application/json','Content-Length':3000}});
    req.on('error',()=>resolveAbort());req.on('response',r=>{r.resume();resolveAbort();});
    req.write('{"title":"incomplete');setTimeout(()=>req.destroy(),20);
    const timeout=setTimeout(()=>reject(Error('abort did not finish')),1000);timeout.unref();
  });
  await new Promise(r=>setTimeout(r,30));assert.deepEqual(await readdir(dir),[result.id+'.json']);checks.push('interrupted upload cannot publish a partial cart');
  console.log('PASS independent server: '+checks.join('; '));
  await writeFile(new URL('../../../build/workshop-native-share/verification.json',import.meta.url),JSON.stringify({passed:true,checks,verifiedAt:new Date().toISOString()},null,2)+'\n');
} finally {if(server?.listening)await stop();await rm(dir,{recursive:true,force:true});}
