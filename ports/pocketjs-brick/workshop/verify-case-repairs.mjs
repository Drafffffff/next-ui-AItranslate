import assert from 'node:assert/strict';
import vm from 'node:vm';
import {build} from 'esbuild';
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import path from 'node:path';
const root=path.resolve(new URL('../../..',import.meta.url).pathname),base=path.join(root,'build/workshop-case-repairs');
const b=await build({entryPoints:[new URL('model.ts',import.meta.url).pathname,new URL('delivery.ts',import.meta.url).pathname],bundle:true,write:false,platform:'neutral',format:'iife',plugins:[{name:'services',setup(b){b.onResolve({filter:/services\.ts$/},()=>({path:'services',namespace:'mock'}));b.onLoad({filter:/.*/,namespace:'mock'},()=>({contents:'export const read=()=>{};export const write=()=>{};export const writeChunks=()=>{};export const task=()=>{};export const delay=()=>{};export const request=()=>{};export const requestStream=undefined;'}));}}],outdir:'/tmp/case-bundle',globalName:'M'});
const ctx=vm.createContext({});let M,D;
for(const file of b.outputFiles){vm.runInContext(file.text,ctx);if(file.path.endsWith('model.js'))M=ctx.M;else D=ctx.M;}
const tests={
 'game-muqioi9l-yczh':[
 ['scenes-and-pickups','reset() assert(#enemies==3) open_cave(10,4) open_cave(10,4) assert(#pickups==1) open_cave(8,19) open_cave(20,12) assert(#pickups==3) entered_dungeon=true assert(not blocked(48,48)) assert(blocked(7,48)) assert(blocked(113,48)) reset() assert(not next(opened_caves))'],
 ['camera-and-hud','state="play" player.x=176 player.y=96 shake=0 inv=0 eval_camera=camera camera=function(x,y) eval_cx=x or 0 eval_cy=y or 0 eval_camera(x,y) end eval_hero=draw_hero draw_hero=function() assert(eval_cx==116 and eval_cy==36) eval_hero() end _draw() assert(eval_cx==0 and eval_cy==0) camera=eval_camera draw_hero=eval_hero'],
 ['dungeon-reset','start_dungeon() assert(#enemies==5 and #pickups==0 and #swings==0 and player.atk==0) assert(not blocked(player.x,player.y)) reset() assert(not entered_dungeon and shards==0 and #enemies==3) state="play" inv=300']
 ],
 'game-muq0mtg0-ecul':[
 ['drivable-loop','reset() assert(pa==0.5) for x=12,116,8 do assert(not kart_blocked(x,12)) assert(not kart_blocked(x,116)) end for y=12,116,8 do assert(not kart_blocked(12,y)) assert(not kart_blocked(116,y)) end for c in all(checkpoints) do assert(not kart_blocked(c[1],c[2])) end assert(kart_blocked(2,2))'],
 ['laps-and-shortcut','state="play" t=90 lastx=21 px=19 py=116 advance_track() assert(lap==0) for n=1,3 do for c in all(checkpoints) do px=c[1] py=c[2] advance_track() end assert(checkpoint==5) lastx=19 px=21 py=116 advance_track() assert(lap==n-1) lastx=21 px=19 advance_track() assert(lap==n) end assert(state=="over" and best==90) reset() assert(lap==0 and checkpoint==1 and t==0) state="play"']
 ],
 'game-muq16s15-5kr4':[
 ['prepare','reset() cur={t=2,m=mats[2],col=7,px=4,py=1} tick=0 eval_py=cur.py'],
 ['soft-drop','assert(cur.py==eval_py+3) reset() cur={t=2,m=mats[2],col=7,px=4,py=1} tick=0'],
 ['immediate-move','assert(cur.px==5)'],['das-delay','assert(cur.px==5)'],['das-repeat','assert(cur.px==6)'],['arr-repeat','assert(cur.px==7)']
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
