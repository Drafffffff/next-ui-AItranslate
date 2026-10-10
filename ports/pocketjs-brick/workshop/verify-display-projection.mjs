import assert from 'node:assert/strict';import vm from 'node:vm';import {build} from 'esbuild';
async function module(file){const b=await build({entryPoints:[new URL(file,import.meta.url).pathname],bundle:true,write:false,platform:'neutral',format:'iife',globalName:'M'});const c=vm.createContext({});vm.runInContext(b.outputFiles[0].text,c);return c.M;}
const {displayText}=await module('../app/display-text.ts');
for(const [input,expected] of [['游戏🎮生成器','游戏生成器'],['音乐🎵＋操作🔊','音乐＋操作'],['👨‍👩‍👧‍👦角色 🇨🇳','角色 '],['A SR SE ↑↓ →← O/X','A SR SE ↑↓ →← O/X'],['按钮1️⃣','按钮1']])assert.equal(displayText(input),expected);
const {projectReads}=await module('read-projection.ts');
const read=(id,lua)=>({role:'toolResult',toolCallId:id,toolName:'read_game',content:[{type:'text',text:JSON.stringify({project_id:'game-one',revision:id,lua})}],timestamp:1,isError:false});
const messages=[read('old','old source\n'.repeat(20000)),read('new','actual current source')],original=JSON.stringify(messages),projection=projectReads(messages);
assert.equal(JSON.stringify(messages),original);assert(JSON.stringify(projection).length<original.length/100);assert(JSON.parse(projection[0].content[0].text).superseded_read);assert.equal(projection[1],messages[1]);assert.equal(projection[0].toolCallId,'old');
const write=(id,args)=>({role:'assistant',content:[{type:'toolCall',id,name:'write_game',arguments:args}],timestamp:1});
const saved=id=>({role:'toolResult',toolCallId:id,toolName:'write_game',content:[{type:'text',text:'{"draft_saved":true}'}],timestamp:1,isError:false});
const oldWrite=write('old-write',{lua:'old source\n'.repeat(30000),title:'保留标题',sprite_overrides:[{id:32,pixels:['aaaaaaaa']}]});
const newWrite=write('current-write',{edits:[{oldText:'before',newText:'after'}]});
const history=[oldWrite,saved('old-write'),newWrite,saved('current-write')],raw=JSON.stringify(history),dedup=projectReads(history);
assert.equal(JSON.stringify(history),raw);assert(JSON.stringify(dedup).length<raw.length/100);
assert.equal(dedup[0].content[0].id,'old-write');assert.equal(dedup[0].content[0].arguments.title,'保留标题');assert(!('lua' in dedup[0].content[0].arguments));
assert.equal(dedup[2],newWrite);assert.equal(dedup[3],history[3]);assert.equal(projectReads(history)[0],dedup[0]);
const failed=write('failed',{lua:'needed repair evidence'}),error={...saved('failed'),isError:true};
assert.equal(projectReads([...history,failed,error]).at(-2),failed);
const later=projectReads([...history,read('now','exact current source')]);assert(!('edits' in later[2].content[0].arguments));
assert.equal(projectReads(history)[2],newWrite,'Earlier contexts still retain their latest successful write');
for(const invalid of [null,'{\"edits\":[]}',4,true,['malformed']]){const bad=write('bad',invalid),trace=[bad,{...saved('bad'),isError:true},newWrite,saved('current-write')],before=JSON.stringify(trace);const projected=projectReads(trace);assert.equal(JSON.stringify(trace),before);assert.equal(projected[0],bad,'Rejected primitive/array arguments retain their raw failure evidence without crashing future projection');}
console.log('PASS display-only emoji removal; stale source reads/writes deduplicated with stable identities, raw history and tool pairing preserved, current and unsuperseded failed writes retained.');
