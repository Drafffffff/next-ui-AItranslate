// Actual Pi execution with isolated transport/storage: publication follows current evidence.
import assert from 'node:assert/strict';
import vm from 'node:vm';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {build} from 'esbuild';
const dir=path.dirname(fileURLToPath(import.meta.url));
const clone=o=>JSON.parse(JSON.stringify(o));
const files=new Map(),roms=new Map();let steps=[],requests=0,id=0,failPublish=false;
const complete=(name,args={})=>({status:200,text:JSON.stringify({choices:[{message:{role:'assistant',content:name?null:'根据实际工具结果回复。',...(name?{tool_calls:[{id:'harness-'+(++id),type:'function',function:{name,arguments:JSON.stringify(args)}}]}:{})},finish_reason:name?'tool_calls':'stop'}]})});
const harness={read:async n=>files.get(n),write:async(n,text)=>files.set(n,text),task:input=>{if(input.op==='syntax')return {promise:Promise.resolve({text:'syntax_checked'})};if(failPublish){failPublish=false;return {promise:Promise.reject(new Error('fixture publication interrupted'))};}roms.set(input.name,input.text);return {promise:Promise.resolve({text:'published'})};},request:(url,key,body)=>{requests++;const value=JSON.parse(body);assert(steps.length,'Unexpected model request');return {promise:Promise.resolve(steps.shift()(value)),cancel(){}};}};
const bundle=await build({entryPoints:[path.join(dir,'model.ts')],bundle:true,write:false,platform:'neutral',format:'iife',globalName:'Model',plugins:[{name:'native',setup(b){b.onResolve({filter:/services\.ts$/},()=>({path:'native',namespace:'native'}));b.onLoad({filter:/.*/,namespace:'native'},()=>({contents:'export const read=(...a)=>globalThis.__harness.read(...a);export const write=(...a)=>globalThis.__harness.write(...a);export const task=(...a)=>globalThis.__harness.task(...a);export const writeChunks=async(n,produce)=>{const parts=[];await produce(p=>parts.push(p));return write(n,parts.join(""));};export const delay=async()=>{};export const requestStream=undefined;export const request=(...a)=>globalThis.__harness.request(...a);'}));}}]});
const ctx=vm.createContext({__harness:harness});vm.runInContext(bundle.outputFiles[0].text,ctx);
const {newProject,projectAgent,recoverProject,STARTER,cartridge,PublishedReplyError}=ctx.Model;
const config={key:'fixture',endpoint:'https://example.invalid',model:'deepseek-flash',timeoutMs:10000};
const result=(body,name)=>{const a=body.messages.filter(m=>m.tool_calls?.some(c=>c.function.name===name)).at(-1);const call=a.tool_calls.find(c=>c.function.name===name);const raw=body.messages.find(m=>m.role==='tool'&&m.tool_call_id===call.id).content;try{return JSON.parse(raw);}catch{return {error:raw};}};
const call=(name,args={})=>()=>complete(name,args);
const expectError=(name,pattern,next,args={})=>body=>{assert.match(result(body,name).error,pattern);return complete(next,args);};
async function run(p,script,published=false,agent){steps=script;const work=(agent||projectAgent(p,config,()=>{},()=>{})).prompt('修改当前游戏');if(published)await work;else await assert.rejects(work,/尚未提交/);assert.equal(steps.length,0);}
const p=newProject();
await run(p,[call('publish_game'),expectError('publish_game',/check_game/)],false);
assert.equal(p.revision,0);assert.equal(roms.size,0);
await run(p,[call('read_game'),body=>{assert.equal(result(body,'read_game').draft.differs_from_published,true);return complete('write_game',{lua:STARTER,title:'原游戏',summary:'原规则'});},call('check_game'),call('publish_game'),call('read_game'),body=>{const current=result(body,'read_game');assert.equal(current.published.revision,1);assert.equal(current.published.title,'原游戏');assert.equal(current.draft.differs_from_published,false);return complete();}],true);
const originalCart=roms.get(p.id),gfx=p.gfx,audio=clone(p.audio);
// A title-only draft invalidates a prior check without changing the menu cartridge.
const replacement={lua:p.lua,title:'新标题',summary:p.summary};
await run(p,[call('check_game'),call('write_game',replacement),call('publish_game'),expectError('publish_game',/check_game/,'read_game'),body=>{const current=result(body,'read_game');assert.equal(current.title,'新标题');assert.equal(current.published.title,'原游戏');assert.equal(current.draft.differs_from_published,true);return complete();}]);
assert.equal(roms.get(p.id),originalCart);assert.equal(p.revision,1);assert.equal(p.gfx,gfx);assert.deepEqual(clone(p.audio),audio);
// Both semantic and schema failures invalidate prior proof, even if bytes stay unchanged.
for(const args of [{...replacement,sprite_overrides:[{id:32,pixels:Array(8).fill('aaaaaaaa')},{id:32,pixels:Array(8).fill('bbbbbbbb')}]},{...replacement,lua:42}]){
 await run(p,[call('check_game'),call('write_game',args),body=>{assert(result(body,'write_game').error);return complete('publish_game');},expectError('publish_game',/check_game/)]);
 assert.equal(roms.get(p.id),originalCart);assert.equal(p.title,'新标题');
}
// Optional reference reads cannot change the draft or its assets. Unknown paths fail.
const before=clone(p);
await run(p,[call('read_skill',{module:'../../config.json'}),body=>{assert(result(body,'read_skill').error);return complete('read_skill',{module:'game-loop'});},body=>{assert.equal(result(body,'read_skill').path,'modules/game-loop.md');return complete();}], 'readonly');
for(const field of ['lua','title','summary','gfx','audio','revision','versions'])assert.deepEqual(clone(p[field]),before[field]);
// Reusing an Agent for another user turn must not carry a check into that turn.
const reused=projectAgent(p,config,()=>{},()=>{});
await run(p,[call('check_game'),call()],'readonly',reused);
assert.equal(p.revision,1);assert.equal(roms.get(p.id),originalCart,'A read-only check must not force or perform publication');
await run(p,[call('publish_game'),expectError('publish_game',/check_game/)],false,reused);
await run(p,[call('check_game'),call('publish_game'),call('publish_game'),expectError('publish_game',/check_game/,'read_game'),body=>{assert.equal(result(body,'read_game').draft.differs_from_published,false);return complete();}],true);
assert.equal(p.revision,2);assert.equal(p.versions.length,2);assert.equal(roms.get(p.id),cartridge(p.lua,p.gfx,p.audio));
// A failed native publication consumes the proof and retains its recoverable intent.
const interrupted=newProject();failPublish=true;
await run(interrupted,[call('check_game'),call('publish_game'),expectError('publish_game',/interrupted/,'publish_game'),expectError('publish_game',/check_game/)]);
assert.equal(interrupted.revision,0);assert.equal(interrupted.pending.revision,1);assert(!roms.has(interrupted.id));
await recoverProject(interrupted);assert.equal(interrupted.revision,1);assert.equal(interrupted.versions.length,1);assert(!interrupted.pending);
// Actual project wiring permits a 100-request workflow without the old 10-turn clamp.
const extended=newProject(),startRequests=requests;
await run(extended,[...Array.from({length:97},()=>call('check_game')),call('publish_game'),call('read_game'),body=>{assert.equal(result(body,'read_game').published.revision,1);return complete();}],true);
assert.equal(requests-startRequests,100);assert.equal(extended.revision,1);
// Re-checking identical bytes does not create another menu artifact or revision.
await run(extended,[call('check_game'),call('publish_game'),body=>{assert.equal(result(body,'publish_game').already_published,true);return complete();}],true);
assert.equal(extended.revision,1);assert.equal(extended.versions.length,1);
// Failed final prose cannot relabel a successfully published game as failed.
const delivered=newProject();steps=[call('check_game'),call('publish_game'),...Array.from({length:4},()=>()=>({status:503,text:''}))];
await assert.rejects(projectAgent(delivered,config,()=>{},()=>{}).prompt('生成游戏'),e=>e instanceof PublishedReplyError&&e.revision===1);
assert.equal(steps.length,0);assert.equal(delivered.revision,1);assert.equal(delivered.versions.length,1);assert(roms.has(delivered.id));
// The parser accepts this real-model failure, but delivery rejects it before
// creating a ROM. Correcting to the cart-native helper permits publication.
const incompatible=newProject(),badLua=STARTER+'\nfunction draw_bag() return table.remove(bag) end';
await run(incompatible,[call('write_game',{lua:badLua,title:'API regression',summary:'fixture'}),call('check_game'),expectError('check_game',/table.*标准库/,'publish_game'),expectError('publish_game',/check_game/)]);
assert.equal(incompatible.revision,0);assert(!roms.has(incompatible.id));
await run(incompatible,[call('write_game',{edits:[{oldText:'table.remove(bag)',newText:'deli(bag,#bag)'}]}),call('check_game'),call('publish_game'),call()],true);
assert.equal(incompatible.revision,1);assert(roms.get(incompatible.id).includes('deli(bag,#bag)'));
console.log(`PASS: ${requests} real Pi requests; exact-draft publication gate, rejected/skipped writes, title-only drafts, fresh read after publish, round boundaries, no duplicate unchecked publish, readonly module allowlist, and preserved graphics/audio/published cartridge.`);
