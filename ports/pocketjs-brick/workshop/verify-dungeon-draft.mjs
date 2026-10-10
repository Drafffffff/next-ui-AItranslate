import assert from 'node:assert/strict';
import vm from 'node:vm';
import {build} from 'esbuild';
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import path from 'node:path';
const root=path.resolve(new URL('../../..',import.meta.url).pathname),base=path.join(root,'build/workshop-dungeon-draft-publish');
const b=await build({entryPoints:[new URL('model.ts',import.meta.url).pathname,new URL('delivery.ts',import.meta.url).pathname],bundle:true,write:false,platform:'neutral',format:'iife',plugins:[{name:'services',setup(b){b.onResolve({filter:/services\.ts$/},()=>({path:'services',namespace:'mock'}));b.onLoad({filter:/.*/,namespace:'mock'},()=>({contents:'export const read=()=>{};export const write=()=>{};export const writeChunks=()=>{};export const task=()=>{};export const delay=()=>{};export const request=()=>{};export const requestStream=undefined;'}));}}],outdir:'/tmp/case-bundle',globalName:'M'});
const ctx=vm.createContext({});let M,D;
for(const file of b.outputFiles){vm.runInContext(file.text,ctx);if(file.path.endsWith('model.js'))M=ctx.M;else D=ctx.M;}
const tests={
 'game-muq26o90-sywc':[
 ['menu-centering',`state='title' eval_print=print eval_text={} print=function(s,x,y,c) add(eval_text,{s=tostr(s),x=x,y=y}) return eval_print(s,x,y,c) end _draw() local title=false local start=false for q in all(eval_text) do if q.s=='dungeon path' then assert(q.x==40) title=true end if q.s=='o: start' then assert(q.y==88 and q.x==48) start=true end end assert(title and start)`],
 ['upgrade-bounds',`state='upgrade' up={'dmg','rate','double'} sel=2 eval_text={} eval_fill=rectfill rectfill=function(x0,y0,x1,y1,c) assert(x0>=0 and y0>=0 and x1<=127 and y1<=127,'panel bounds') eval_fill(x0,y0,x1,y1,c) end _draw() local line=0 for q in all(eval_text) do if q.y==76 then line+=1 end end assert(line==1,'selected option once') print=eval_print rectfill=eval_fill`],
 ['restart',`state='title'`]
 ]
};
const all=[];
for(const [id,checks] of Object.entries(tests)){
 const p=JSON.parse(readFileSync(path.join(base,id+'.original.json'),'utf8')),lua=readFileSync(path.join(base,id+'.patched.lua'),'utf8');
 assert.equal(lua.replace(' music(6,200,3)\n','').split('function _draw()')[0],p.lua.split('function _draw()')[0],'preserve all existing input/physics/upgrade rules');assert.equal(D.deliveryDiagnostics(lua,p.audio).errors.length,0,id+' sound checks');
 const folder=path.join(base,id);mkdirSync(folder,{recursive:true});const cart=path.join(folder,'game.p8');writeFileSync(cart,M.cartridge(lua,p.gfx,p.audio));
 const parser=spawnSync(path.join(root,'build/pocketjs-port/mac-evaluation/workshop-check'),{input:lua,encoding:'utf8'});assert.equal(parser.status,0,parser.stdout+parser.stderr);
 const commands=[];
 for(const [name,source] of checks){const file=path.join(folder,name+'.lua');writeFileSync(file,source);if(id.endsWith('5kr4')){if(name==='soft-drop')commands.push('frames 6 8 soft');if(name==='immediate-move')commands.push('frames 1 2 first');if(name==='das-delay')commands.push('frames 8 2 hold');if(name==='das-repeat')commands.push('frames 2 2 repeat');if(name==='arr-repeat')commands.push('frames 3 2 arr');}commands.push('assert '+file+' '+name);}
 commands.push('frames 120 0 playable');const commandFile=path.join(folder,'commands.txt');writeFileSync(commandFile,commands.join('\n'));
 const r=spawnSync(path.join(root,'build/workshop-eval/fake08-replay'),[cart,commandFile,folder],{encoding:'utf8',timeout:30000});writeFileSync(path.join(folder,'replay.log'),r.stdout+r.stderr);
 assert.equal(r.status,0,r.stdout+r.stderr);const rows=r.stdout.split('\n').filter(s=>s.startsWith('{')).map(JSON.parse);assert(rows.every(r=>r.ok),r.stdout);assert(rows.at(-1).audio_rms>0,id+' PCM must be audible');
 all.push({id,parser:true,sound_refs:true,source_assertions:checks.map(c=>c[0]),runtime_frames:rows.at(-1).frames,audio_rms:rows.at(-1).audio_rms,scope:'scripted source assertions and fixed-core replay; not human playtest'});
 console.log('PASS '+id+' '+checks.map(c=>c[0]).join(', '));
}
writeFileSync(path.join(base,'verification.json'),JSON.stringify(all,null,2));
