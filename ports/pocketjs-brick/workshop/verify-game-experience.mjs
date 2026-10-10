// Development-only: actual pinned VM input/audio, not a handheld Agent tool.
import assert from 'node:assert/strict';
import vm from 'node:vm';
import path from 'node:path';
import {readFileSync,writeFileSync,mkdirSync,existsSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {build} from 'esbuild';
const dir=new URL('.',import.meta.url).pathname,root=path.resolve(dir,'../../..');
const replay=process.env.WORKSHOP_REPLAY||path.join(root,'build/workshop-eval/fake08-replay');
assert(existsSync(replay),'Build the pinned fake08-replay before running experience regressions');
const b=await build({entryPoints:[path.join(dir,'model.ts')],bundle:true,write:false,platform:'neutral',format:'iife',globalName:'M',plugins:[{name:'no-service',setup(b){b.onResolve({filter:/services\.ts$/},()=>({path:'native',namespace:'mock'}));b.onLoad({filter:/.*/,namespace:'mock'},()=>({contents:'export const read=()=>{};export const write=()=>{};export const writeChunks=()=>{};export const task=()=>{};export const delay=()=>{};export const request=()=>{};export const requestStream=undefined;'}));}}]});
const context=vm.createContext({});vm.runInContext(b.outputFiles[0].text,context);
const project=context.M.newProject(),skill=path.join(root,'skills/pico8-game-dev');
const helper=readFileSync(path.join(skill,'assets/snippets/grid-input.lua'),'utf8'),example=readFileSync(path.join(skill,'assets/examples/sokoban.lua'),'utf8');
assert(example.startsWith(helper),'The complete example must contain the exact tested helper');
const output=path.join(root,'build/workshop-game-experience');mkdirSync(output,{recursive:true});
const cases=[{id:'sandbox-api',lua:'function _init() bag={1,2,3} last=deli(bag,#bag) end\nfunction _update() end\nfunction _draw() cls(0) end',steps:[
 ['namespace-boundary','assert(table==nil and math==nil and string==nil,"standard Lua namespaces are not cart APIs")'],
 ['bag-helper','assert(last==3 and #bag==2 and bag[2]==2,"deli must pop and compact the bag")']
 ]},{id:'grid-example',lua:example,steps:[
 ['observe','eval_actions=0 eval_sounds=0 eval_music=0 local m=music local sf=sfx local st=step step=function(...) eval_actions+=1 st(...) end music=function(...) eval_music+=1 m(...) end sfx=function(n,...) if n>=0 then eval_sounds+=1 end sf(n,...) end'],
 [1,2,'first'],['immediate','assert(px==3 and moves==1 and eval_actions==1 and eval_sounds==1)'],
 [5,2,'delay'],['delay-keeps-state','assert(px==3 and eval_actions==1 and eval_sounds==1)'],
 [1,2,'repeat'],['repeat-action','assert(px==4 and moves==2 and eval_actions==2 and eval_sounds==2) board[4][7]=1'],
 [3,2,'blocked'],['illegal-action','assert(moves==2 and eval_actions==3 and eval_sounds==2 and #history==2)'],
 [1,0,'release'],[1,32,'undo'],['undo-state','assert(px==3 and moves==1 and box_at(4,3))'],
 [1,0,'release-undo'],[1,16,'restart'],['restart-state','assert(moves==0 and px==2 and #history==0) eval_music=0'],
 [60,16,'held-restart'],['single-restart','assert(moves==0 and eval_music==0)']
 ]},
 {id:'direction-helper',lua:helper+'\nfunction _init() grid_input_reset() eval_events="" eval_frame=0 end\nfunction _update() eval_frame+=1 local d=grid_request(6,3) if d>=0 then eval_events..=eval_frame..":"..d.."," end end\nfunction _draw() cls(0) end',steps:[
 [13,2,'hold'],['cadence','assert(eval_events=="1:1,7:1,10:1,13:1,",eval_events)'],
 [1,0,'release'],[1,2,'press'],['released-immediate','assert(sub(eval_events,-5)=="15:1,")'],
 [1,6,'diagonal'],['new-direction','assert(sub(eval_events,-5)=="16:2,")'],
 [1,3,'opposites'],['cancel-opposites','assert(grid_dir==-1)'],
 [1,11,'other-axis'],['valid-other-axis','assert(sub(eval_events,-5)=="18:3,")']
 ]}];
const records=[];
for(const c of cases){const out=path.join(output,c.id);mkdirSync(out,{recursive:true});const cart=path.join(out,'game.p8');writeFileSync(cart,context.M.cartridge(c.lua,project.gfx,project.audio));const commands=[];
 for(const step of c.steps){if(typeof step[0]==='number')commands.push(`frames ${step[0]} ${step[1]} ${step[2]}`);else{const f=path.join(out,step[0]+'.lua');writeFileSync(f,step[1]);commands.push(`assert ${f} ${step[0]}`);}}
 const script=path.join(out,'replay.txt');writeFileSync(script,commands.join('\n'));
 const r=spawnSync(replay,[cart,script,out],{encoding:'utf8',timeout:30000});writeFileSync(path.join(out,'result.log'),r.stdout+r.stderr);assert.equal(r.status,0,r.stdout+r.stderr);const events=r.stdout.trim().split('\n').map(JSON.parse);assert(events.every(e=>e.ok),r.stdout);
 records.push({id:c.id,events,scope:'Pinned fake-08 VM logical frames; core PCM, not frontend latency or listening quality'});console.log('PASS actual VM '+c.id+': '+events.filter(e=>e.kind==='assert').map(e=>e.name).join(', '));
}
writeFileSync(path.join(output,'summary.json'),JSON.stringify(records,null,2));
