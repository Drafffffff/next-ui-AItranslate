import assert from 'node:assert/strict';
import vm from 'node:vm';
import {build} from 'esbuild';
import {readFileSync} from 'node:fs';
import {performance} from 'node:perf_hooks';
const dir=new URL('.',import.meta.url).pathname;
async function load(file,old){
 const b=await build({entryPoints:[dir+file],bundle:true,write:false,format:'iife',platform:'neutral',globalName:'Module',plugins:old?[{name:'baseline',setup(b){b.onLoad({filter:/context\.ts$/},()=>({contents:readFileSync(old,'utf8'),resolveDir:dir,loader:'ts'}));}}]:[]});
 const ctx=vm.createContext({TextEncoder,URL,AbortController,AbortSignal,structuredClone});vm.runInContext(b.outputFiles[0].text,ctx);return ctx.Module;
}
const module=await load('context.ts'),save=await load('persistence.ts');
const messages=Array.from({length:80000},(_,i)=>({role:'user',content:'旧游戏要求 '+i+' '+'history '.repeat(12),timestamp:i}));
const record={schema:1,summary:'保留当前游戏规则，继续修改速度。',firstKept:79990,through:80000,createdAt:1,tokensBefore:500000};
const settings={latest:record,onCommit:async()=>{throw Error('unexpected compaction');}};
const model={maxTokens:65536};const manager=module.createContextManager(settings,{},model,'skill');
const original=JSON.stringify(messages);let start=performance.now();await manager.transform(messages);const first=performance.now()-start;
const estimated=manager.metrics().estimates;messages.push({role:'user',content:'新要求',timestamp:80001});await manager.transform(messages);assert.equal(manager.metrics().estimates,estimated+1,'Only appended messages should be estimated');
assert.equal(JSON.stringify(messages.slice(0,80000)),original,'Projection cannot mutate local history');
assert.equal(manager.project(messages).length,12,'Compacted projection should depend on retained history');
const p={schema:1,id:'game-perf',title:'长对话',revision:1,updatedAt:1,lua:'source',summary:'game',messages,versions:[]};
const snapshot=save.projectSnapshot(p);p.messages.push({role:'user',content:'later',timestamp:80002});let yields=0,maxSlice=0,last=performance.now();start=last;
const json=await save.serializeProject(snapshot,async()=>{const now=performance.now();maxSlice=Math.max(maxSlice,now-last);last=now;yields++;});
assert.equal(JSON.parse(json).messages.length,80001);assert.equal(json,JSON.stringify(snapshot));assert(yields>0,'Large save should yield between batches');
console.log(`PASS: 80k local log unchanged; retained context=${manager.project(messages).length}; first projection ${first.toFixed(2)}ms; save ${(performance.now()-start).toFixed(1)}ms, yields=${yields}, longest intermediate slice=${maxSlice.toFixed(1)}ms (desktop CPU, excludes final join/native write).`);
if(process.env.WORKSHOP_BASELINE_CONTEXT){
 const old=await load('context.ts',process.env.WORKSHOP_BASELINE_CONTEXT);
 for(const [name,lib] of [['before',old],['after',module]]){
  const m=lib.createContextManager(settings,{},model,'skill');start=performance.now();for(let i=0;i<20;i++)await m.transform(messages);
  console.log(`context_${name}_20_calls_ms=${(performance.now()-start).toFixed(2)}`);
 }
}
