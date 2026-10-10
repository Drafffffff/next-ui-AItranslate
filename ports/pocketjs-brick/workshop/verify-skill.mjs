// Real Pi/project tools with isolated file and model fixtures; no paid requests.
import assert from 'node:assert/strict';
import vm from 'node:vm';
import path from 'node:path';
import {readFile} from 'node:fs/promises';
import {fileURLToPath} from 'node:url';
import {build} from 'esbuild';
const directory=path.dirname(fileURLToPath(import.meta.url));
const files=new Map(),roms=new Map(),requests=[];
let responses=[];
const harness={
 read:async name=>{assert(files.has(name));return files.get(name);},
 write:async(name,text)=>{files.set(name,text);return {text:''};},
 task:input=>{if(input.op==='syntax')return {promise:Promise.resolve({text:'syntax_checked'})};assert.equal(input.op,'publish');roms.set(input.name,input.text);return {promise:Promise.resolve({text:'published'})};},
 request:(url,key,body)=>{assert.equal(key,'fixture-key');const value=JSON.parse(body);requests.push(value);assert(responses.length,'Unexpected model request');return {promise:Promise.resolve(responses.shift()(value)),cancel(){}};},
};
const bundled=await build({entryPoints:[path.join(directory,'model.ts')],bundle:true,write:false,platform:'neutral',target:'es2020',format:'iife',globalName:'WorkshopModel',
 plugins:[{name:'isolated-native',setup(build){build.onResolve({filter:/services\.ts$/},()=>({path:'services',namespace:'isolated-native'}));build.onLoad({filter:/.*/,namespace:'isolated-native'},()=>({contents:'export const read=(...args)=>globalThis.__harness.read(...args);export const write=(...args)=>globalThis.__harness.write(...args);export const task=(...args)=>globalThis.__harness.task(...args);export const writeChunks=async(n,produce)=>{const parts=[];await produce(p=>parts.push(p));return write(n,parts.join(""));};export const delay=async()=>{};export const requestStream=undefined;export const request=(...args)=>globalThis.__harness.request(...args);',loader:'js'}));}}]});
const context=vm.createContext({__harness:harness});vm.runInContext(bundled.outputFiles[0].text,context);
const {newProject,decodeProject,projectAgent,recoverProject,cartridge,STARTER}=context.WorkshopModel;
const config={key:'fixture-key',endpoint:'https://example.invalid',model:'deepseek-flash',timeoutMs:10000};
const complete=(calls=[],text='已保存，等待试玩。')=>({status:200,text:JSON.stringify({choices:[{message:{role:'assistant',content:calls.length?null:text,...(calls.length?{tool_calls:calls}:{})},finish_reason:calls.length?'tool_calls':'stop'}]})});
let nextId=0;
const call=(name,args={})=>({id:'skill-'+(++nextId),type:'function',function:{name,arguments:JSON.stringify(args)}});
const lastResult=(body,name)=>{const assistant=body.messages.filter(m=>m.tool_calls?.some(c=>c.function.name===name)).at(-1);const id=assistant.tool_calls.find(c=>c.function.name===name).id;const text=body.messages.find(m=>m.role==='tool'&&m.tool_call_id===id).content;try{return JSON.parse(text);}catch{return {error:text};}};
const gfxOf=cart=>cart.split('\n__gfx__\n')[1].split('\n__')[0].trimEnd();
const pixels=(gfx,id)=>{const rows=gfx.split('\n'),x=id%16*8,y=Math.floor(id/16)*8;return rows.slice(y,y+8).map(row=>row.slice(x,x+8));};
const starter=await readFile(path.resolve(directory,'../../../skills/pico8-game-dev/assets/starter.p8'),'utf8');
const builtin=gfxOf(starter),blank=('0'.repeat(128)+'\n').repeat(128).trimEnd();
const patch=(id,color)=>({id,pixels:Array(8).fill(color.repeat(8))});
const statuses=[];
let project=newProject();assert.equal(project.gfx,builtin);
async function revision(p,number,overrides,expectCustom){
 const args={lua:STARTER+`-- revision ${number}\n`,title:'素材游戏',summary:'保留已有玩法并修改素材',...(overrides!==undefined?{sprite_overrides:overrides}:{})};
 responses=[()=>complete([call('read_game')]),body=>{
  const read=lastResult(body,'read_game');assert.equal(read.project_id,p.id);assert.equal(read.revision,number-1);
  assert.equal(read.assets.sprites.length,12);assert.deepEqual(read.assets.custom_sprites,expectCustom);
  return complete([call('write_game',args)]);
 },body=>{assert.equal(lastResult(body,'write_game').draft_saved,true);return complete([call('check_game'),call('publish_game')]);},
 body=>{assert.equal(lastResult(body,'check_game').gameplay_tested,false);assert.equal(lastResult(body,'publish_game').revision,number);return complete();}];
 await projectAgent(p,config,text=>statuses.push(text),()=>{}).prompt('修改这个游戏');assert.equal(responses.length,0);assert.equal(p.revision,number);
}
await revision(project,1,[patch(32,'a'),patch(33,'c')],[]);
let first=project.gfx;assert.deepEqual(pixels(first,32),patch(32,'a').pixels);assert.deepEqual(pixels(first,33),patch(33,'c').pixels);
for(let id=1;id<=12;id++)assert.deepEqual(pixels(first,id),pixels(builtin,id));
assert.equal(gfxOf(roms.get(project.id)),first);
// Reopen from exactly the saved JSON. Omitted overrides must keep all pixels.
project=decodeProject(files.get('projects/'+project.id+'.json'));
await revision(project,2,undefined,[patch(32,'a'),patch(33,'c')]);assert.equal(project.gfx,first);
await revision(project,3,[patch(32,'b')],[patch(32,'a'),patch(33,'c')]);
assert.deepEqual(pixels(project.gfx,32),patch(32,'b').pixels);assert.deepEqual(pixels(project.gfx,33),patch(33,'c').pixels);
assert.equal(project.versions[0].gfx,first);assert.equal(project.versions[1].gfx,first);
// Duplicate sprite IDs pass the JSON shape check but are rejected before any mutation.
const before={lua:project.lua,title:project.title,gfx:project.gfx,revision:project.revision};
responses=[()=>complete([call('write_game',{lua:STARTER,title:'不应写入',summary:'invalid',sprite_overrides:[patch(32,'a'),patch(32,'b')]})]),
 body=>{assert.match(lastResult(body,'write_game')?.error||body.messages.at(-1).content,/唯一/);return complete([], '素材参数有误，尚未发布。');}];
await assert.rejects(projectAgent(project,config,()=>{},()=>{}).prompt('重复精灵参数'),/尚未提交/);
for(const [key,value] of Object.entries(before))assert.equal(project[key],value);
// Old project/version records have no gfx. Reopening preserves their published blank sheet.
const legacy=JSON.parse(JSON.stringify(project));legacy.id='game-legacy';delete legacy.gfx;delete legacy.audio;
for(const version of legacy.versions){delete version.gfx;delete version.audio;}
const reopened=decodeProject(JSON.stringify(legacy));await recoverProject(reopened);
assert.equal(gfxOf(roms.get(reopened.id)),blank);assert.equal(reopened.gfx,undefined);
// A pending publication carries the custom pixels through restart.
const pending=decodeProject(JSON.stringify(project));pending.pending={revision:4,title:pending.title,summary:'恢复素材',lua:STARTER,gfx:pending.gfx};
await recoverProject(pending);assert.equal(pending.revision,4);assert.equal(gfxOf(roms.get(pending.id)),project.gfx);assert(!pending.pending);
assert.equal(gfxOf(cartridge(STARTER)),blank);
// Complete teaching games are progressively read through the actual Skill tool.
const examples=JSON.parse(await readFile(path.resolve(directory,'../../../skills/pico8-game-dev/assets/examples/catalog.json'),'utf8')).examples;
for(const example of examples){
 const lua=await readFile(path.resolve(directory,'../../../skills/pico8-game-dev/'+example.path),'utf8');
 const p=newProject();
 responses=[body=>{
  const prompt=body.messages[0].content;
  assert(!prompt.includes(lua));assert(!prompt.includes('<example name='));
  assert(body.tools.some(t=>t.function.name==='read_skill'));
  return complete([call('read_game')]);
 },body=>{
  const current=lastResult(body,'read_game');assert.equal(current.revision,0);assert.equal(current.lua,STARTER);assert.equal(current.assets.audio,undefined);
  return complete([call('read_skill',{module:'example/'+example.id})]);
 },body=>{
  const reference=lastResult(body,'read_skill');assert.equal(reference.path,example.path);assert.equal(reference.kind,'complete-lua');assert.equal(reference.advisory,true);assert(reference.text.includes(lua));
  assert.equal(p.lua,STARTER);assert.equal(p.revision,0); // Loading a reference cannot mutate the project.
  return ['mesh3d','raycaster'].includes(example.id)?complete([call('read_skill',{module:'3d'})]):complete([call('write_game',{lua,title:example.title,summary:example.when})]);
 },...(['mesh3d','raycaster'].includes(example.id)?[body=>{const reference=lastResult(body,'read_skill');assert.equal(reference.module,'3d');assert(reference.text.includes('DDA'));return complete([call('write_game',{lua,title:example.title,summary:example.when})]);}]:[]),
 body=>{assert.equal(lastResult(body,'write_game').draft_saved,true);return complete([call('check_game')]);},
 body=>{assert.equal(lastResult(body,'check_game').gameplay_tested,false);return complete([call('publish_game')]);},
 body=>{assert.equal(lastResult(body,'publish_game').revision,1);return complete();}];
 await projectAgent(p,config,()=>{},()=>{}).prompt('创建'+example.title);
 assert.equal(p.lua,lua);assert.equal(p.revision,1);assert(roms.get(p.id).includes(lua.trimEnd()));assert.equal(p.gfx,builtin);
 assert(p.messages.some(m=>m.role==='toolResult'&&m.toolName==='read_skill'));
}
// Routing relies on actual source and Agent decisions, not request keyword guesses.
const selectionBundle=await build({entryPoints:[path.join(directory,'skill.ts')],bundle:true,write:false,platform:'neutral',format:'iife',globalName:'Selection'});
vm.runInContext(selectionBundle.outputFiles[0].text,context);
const {readSkillModule,pico8SystemPrompt,SKILL_MODULE_NAMES}=context.Selection;
const basePrompt=pico8SystemPrompt(false),modifyPrompt=pico8SystemPrompt(true);
assert(basePrompt.length<14000,'Keep default context bounded: '+basePrompt.length);
const guide=await readFile(path.resolve(directory,'../../../skills/pico8-game-dev/modules/game-loop.md'),'utf8');
const api=await readFile(path.resolve(directory,'../../../skills/pico8-game-dev/references/pico8-api.md'),'utf8');
const music=await readFile(path.resolve(directory,'../../../skills/pico8-game-dev/modules/music.md'),'utf8');
for(const text of [guide.trim(),api.trim(),music.trim()])assert(!basePrompt.includes(text));
for(const e of examples){const source=await readFile(path.resolve(directory,'../../../skills/pico8-game-dev/'+e.path),'utf8');assert(!basePrompt.includes(source));assert(!modifyPrompt.includes(source));}
assert.equal(readSkillModule('music').text,music.trim());assert.equal(readSkillModule('api').text,api.trim());
assert.equal(readSkillModule('game-loop').text,guide.trim());
assert.throws(()=>readSkillModule('../../config.json'),/未知Skill/);
assert.throws(()=>readSkillModule('Music'),/未知Skill/);
assert.equal(new Set(SKILL_MODULE_NAMES).size,20);
// Library references must be tool-addressable and stay out of the default
// context. Exercise the actual Pi tool boundary, not just a direct helper read.
const research=JSON.parse(await readFile(path.resolve(directory,'../../../skills/pico8-game-dev/references/library-cases.json'),'utf8'));
assert.equal(research.cases.filter(c=>c.review_level==='mechanism-review').length,17);
const libraryModules=['library-patterns','platform-feel','grid-turns','racing-physics','combat-design','level-design'];
for(const name of libraryModules){
 const guide=await readFile(path.resolve(directory,'../../../skills/pico8-game-dev/modules/'+name+'.md'),'utf8');
 assert(!basePrompt.includes(guide.trim()));assert(!modifyPrompt.includes(guide.trim()));
 assert(SKILL_MODULE_NAMES.includes(name));
 const p=decodeProject(files.get('projects/'+project.id+'.json'));
 const before=JSON.stringify({lua:p.lua,gfx:p.gfx,audio:p.audio,revision:p.revision,versions:p.versions});
 responses=[()=>complete([call('read_game')]),body=>{
  assert.equal(lastResult(body,'read_game').lua,p.lua);
  const tool=body.tools.find(t=>t.function.name==='read_skill');
  assert(tool.function.parameters.properties.module.enum.includes(name));
  return complete([call('read_skill',{module:name})]);
 },body=>{
  const result=lastResult(body,'read_skill');
  assert.equal(result.path,'modules/'+name+'.md');assert.equal(result.kind,'reference');assert.equal(result.advisory,false);
  assert(result.text.startsWith(guide.trim()));
  if(name==='library-patterns')for(const c of research.cases.filter(c=>c.observations.length))assert(result.text.includes(c.source_url));
  return complete([], '已分析参考方法，尚未修改游戏。');
 }];
 await projectAgent(p,config,()=>{},()=>{}).prompt('先研究现有游戏的相关机制，暂不改动');
 assert.equal(responses.length,0);assert.equal(JSON.stringify({lua:p.lua,gfx:p.gfx,audio:p.audio,revision:p.revision,versions:p.versions}),before);
 assert(p.messages.some(m=>m.role==='toolResult'&&m.toolName==='read_skill'));
}
// Every provider call includes the core Skill, contract and readable module index, not full optional guides.
const skill=await readFile(path.resolve(directory,'../../../skills/pico8-game-dev/SKILL.md'),'utf8');
for(const body of requests){
 const system=body.messages.filter(message=>message.role==='system');assert.equal(system.length,1);
 assert(system[0].content.includes(skill.replace(/^---\n[\s\S]*?\n---\n/,'').trim()));
 assert(system[0].content.includes('<reference name="references/project-contract.md" mode="tools">'));
 assert(system[0].content.includes('<skill_modules>'));assert(system[0].content.includes('<asset_catalog name="assets/catalog.json">'));
 assert(!system[0].content.includes(music.trim()));assert(!system[0].content.includes(api.trim()));assert(!system[0].content.includes(guide.trim()));
 assert(!system[0].content.includes('## 单次 JSON 模式：开发端参考'));assert(!JSON.stringify(body).includes(config.key));
}
assert(!JSON.stringify(project.messages).includes('<skill name='));
console.log(`PASS: ${requests.length} actual Pi requests; mandatory core ${basePrompt.length} chars, modules/examples loaded only through read_skill, unchanged live game on reference reads, persisted load traces, seven examples publish; existing sprite/reopen/recovery checks pass.`);
