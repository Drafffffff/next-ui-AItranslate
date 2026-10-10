// Actual Pi project tools with isolated model/files, plus tracker encoding invariants.
import assert from 'node:assert/strict';
import vm from 'node:vm';
import path from 'node:path';
import {build} from 'esbuild';
import {fileURLToPath} from 'node:url';
import {mkdir,writeFile} from 'node:fs/promises';
const dir=path.dirname(fileURLToPath(import.meta.url));
const clone=o=>JSON.parse(JSON.stringify(o));
const files=new Map(),roms=new Map();let replies=[],requests=0;
const harness={read:async n=>files.get(n),write:async(n,raw)=>{files.set(n,raw);},task:input=>{if(input.op==='syntax')return {promise:Promise.resolve({text:'syntax_checked'})};roms.set(input.name,input.text);return {promise:Promise.resolve({text:'published'})};},request:(url,key,body)=>{requests++;const value=JSON.parse(body);if(!value.tools)return {promise:Promise.resolve({status:200,text:JSON.stringify({choices:[{message:{role:'assistant',content:'## Goal\n保留当前游戏与音频，修改自定义声部；下一步重新read_game及read_skill music读取真实数据。'},finish_reason:'stop'}]})}),cancel(){}};assert(replies.length);return {promise:Promise.resolve(replies.shift()(value)),cancel(){}};}};
const model=await build({entryPoints:[path.join(dir,'model.ts')],bundle:true,write:false,platform:'neutral',format:'iife',globalName:'Model',plugins:[{name:'native',setup(b){b.onResolve({filter:/services\.ts$/},()=>({path:'native',namespace:'native'}));b.onLoad({filter:/.*/,namespace:'native'},()=>({contents:'export const read=(...a)=>globalThis.__harness.read(...a);export const write=(...a)=>globalThis.__harness.write(...a);export const task=(...a)=>globalThis.__harness.task(...a);export const writeChunks=async(n,produce)=>{const parts=[];await produce(p=>parts.push(p));return write(n,parts.join(""));};export const delay=async()=>{};export const requestStream=undefined;export const request=(...a)=>globalThis.__harness.request(...a);'}));}}]});
const audio=await build({entryPoints:[path.join(dir,'audio.ts')],bundle:true,write:false,platform:'neutral',format:'iife',globalName:'Audio'});
const context=vm.createContext({__harness:harness});vm.runInContext(model.outputFiles[0].text,context);vm.runInContext(audio.outputFiles[0].text,context);
const {newProject,decodeProject,projectAgent,recoverProject,cartridge,STARTER}=context.Model;
const {BUILTIN_AUDIO,validateAudio,mergeAudio,audioSections}=context.Audio;
validateAudio(BUILTIN_AUDIO);
assert.equal(BUILTIN_AUDIO.sfx.length,14);assert.equal(BUILTIN_AUDIO.music.length,7);
const sound=(id,pitch=36)=>({id,speed:16,loop_start:0,loop_end:0,notes:Array.from({length:32},(_,i)=>[pitch,0,i%4===0?3:0,0])});
const pattern={id:8,channels:[16,17,-1,-1],loop_start:true,loop_end:true,stop:false};
let p=newProject();assert.deepEqual(clone(p.audio),clone(BUILTIN_AUDIO));
const config={key:'fixture-key',endpoint:'https://example.invalid',model:'deepseek-flash',timeoutMs:10000};
let callId=0;
const response=(name,args={})=>({status:200,text:JSON.stringify({choices:[{message:{role:'assistant',content:name?null:'已保存音乐，等待试听。',...(name?{tool_calls:[{id:'audio-'+(++callId),type:'function',function:{name,arguments:JSON.stringify(args)}}]}:{})},finish_reason:name?'tool_calls':'stop'}]})});
const tool=(body,name)=>{const a=body.messages.filter(m=>m.tool_calls?.some(c=>c.function.name===name)).at(-1);const id=a.tool_calls.find(c=>c.function.name===name).id;const text=body.messages.find(m=>m.role==='tool'&&m.tool_call_id===id).content;try{return JSON.parse(text);}catch{return {error:text};}};
async function revision(overrides={},includeCode=true,noChange=false){
 const old=clone(p.audio),rev=p.revision;
 replies=[body=>{assert(body.messages[0].content.includes('modules/music.md'));assert(!body.messages[0].content.includes('一条 SFX：'));return response('read_game');},body=>{
  assert.equal(tool(body,'read_game').assets.audio,undefined);assert.equal(tool(body,'read_game').assets.audio_summary.details_module,'music');
  return response('read_skill',{module:'music'});
 },body=>{
  const module=tool(body,'read_skill');assert.equal(module.path,'modules/music.md');assert.deepEqual(module.project_assets.audio,old);assert.equal(module.project_assets.project_id,p.id);
  return response('write_game',{...(includeCode?{lua:STARTER+'-- tracker example\n',title:'音乐测试',summary:'保留游戏，修改音乐'}:{}),...overrides});
 },body=>{assert.equal(tool(body,'write_game').draft_saved,true);return response('check_game');},body=>{assert.equal(tool(body,'check_game').format_checked,true);return response('publish_game');},body=>{assert.equal(tool(body,'publish_game').revision,rev+(noChange?0:1));return response();}];
 await projectAgent(p,config,()=>{},()=>{},undefined,'修改游戏音乐').prompt('修改游戏音乐');assert.equal(p.revision,rev+(noChange?0:1));
 assert.equal(roms.get(p.id),cartridge(p.lua,p.gfx,p.audio));
}
await revision({sfx_overrides:[sound(16),sound(17,24)],music_overrides:[pattern]});
const first=clone(p.audio);p=decodeProject(files.get('projects/'+p.id+'.json'));
const sourceBefore=p.lua,titleBefore=p.title;
await revision({sfx_overrides:[sound(16,40)]},false);assert.equal(p.lua,sourceBefore);assert.equal(p.title,titleBefore);assert.deepEqual(clone(p.audio.music),first.music);assert.deepEqual(clone(p.audio.sfx.find(s=>s.id===17)),first.sfx.find(s=>s.id===17));
assert.deepEqual(clone(p.versions[0].audio),first);
const second=clone(p.audio);await revision({},true,true);assert.deepEqual(clone(p.audio),second);
for(const invalid of [{sfx_overrides:[sound(16),sound(16)]},{sfx_overrides:[sound(8)]},{sfx_overrides:[{...sound(16),notes:[[64,0,3,0],...sound(16).notes.slice(1)]}]},{sfx_overrides:[{...sound(16),notes:[[36,8,3,0],...sound(16).notes.slice(1)]}]},{music_overrides:[{...pattern,channels:[63,-1,-1,-1]}]},{music_overrides:[{...pattern,loop_end:false,loop_start:false}]},{music_overrides:[{...pattern,stop:true}]}]){
 const before=clone(p);
 replies=[()=>response('write_game',{lua:STARTER+'-- rejected\n',title:'不应保存',summary:'invalid',...invalid}),body=>{assert(tool(body,'write_game').error);return response();}];
 await assert.rejects(projectAgent(p,config,()=>{},()=>{}).prompt('无效音符'),/尚未提交/);
 for(const field of ['lua','title','summary','gfx','audio','revision','versions'])assert.deepEqual(clone(p[field]),before[field]);
}
const pending=clone(p);pending.pending={revision:4,title:p.title,summary:p.summary,lua:p.lua,gfx:p.gfx,audio:first};
await recoverProject(pending);assert.equal(pending.revision,4);assert.deepEqual(clone(pending.audio),first);assert(!pending.pending);
assert.equal(roms.get(p.id),cartridge(p.lua,p.gfx,first));
const bad=clone(p);bad.audio.sfx[0].notes[0][2]=8;assert.throws(()=>decodeProject(JSON.stringify(bad)),/音效/);
const legacy=clone(p);delete legacy.audio;for(const v of legacy.versions)delete v.audio;
await recoverProject(legacy);assert(!roms.get(p.id).includes('__sfx__'));assert(!cartridge(STARTER).includes('__sfx__'));
const encoded=audioSections(p.audio);const sfx=encoded.split('__sfx__\n')[1].split('\n__music__')[0].split('\n'),music=encoded.split('__music__\n')[1].trimEnd().split('\n');
assert.equal(sfx.length,64);assert(sfx.every(row=>/^[0-9a-f]{168}$/.test(row)));assert.equal(music.length,64);assert(music.every(row=>/^[0-9a-f]{2} [0-9a-f]{8}$/.test(row)));
assert.equal(music[6],'03 0e0f4344');assert.equal(music[8],'03 10114344');assert(sfx[16].startsWith('0010000028030'));
const highest=mergeAudio(BUILTIN_AUDIO,[sound(63,63)],[{...pattern,id:63,channels:[63,-1,-1,-1]}]);assert(cartridge(STARTER,p.gfx,highest).length<80000);
const output=path.resolve(dir,'../../../build/pocketjs-port/workshop-audio-verification');await mkdir(output,{recursive:true});
await writeFile(path.join(output,'audio-fixture.p8'),cartridge(STARTER,p.gfx,BUILTIN_AUDIO));
await writeFile(path.join(output,'custom-audio-fixture.p8'),cartridge(STARTER,p.gfx,p.audio));
console.log(`PASS: ${requests} actual Pi requests; tracker source/default guide, structured composing and retuning, untouched voices, version snapshots, reopen/pending recovery, legacy silence, range/reference/loop rejection before mutation, exact SFX/music rows and native cart size. Audio fixtures: ${output}`);
