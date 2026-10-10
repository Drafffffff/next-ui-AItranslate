import assert from 'node:assert/strict';
import {readFile,mkdtemp,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {build} from 'esbuild';
import initialize from './public/runtime/fake08.js';
const dir=await mkdtemp(join(tmpdir(),'workshop-share-'));
try{
 await build({entryPoints:['api/upload.ts'],outfile:join(dir,'upload.cjs'),bundle:true,platform:'node',format:'cjs',external:['@vercel/blob']});
 // The bundled module resolves its only runtime dependency from this project's node_modules.
 const code=await readFile(join(dir,'upload.cjs'),'utf8');
 const module={exports:{}};
 const {createRequire}=await import('node:module');
 const require=createRequire(import.meta.url);
 new Function('require','module','exports',code)(require,module,module.exports);
 const {authorized,validateUpload}=module.exports;
 assert.equal(authorized('Bearer '+'a'.repeat(48),'a'.repeat(48)),true);
 for(const header of [undefined,'Bearer wrong','Bearer '+ 'b'.repeat(48)])assert.equal(authorized(header,'a'.repeat(48)),false);
 assert.equal(authorized('Bearer x','x'),false);
 await build({stdin:{contents:'import {STARTER} from "../pocketjs-brick/workshop/starter.ts";import {BUILTIN_GFX} from "../pocketjs-brick/workshop/skill.ts";import {BUILTIN_AUDIO,audioSections} from "../pocketjs-brick/workshop/audio.ts";export const cart="pico-8 cartridge // http://www.pico-8.com\\nversion 42\\n__lua__\\n"+STARTER+"\\n__gfx__\\n"+BUILTIN_GFX+"\\n"+audioSections(BUILTIN_AUDIO);',resolveDir:process.cwd(),loader:'ts'},outfile:join(dir,'cart.cjs'),bundle:true,platform:'node',format:'cjs'});
 const {createRequire:makeRequire}=await import('node:module');const {cart}=makeRequire(import.meta.url)(join(dir,'cart.cjs'));
 const input={title:' 测试 ',revision:1,summary:'hello',cart,messages:[{secret:'excluded'}]};
 const first=validateUpload(input),same=validateUpload(input);
 assert.equal(first.id,same.id);assert.match(first.id,/^[a-f0-9]{64}$/);assert.equal(JSON.parse(first.raw).title,'测试');assert(!first.raw.includes('excluded'));
 for(const invalid of [{...input,revision:-1},{...input,title:''},{...input,cart:'<script>bad</script>'},{...input,cart:cart+'a'.repeat(3*1024*1024)}])assert.throws(()=>validateUpload(invalid));
 const core=await initialize({wasmBinary:await readFile('public/runtime/fake08.wasm')});
 const load=(text)=>{const bytes=new TextEncoder().encode(text),at=core._malloc(bytes.length);core.HEAPU8.set(bytes,at);try{return core._web_load(at,bytes.length);}finally{core._free(at);}};
 console.log('WASM starter load');assert([30,60].includes(load(cart)));console.log('WASM starter frames');
 let audible=false,frameHashes=new Set();
 for(let n=0;n<90;n++){
  const samples=core._web_step(n===0?16:n<30?2:0);assert(samples>0);
  const frame=core.HEAPU8.slice(core._web_pixels(),core._web_pixels()+65536);
  assert.equal(frame[3],255);
  frameHashes.add(Buffer.from(frame).toString('base64'));
  const at=core._web_audio()/2;
  audible ||= core.HEAP16.slice(at,at+samples*2).some(n=>n!==0);
 }
 assert(frameHashes.size>1,'actual VM renders changing frames');assert(audible,'starter BGM produces real PCM');
 const simple='pico-8 cartridge // http://www.pico-8.com\nversion 42\n__lua__\nfunction _init() x=10 end\nfunction _update() if btn(1) then x+=1 end end\nfunction _draw() cls(0) pset(x,20,8) end\n__gfx__\n'+('0'.repeat(128)+'\n').repeat(128);
 console.log('WASM basic load');assert.equal(load(simple),30);core._web_step(0);
 const initial=Buffer.from(core.HEAPU8.slice(core._web_pixels(),core._web_pixels()+65536));
 core._web_step(2);assert.notDeepEqual(Buffer.from(core.HEAPU8.slice(core._web_pixels(),core._web_pixels()+65536)),initial,'input reaches VM');
 console.log('WASM restart');load(simple);core._web_step(0);assert.deepEqual(Buffer.from(core.HEAPU8.slice(core._web_pixels(),core._web_pixels()+65536)),initial,'restart resets state');
 assert.equal(load(simple.replace('x=10','x=)')),0,'syntax failure must be reported without breaking the interpreter');
 console.log('PASS: upload authentication/size/content hash/privacy; actual FAKE-08 WASM render, input, restart, PCM and syntax errors.');
}finally{await rm(dir,{recursive:true,force:true});}
