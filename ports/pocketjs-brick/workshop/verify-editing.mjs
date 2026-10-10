// Compare against the retained original Pi v0.81.1 algorithm, then run actual project tools.
import assert from 'node:assert/strict';
import vm from 'node:vm';
import path from 'node:path';
import {readFile} from 'node:fs/promises';
import {fileURLToPath} from 'node:url';
import {build} from 'esbuild';
import {createHash} from 'node:crypto';
const dir=path.dirname(fileURLToPath(import.meta.url)),clone=o=>JSON.parse(JSON.stringify(o));
const original=await readFile(path.join(dir,'third-party/pi-edit.upstream.ts'),'utf8');
const vendored=await readFile(path.join(dir,'third-party/pi-edit.ts'),'utf8');
assert(vendored.includes(createHash('sha256').update(original).digest('hex')));
const reference='import * as Diff from "diff";\n'+original.slice(original.indexOf('export function detectLineEnding'),original.indexOf('export interface EditDiffResult'));
const bundles=await Promise.all([build({stdin:{contents:reference,loader:'ts',resolveDir:dir},bundle:true,write:false,platform:'neutral',format:'iife',globalName:'Original'}),build({entryPoints:[path.join(dir,'editing.ts')],bundle:true,write:false,platform:'neutral',format:'iife',globalName:'Editing'})]);
const ctx=vm.createContext({});for(const b of bundles)vm.runInContext(b.outputFiles[0].text,ctx);
const upstream=ctx.Original,{updateLua,codeDifference}=ctx.Editing;
const applyReference=(s,edits)=>upstream.restoreLineEndings(upstream.applyEditsToNormalizedContent(upstream.normalizeToLF(s),edits,'当前游戏Lua').newContent,upstream.detectLineEnding(s));
const cases=[
 ['speed=2\nother=7\n',[{oldText:'test_speed=2',newText:'test_speed=1.5'}]],
 ['one=1\ntwo=2\n',[{oldText:'two=2',newText:'three=3'},{oldText:'one=1',newText:'two=2'}]],
 ['-- remove\nx=1\n',[{oldText:'-- remove\n',newText:''}]],
 ['x=1\nend\n',[{oldText:'x=1\nend',newText:'x=1\ny=2\nend'}]],
 ['speed=2\r\nother=7\r\n',[{oldText:'test_speed=2\nother=7',newText:'test_speed=1\nother=7'}]],
 ['name="cat"  \nuntouched=1  \n',[{oldText:'name=“cat”\n',newText:'name="dog"\n'}]],
 ['a=1\tb=2\nkeep=3  \n',[{oldText:'a=1\tb=2',newText:'a=9\tb=2'}]],
 ['x=1  \ny=2   \nkeep=3  \n',[{oldText:'x=1\ny=2\n',newText:'x=3\ny=4\n'}]],
 ['x=1\nx=1\n',[{oldText:'x=1',newText:'x=2'}]],
 ['abc=1\n',[{oldText:'abc',newText:'def'},{oldText:'bc',newText:'gh'}]],
 ['x=1\n',[{oldText:'missing',newText:'x=2'}]],
 ['x=1\n',[{oldText:'',newText:'x=2'}]],
 ['x=1\n',[{oldText:'x=1',newText:'x=1'}]],
 ['a=1\n',[{oldText:'a=1',newText:'b=2'},{oldText:'b=2',newText:'c=3'}]],
];
for(const [source,edits] of cases){let expected,error;try{expected=applyReference(source,edits);}catch(e){error=e.message;}if(error)assert.throws(()=>updateLua(source,{edits}),e=>e.message===error);else{const actual=updateLua(source,{edits});assert.equal(actual.lua,expected);const ref=upstream.generateDiffString(upstream.normalizeToLF(source),upstream.normalizeToLF(expected));assert.deepEqual(clone(codeDifference(source,expected)),{...clone(ref),diff_truncated:false});}}
// Repeated normalized lines must preserve unrelated lines and their whitespace.
const preserved='speed=2  \n-- keep  \nmarker=1\nspeed=2\n';
assert.equal(updateLua(preserved,{edits:[{oldText:'marker=1\nspeed=2\n',newText:'marker=1\nspeed=3\n'}]}).lua,'speed=2  \n-- keep  \nmarker=1\nspeed=3\n');
const huge=Array.from({length:3000},(_,i)=>'-- old '+i).join('\n'),newHuge=huge.replaceAll('old','new');
const diff=codeDifference(huge,newHuge);assert(diff.diff_truncated);assert(diff.diff.length<=8100);

const files=new Map(),roms=new Map();let replies=[],requests=0,callId=0,failDraft=false;
const response=(name,args={})=>({status:200,text:JSON.stringify({choices:[{message:{role:'assistant',content:name?null:'根据实际结果完成。',...(name?{tool_calls:[{id:'edit-'+(++callId),type:'function',function:{name,arguments:JSON.stringify(args)}}]}:{})},finish_reason:name?'tool_calls':'stop'}]})});
const harness={read:async n=>files.get(n),write:async(n,raw)=>{if(failDraft&&JSON.parse(raw).lua.includes('-- fail-save')){failDraft=false;throw new Error('fixture draft save failed');}files.set(n,raw);},task:input=>{if(input.op==='syntax')return {promise:Promise.resolve({text:'syntax_checked'})};roms.set(input.name,input.text);return {promise:Promise.resolve({text:'published'})};},request:(url,key,body)=>{requests++;const value=JSON.parse(body);if(!value.tools)return {promise:Promise.resolve(response()),cancel(){}};assert(replies.length);return {promise:Promise.resolve(replies.shift()(value)),cancel(){}};}};
const model=await build({entryPoints:[path.join(dir,'model.ts')],bundle:true,write:false,platform:'neutral',format:'iife',globalName:'Model',plugins:[{name:'native',setup(b){b.onResolve({filter:/services\.ts$/},()=>({path:'native',namespace:'native'}));b.onLoad({filter:/.*/,namespace:'native'},()=>({contents:'export const read=(...a)=>globalThis.__harness.read(...a);export const write=(...a)=>globalThis.__harness.write(...a);export const task=(...a)=>globalThis.__harness.task(...a);export const writeChunks=async(n,produce)=>{const parts=[];await produce(p=>parts.push(p));return write(n,parts.join(""));};export const delay=async()=>{};export const requestStream=undefined;export const request=(...a)=>globalThis.__harness.request(...a);'}));}}]});
ctx.__harness=harness;vm.runInContext(model.outputFiles[0].text,ctx);
const {newProject,projectAgent,decodeProject,STARTER,cartridge}=ctx.Model;
const config={key:'fixture',endpoint:'https://example.invalid',model:'deepseek-flash',timeoutMs:10000};
const result=(body,name)=>{const a=body.messages.filter(m=>m.tool_calls?.some(c=>c.function.name===name)).at(-1),id=a.tool_calls.find(c=>c.function.name===name).id;const raw=body.messages.find(m=>m.role==='tool'&&m.tool_call_id===id).content;try{return JSON.parse(raw);}catch{return {error:raw};}};
const call=(name,args={})=>()=>response(name,args);
let p=newProject();
async function revision(args,mode){const before=clone(p),expected=updateLua(p.lua,args).lua;replies=[call('read_game'),body=>{assert.equal(result(body,'read_game').lua,before.lua);return response('write_game',args);},body=>{const written=result(body,'write_game');assert.equal(written.draft_saved,true);assert.equal(written.mode,mode);assert.equal(p.lua,expected);if(mode==='edit')assert(written.diff.includes('+'));return response('check_game');},call('publish_game'),()=>response()];await projectAgent(p,config,()=>{},()=>{}).prompt('修改当前游戏');assert.equal(replies.length,0);assert.equal(p.revision,before.revision+1);assert.equal(roms.get(p.id),cartridge(p.lua,p.gfx,p.audio));return before;}
await revision({lua:'test_speed=2\n'+STARTER,title:'局部修改测试',summary:'保留已有玩法'},'replace');
const first=clone(p),originalCart=roms.get(p.id);
await revision({edits:[{oldText:'test_speed=2',newText:'test_speed=1.5'},{oldText:'cls(1)',newText:'cls(2)'}]},'edit');
assert.equal(p.title,first.title);assert.equal(p.summary,first.summary);assert.equal(p.gfx,first.gfx);assert.deepEqual(clone(p.audio),first.audio);assert.equal(p.versions[0].lua,first.lua);
p=decodeProject(files.get('projects/'+p.id+'.json'));
const originalLua=p.lua;
await revision({title:'只改名称'},'metadata-assets');assert.equal(p.lua,originalLua);
await revision({sprite_overrides:[{id:32,pixels:Array(8).fill('aaaaaaaa')}]},'metadata-assets');assert.equal(p.lua,originalLua);
const gfx=p.gfx;await revision({summary:'只改摘要'},'metadata-assets');assert.equal(p.gfx,gfx);assert.equal(p.lua,originalLua);
await revision({edits:[{oldText:'test_speed=1.5',newText:'test_speed=1'}],summary:'只降低速度'},'edit');
const invalids=[{}, {lua:p.lua,edits:[{oldText:'test_speed=1',newText:'test_speed=2'}]}, {edits:[]}, {edits:[{oldText:'end',newText:'end -- changed'}]}, {edits:[{oldText:'not here',newText:'x=1'}]}, {edits:[{oldText:'test_speed=1',newText:'test_speed=1'}]}, {edits:[{oldText:'test_speed=1',newText:'test_speed=2'},{oldText:'speed',newText:'fast'}]}, {edits:[{oldText:'test_speed=1',newText:'test_speed=2'},{oldText:'absent',newText:'ok'}]}, {edits:[{oldText:'function _init',newText:'function broken'}]}, {edits:[{oldText:'test_speed=1',newText:'test_speed=猫'}]}, {edits:[{oldText:'test_speed=1',newText:'test_speed=3'}],sprite_overrides:[{id:32,pixels:['a']}]}, {edits:[{oldText:'test_speed=1',newText:'test_speed=3'}],music_overrides:[{id:8,channels:[63,-1,-1,-1],loop_start:true,loop_end:true,stop:false}]}];
for(const args of invalids){const before=clone(p),cart=roms.get(p.id);replies=[call('check_game'),call('write_game',args),body=>{assert(result(body,'write_game').error,JSON.stringify(args));return response('publish_game');},body=>{assert.match(result(body,'publish_game').error,/check_game/);return response();}];await assert.rejects(projectAgent(p,config,()=>{},()=>{}).prompt('无效修改'),/尚未提交/);for(const key of ['lua','title','summary','gfx','audio','versions','revision'])assert.deepEqual(clone(p[key]),before[key]);assert.equal(roms.get(p.id),cart);}
// A real persistence failure rolls back the draft and leaves the published game untouched.
const before=clone(p),cart=roms.get(p.id);failDraft=true;
replies=[call('write_game',{edits:[{oldText:'test_speed=1',newText:'test_speed=1\n-- fail-save'}],title:'不应保存'}),body=>{assert.match(result(body,'write_game').error,/draft save failed/);return response();}];
await assert.rejects(projectAgent(p,config,()=>{},()=>{}).prompt('保存失败'),/尚未提交/);
for(const key of ['lua','title','summary','gfx','audio','versions','revision'])assert.deepEqual(clone(p[key]),before[key]);assert.equal(roms.get(p.id),cart);assert.equal(decodeProject(files.get('projects/'+p.id+'.json')).lua,before.lua);assert(!failDraft);
assert.notEqual(cart,originalCart);
// Large source passes real Pi schema validation, persistence, checks and publish;
// complete old/new edit blocks also exceed the removed 28KB schema limits.
// This fixture checks large edit/schema/persistence parity, at the user's
// production 500k window. Smaller-window compaction has its own batch suite;
// this whole-source replacement deliberately sends both old/new 60KB blocks.
p=newProject();config.contextWindowTokens=500000;
const largeSource='-- preserve this line\n'.repeat(2900)+STARTER;
assert(largeSource.length>60000);assert(largeSource.length<65535);
await revision({lua:largeSource,title:'大源码测试'},'replace');
const largeUpdated=largeSource.replace('your next game','large source still works');
await revision({edits:[{oldText:largeSource,newText:largeUpdated}]},'edit');
assert.equal(p.lua,largeUpdated);assert.equal(decodeProject(files.get('projects/'+p.id+'.json')).lua,largeUpdated);
assert(roms.get(p.id).length>80000,'Exercise the removed native cartridge gate, too');
const small={edits:[{oldText:'test_speed=2',newText:'test_speed=1.5'}]};
const largeLua='speed=2\n'+'-- unchanged game code\n'.repeat(1000)+STARTER;
console.log(`PASS: ${cases.length} original Pi edit/diff parity cases; ${requests} actual Pi requests; whole/batch/metadata/sprite updates, preservation, reopen/versioning, unique/overlap/missing/mixed/no-op/invalid-source/invalid-asset rejection, save rollback, and publication gate. Illustrative payload: full ${JSON.stringify({lua:largeLua}).length} chars vs local ${JSON.stringify(small).length} chars (not a model latency benchmark).`);
