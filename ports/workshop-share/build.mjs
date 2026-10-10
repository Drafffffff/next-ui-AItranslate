import {build} from 'esbuild';
import {existsSync} from 'node:fs';
import {readFile,writeFile,readdir,unlink} from 'node:fs/promises';
import {createHash} from 'node:crypto';
if(!existsSync(new URL('public/runtime/fake08.wasm',import.meta.url)))throw Error('Run build-runtime.sh first');
await build({entryPoints:['src/player.ts','src/worker.ts'],outdir:'public',bundle:true,format:'esm',target:'es2020',minify:true,external:['/runtime/*']});
// HTML revalidates; content-addressed assets bypass a previous release's browser cache.
for(const name of await readdir('public'))if(/^player\.[a-f0-9]{16}\.(css|js)$/.test(name))await unlink('public/'+name);
let html=await readFile('public/index.html','utf8');
for(const extension of ['css','js']){
 const bytes=await readFile('public/player.'+extension);
 const name='player.'+createHash('sha256').update(bytes).digest('hex').slice(0,16)+'.'+extension;
 await writeFile('public/'+name,bytes);
 html=html.replace(new RegExp('/player(?:\\.[a-f0-9]{16})?\\.'+extension,'g'),'/'+name);
}
await writeFile('public/index.html',html);
