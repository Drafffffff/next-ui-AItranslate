import assert from 'node:assert/strict';
import vm from 'node:vm';
import {build} from 'esbuild';
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import path from 'node:path';
const root=path.resolve(new URL('../../..',import.meta.url).pathname),base=path.join(root,'build/workshop-case-repairs-3d');
const b=await build({entryPoints:[new URL('model.ts',import.meta.url).pathname,new URL('delivery.ts',import.meta.url).pathname],bundle:true,write:false,platform:'neutral',format:'iife',plugins:[{name:'services',setup(b){b.onResolve({filter:/services\.ts$/},()=>({path:'services',namespace:'mock'}));b.onLoad({filter:/.*/,namespace:'mock'},()=>({contents:'export const read=()=>{};export const write=()=>{};export const writeChunks=()=>{};export const task=()=>{};export const delay=()=>{};export const request=()=>{};export const requestStream=undefined;'}));}}],outdir:'/tmp/case-bundle',globalName:'M'});
const ctx=vm.createContext({});let M,D;
for(const file of b.outputFiles){vm.runInContext(file.text,ctx);if(file.path.endsWith('model.js'))M=ctx.M;else D=ctx.M;}
const tests={
 'game-muq20c9n-huje':[
 ['closed-borders',`for seed=0,19 do srand(seed) reset_run() for n=1,9 do assert(grid[1][n]==1 and grid[9][n]==1 and grid[n][1]==1 and grid[n][9]==1,'closed border') end assert(walkable(cx,cy)) assert(walkable(exitx,exity)) local found=0 for it in all(items) do if it.t=='key' then assert(walkable(it.x,it.y)) found+=1 end end assert(found==1 or haskey) end`],
 ['wall-occlusion',`reset_run() state='play' cx=2.5 cy=2.5 cdir=0 mobs={} parts={} items={{x=4.5,y=2.5,t='coin'}} exitx=2 exity=2.5 grid[2][3]=1 eval_sspr=sspr eval_calls=0 sspr=function(sx,sy,sw,sh,dx,dy,dw,dh) eval_calls+=1 assert(sw==8 and sh==8 and dw<24 and dh<24,'single sprite scaling') eval_sspr(sx,sy,sw,sh,dx,dy,dw,dh) end _draw() assert(eval_calls==0,'item behind wall') grid[2][3]=0 _draw() assert(eval_calls>0,'visible item') sspr=eval_sspr`],
 ['perspective-and-order',`local p=project(4.5,3.5,0) assert(abs(p[1]-(64+32/0.66))<.02 and p[4]==2,'camera plane') items={{x=4.5,y=2.5,t='heart'},{x=5.5,y=2.5,t='coin'}} eval_seen_near=false sspr=function(sx,sy,sw,sh,dx,dy,dw,dh) if sx==56 then eval_seen_near=true elseif sx==48 then assert(not eval_seen_near,'far before near') end eval_sspr(sx,sy,sw,sh,dx,dy,dw,dh) end _draw() assert(eval_seen_near) sspr=eval_sspr`],
 ['reset-and-play',`reset_run() assert(hp==3 and not haskey and swordcd==0 and floor_n==1 and walkable(cx,cy))`]
 ]
};
const all=[];
for(const [id,checks] of Object.entries(tests)){
 const p=JSON.parse(readFileSync(path.join(base,id+'.original.json'),'utf8')),lua=readFileSync(path.join(base,id+'.patched.lua'),'utf8');
 assert.equal(D.deliveryDiagnostics(lua,p.audio).errors.length,0,id+' sound checks');
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
